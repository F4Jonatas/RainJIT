/**
 * @file core.hpp
 * @brief Core structures and registry for fetch module
 * @license GPL v2.0 License
 *
 * Contains the main data structures (FetchRequest, FetchResponse, FetchContext)
 * and the thread-safe registry for managing async requests.
 */

#pragma once

/**
 * @brief Window message sent by the worker thread when an HTTP request completes.
 *
 * Posted via PostMessage() to the fetch notify window (HWND per Rain instance).
 * wParam carries the context ID (FetchContext::refSelf) used to retrieve
 * the completed context from ContextRegistry.
 *
 * Defined here - core.hpp is the single header included by all fetch
 * submodules - to avoid duplicating the constant across http.cpp, wininet.cpp
 * and lua.cpp.
 */
#define WM_FETCH_COMPLETE ( WM_APP + 3 )

#include <Windows.h>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <lua.hpp>




// Forward declarations
struct Rain;
struct lua_State;




namespace core {

	/**
	 * @struct FetchResponse
	 * @brief HTTP response data container
	 */
	struct FetchResponse {
		int status = 0;
		std::vector<BYTE> body;
		std::map<std::string, std::string> headers;
		std::map<std::string, std::string> cookies;
		std::string error;
		std::string text;

		static const int STATUS_NETWORK_ERROR = -1; // Generic network error
		static const int STATUS_CANCELLED = -2; // Request cancelled by the user
		static const int STATUS_INVALID_URL = -3; // Invalid / malformed URL
		static const int STATUS_NO_INTERNET = -4; // No internet connection
		static const int STATUS_THREAD_ERROR = -5; // Failed to create the worker thread

		static const int STATUS_ABORTED = -6; // Operation aborted (E_ABORT)
		static const int STATUS_CONNECTION_LOST = -7; // Connection lost during transfer
		static const int STATUS_SSL_ERROR = -8; // SSL/TLS error
		static const int STATUS_PROXY_ERROR = -9; // Proxy error
		static const int STATUS_TIMEOUT_DNS = -10; // DNS resolution timeout
		static const int STATUS_TIMEOUT_CONNECT = -11; // Connection timeout
		static const int STATUS_TIMEOUT_SEND = -12; // Send timeout
		static const int STATUS_TIMEOUT_RECEIVE = -13; // Receive timeout
		static const int STATUS_CHUNKED_ERROR = -14; // Failed to process chunked encoding

		FetchResponse() = default;
	};



	/**
	 * @struct FetchRequest
	 * @brief HTTP request configuration
	 */
	struct FetchRequest {
		std::wstring url;
		std::wstring method = L"GET";
		std::map<std::wstring, std::wstring> headers;
		std::vector<BYTE> body;

		// General timeout (kept for compatibility)
		int timeout = 30000;

		// Phase-specific timeouts (in milliseconds)
		// Note: ignored when driver == "wininet" (not supported by WinINet)
		int dnsTimeout = 0; // 0 = use the general timeout
		int connectTimeout = 0; // 0 = use the general timeout
		int sendTimeout = 0; // 0 = use the general timeout
		int receiveTimeout = 0; // 0 = use the general timeout

		bool followRedirects = true;
		bool HTTPVersion = false;

		/**
		 * @brief HTTP driver/stack to use for this request.
		 *
		 * Supported values:
		 * - "winhttp"  (default) - WinHTTP stack, clean and fast.
		 * - "wininet"            - WinINet stack, inherits IE/Edge session context.
		 *                         Useful for sites that block non-browser clients
		 *                         (e.g. Cloudflare-protected feeds).
		 *
		 * @note When "wininet" is used, phase-specific timeouts (dnsTimeout,
		 *       connectTimeout, sendTimeout, receiveTimeout) are not supported
		 *       and will be ignored with a warning.
		 */
		std::string driver = "winhttp";

		FetchRequest() = default;
	};



	/**
	 * @struct NetHandle
	 * @brief Opaque network handle (WinHTTP/WinINet) plus the function that closes it.
	 *
	 * The closer is supplied by the driver that created the handle, because
	 * winhttp.h and wininet.h cannot be included in the same translation unit.
	 * This keeps core.cpp independent from both stacks.
	 */
	struct NetHandle {
		void *handle;
		void ( *closer )( void * );
	};



	/**
	 * @struct FetchContext
	 * @brief Async fetch context with thread-safe state
	 */
	struct FetchContext {
		// Configuration
		FetchRequest request;

		// Thread-safe state
		std::atomic<bool> threadActive{ false };
		std::atomic<bool> completed{ false };
		std::atomic<bool> cancelled{ false };
		std::mutex mutex;

		// Network handles created by the worker thread. They are tracked so that
		// requestCancel() can close them from any thread - closing the handle is the
		// only way to interrupt a blocking synchronous WinHTTP/WinINet call.
		std::mutex handleMutex;
		std::vector<NetHandle> netHandles;

		// Results
		FetchResponse response;

		// Lua integration
		Rain *rain = nullptr;
		int refSelf = -1;
		int refSelfLua = LUA_NOREF;
		int refCallback = LUA_NOREF;

		HWND hNotifyWindow = nullptr;
		bool rainValid = true;

		FetchContext( Rain *r );
		~FetchContext();

		void releaseLuaRefs();

		/**
		 * @brief Registers a network handle so that requestCancel() can close it.
		 *
		 * Must be called by the driver right after each successful Open/Connect call.
		 * If the request was already cancelled, the handle is closed immediately and
		 * false is returned - the driver must then abort.
		 *
		 * @param handle Native handle (HINTERNET).
		 * @param closer Function that closes the handle (WinHttpCloseHandle / InternetCloseHandle).
		 * @return true if the handle is now tracked, false if it was closed because of a cancel.
		 */
		bool trackHandle( void *handle, void ( *closer )( void * ) );

		/**
		 * @brief Closes every tracked handle (children first).
		 *
		 * Safe to call repeatedly and from any thread: each handle is closed only once.
		 */
		void closeHandles();

		/**
		 * @brief Marks the request as cancelled and closes the tracked handles,
		 *        which aborts any blocking network call. Callable from any thread.
		 */
		void requestCancel();
	};



	/**
	 * @class ContextRegistry
	 * @brief Global thread-safe registry for fetch contexts
	 */
	struct ContextRegistry {
		static ContextRegistry &instance();

		// No copying
		ContextRegistry( const ContextRegistry & ) = delete;
		ContextRegistry &operator=( const ContextRegistry & ) = delete;

		int registerContext( std::shared_ptr<FetchContext> ctx );
		std::shared_ptr<FetchContext> getContext( int id );
		void removeContext( int id );
		void removeAllByRain( Rain *rain );
		size_t count();

	private:
		ContextRegistry() = default;
		~ContextRegistry() = default;

		std::mutex mutex_;
		std::unordered_map<int, std::shared_ptr<FetchContext>> contexts_;
		std::atomic<int> nextId_{ 1 };
	};



	// Public API functions
	void CleanupContexts( Rain *rain );

} // namespace core
