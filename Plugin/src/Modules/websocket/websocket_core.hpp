/**
 * @file websocket_core.hpp
 * @brief Core structures and registry for websocket module
 * @license GPL v2.0 License
 *
 * Contains the data shared between the Rainmeter (Lua) thread and the server
 * thread: configuration, event / command queues, connection state and the
 * thread-safe registry of the servers.
 *
 * Threading model:
 * - The server runs on ONE dedicated thread per server (see websocket_network.cpp).
 * - Lua is only ever touched by the Rainmeter thread.
 * - Server thread -> Lua : Event queue + WM_WEBSOCKET_EVENT (same idea as the fetch module).
 * - Lua -> server thread : Command queue + wake-up socket.
 */

#pragma once

/**
 * @brief Window message posted by the server thread when events are waiting.
 *
 * Posted via PostMessage() to the websocket notify window (HWND per Rain instance).
 * wParam carries the context ID (Context::refSelf) used to retrieve the
 * server from ContextRegistry. Only one message is pending at a time: the
 * handler drains the event queue (see Context::notifyPending).
 */
#define WM_WEBSOCKET_EVENT ( WM_APP + 4 )

#include <Windows.h>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <lua.hpp>




// Forward declarations
struct Rain;
struct lua_State;




namespace websocket::core {

	/**
	 * @struct Connection
	 * @brief State of one WebSocket connection, shared by both threads
	 */
	struct Connection {
		uint64_t id = 0;

		// Written by the server thread before the OPEN event is queued, read-only afterwards.
		std::string ip;

		// Cleared by the server thread when the socket closes.
		std::atomic<bool> open{ true };
	};



	/**
	 * @struct Event
	 * @brief Something that happened on the server thread and that Lua must be told about
	 */
	struct Event {
		static constexpr int OPEN = 0;
		static constexpr int MESSAGE = 1;
		static constexpr int CLOSE = 2;
		static constexpr int DRAIN = 3;
		static constexpr int PING = 4;
		static constexpr int PONG = 5;
		static constexpr int SERVER_ERROR = 6;
		static constexpr int RECONNECTING = 7; // client only: a new attempt is scheduled
		static constexpr int COUNT = 8;

		int type = OPEN;
		uint64_t connId = 0;
		std::shared_ptr<Connection> conn; // OPEN only
		std::string data; // message / ping / pong payload, close reason, error text
		bool binary = false;
		int code = 0; // close code; RECONNECTING: attempt number
		bool retry = false; // client: CLOSE / SERVER_ERROR that is followed by a reconnection
		unsigned int delay = 0; // RECONNECTING: ms until the attempt

		Event() = default;
	};



	/**
	 * @struct Command
	 * @brief Something Lua asks the server thread to do
	 */
	struct Command {
		static constexpr int SEND = 0;
		static constexpr int CLOSE = 1;
		static constexpr int SUBSCRIBE = 2;
		static constexpr int UNSUBSCRIBE = 3;
		static constexpr int PUBLISH = 4; // connId == 0: server level publish
		static constexpr int BROADCAST = 5; // every connection
		static constexpr int STOP = 6;

		int type = SEND;
		uint64_t connId = 0;
		std::string topic;
		std::string data; // payload / close reason
		bool binary = false;
		int code = 1000; // close code

		Command() = default;
	};



	/**
	 * @struct AuthConfig
	 * @brief Credential required to complete the WebSocket upgrade
	 */
	struct AuthConfig {
		static constexpr int AUTH_NONE = 0;
		static constexpr int AUTH_HEADER = 1; // custom request header
		static constexpr int AUTH_QUERY = 2; // query string parameter
		static constexpr int AUTH_PROTOCOL = 3; // Sec-WebSocket-Protocol entry

		int type = AUTH_NONE;
		std::string name; // lowercase header name / query parameter (unused for AUTH_PROTOCOL)
		std::vector<std::string> secrets; // any of them is accepted
	};



	/**
	 * @struct ClientConfig
	 * @brief What a ws:// client needs on top of Config (isClient == true)
	 */
	struct ClientConfig {
		/// Automatic reconnection (clients only).
		struct Reconnect {
			bool enabled = false;
			unsigned int delay = 1000; // ms before the first retry, doubled on every failure
			unsigned int maxDelay = 30000; // ms, upper bound of the delay
			unsigned int maxTries = 0; // consecutive retries before giving up, 0 = never give up
		};

		Reconnect reconnect;
		std::string url; // as given by Lua, for url()
		std::string target = "/"; // request target: path + query
		std::string hostHeader; // value of the Host header
		std::vector<std::pair<std::string, std::string>> headers; // extra request headers
		std::vector<std::string> protocols; // Sec-WebSocket-Protocol offers
		unsigned int connectTimeout = 5000; // ms, TCP connect + handshake
	};



	/**
	 * @struct Config
	 * @brief Everything the Lua options describe (server or client)
	 */
	struct Config {
		bool isClient = false; // true: connect to host:port instead of listening
		ClientConfig client;

		std::string host = "127.0.0.1";
		int port = 8765; // 0 = let the system choose
		int portTries = 20; // consecutive ports tried when the port is busy

		std::string path = "/*"; // exact path, "/prefix/*" or "/*" (everything)

		AuthConfig auth;

		unsigned int maxPayload = 1024 * 1024;
		unsigned int maxBackpressure = 1024 * 1024; // bytes queued per connection, 0 = unlimited
		unsigned short idleTimeout = 120; // seconds, 0 = disabled
		bool sendPings = true; // ping a silent connection at half of idleTimeout
		int maxConnections = 0; // 0 = unlimited

		Config() = default;
	};



	/**
	 * @struct Context
	 * @brief State of one websocket endpoint: a server or a client (Config::isClient)
	 */
	struct Context : std::enable_shared_from_this<Context> {
		static constexpr int START_PENDING = 0;
		static constexpr int START_OK = 1;
		static constexpr int START_FAILED = 2;

		// Configuration (read-only once the server thread runs)
		Config config;

		// Thread-safe state
		std::atomic<bool> running{ false }; // listening and serving
		std::atomic<bool> stopped{ false }; // stop() was called: no more callbacks
		std::atomic<bool> rainValid{ true };
		std::atomic<int> workerAlive{ 0 }; // 1 while the detached server thread is running
		std::atomic<int> boundPort{ 0 };
		std::atomic<int> connectionCount{ 0 };
		std::atomic<bool> clientClosing{ false }; // client: close() was called
		std::atomic<uint64_t> nextConnId{ 1 };
		std::atomic<uint64_t> droppedEvents{ 0 };
		std::atomic<bool> notifyPending{ false }; // a WM_WEBSOCKET_EVENT is already on its way
		std::atomic<bool> wakePending{ false }; // a drain of the command queue is already deferred
		std::atomic<bool> wanted[Event::COUNT]; // true when Lua registered a handler for the event

		// Start-up handshake: listen() waits until the server thread reports success or failure
		std::mutex startMutex;
		std::condition_variable startCv;
		int startState = START_PENDING;
		std::string startError;

		// Server thread -> Lua
		std::mutex eventMutex;
		std::deque<Event> events;

		// Lua -> server thread
		std::mutex commandMutex;
		std::deque<Command> commands;

		// Wake-up socket of the server thread (opaque here, see websocket_network.cpp).
		// Only valid while the loop is running.
		std::mutex loopMutex;
		void *waker = nullptr;

		// Lua integration - Rainmeter thread only
		Rain *rain = nullptr;
		int refSelf = -1; // id in ContextRegistry
		int refSelfLua = LUA_NOREF; // keeps the server object alive while it runs
		int refHandlers[Event::COUNT];
		std::unordered_map<uint64_t, int> connRefs; // connection id -> registry ref of its Lua object

		HWND hNotifyWindow = nullptr;

		Context( Rain *r );
		~Context();

		/**
		 * @brief Releases every Lua registry reference held by this context.
		 *
		 * Must run on the Rainmeter thread while the Lua state is still valid.
		 */
		void releaseLuaRefs();
	};



	/**
	 * @struct ContextRegistry
	 * @brief Global thread-safe registry for server contexts
	 */
	struct ContextRegistry {
		static ContextRegistry &instance();

		// No copying
		ContextRegistry( const ContextRegistry & ) = delete;
		ContextRegistry &operator=( const ContextRegistry & ) = delete;

		int registerContext( std::shared_ptr<Context> ctx );
		std::shared_ptr<Context> getContext( int id );
		void removeContext( int id );
		void removeAllByRain( Rain *rain );
		size_t count();

	private:
		ContextRegistry() = default;
		~ContextRegistry() = default;

		std::mutex mutex_;
		std::unordered_map<int, std::shared_ptr<Context>> contexts_;
		std::atomic<int> nextId_{ 1 };
	};



	// Public API functions
	void CleanupContexts( Rain *rain );

} // namespace websocket::core
