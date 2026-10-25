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
#include <stdexcept>
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

		// The response payload is stored ONCE, in exactly one of these two members
		// (see StoreBody): `text` for textual responses, `body` for binary ones.
		// The other member is always empty, so nothing is duplicated in memory.
		std::vector<BYTE> body; // binary payload (images, archives, ...)
		std::string text; // textual payload (JSON, XML, HTML, plain text, ...)

		std::map<std::string, std::string> headers;
		std::map<std::string, std::string> cookies;
		std::string error;

		/// @return Pointer to the payload bytes (text or binary), nullptr when empty.
		const char *data() const {
			if ( !text.empty() )
				return text.data();
			if ( !body.empty() )
				return reinterpret_cast<const char *>( body.data() );
			return nullptr;
		}

		/// @return Size of the payload in bytes (text or binary).
		size_t size() const {
			return !text.empty() ? text.size() : body.size();
		}

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
		 * @brief When true, the URL is sent as pure ASCII: spaces and non-ASCII
		 *        characters (after the host) are percent-encoded as UTF-8 (%XX).
		 *        When false (default) the URL is sent untouched, which is what
		 *        APIs that expect real characters need.
		 */
		bool onlyASCII = false;

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
	 * @brief Exception carrying the response status that must be reported.
	 *
	 * Thrown by the drivers instead of a plain std::runtime_error whenever the
	 * failure is already classified, so the status never has to be guessed by
	 * searching the error text.
	 */
	struct FetchError : public std::runtime_error {
		int status;
		FetchError( int s, const std::string &msg ) :
			std::runtime_error( msg ),
			status( s ) {
		}
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
		std::atomic<bool> completed{ false }; // result ready, callback not dispatched yet
		std::atomic<bool> dispatched{ false }; // result consumed by the callback dispatch (until the next :send())
		std::atomic<bool> cancelled{ false };
		std::atomic<bool> timedOut{ false }; // total deadline expired (see DeadlineGuard)
		std::atomic<int> workerAlive{ 0 }; // number of detached worker threads still running
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

		// Read by worker threads while the main thread clears it on unload.
		std::atomic<bool> rainValid{ true };

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

		/**
		 * @brief Called when the total deadline expires: closes the tracked handles
		 *        (aborting any blocking call) WITHOUT marking the request as cancelled.
		 */
		void expireDeadline();
	};



	/**
	 * @class DeadlineGuard
	 * @brief Total-deadline watchdog for one request (RAII).
	 *
	 * Uses a timer-queue timer (no extra thread). When it fires it calls
	 * FetchContext::expireDeadline(), which closes the open network handles so a
	 * blocked synchronous WinHTTP/WinINet call returns immediately instead of
	 * waiting for its own, possibly much longer, timeout.
	 *
	 * stop() waits for a running callback to finish, so the context is guaranteed
	 * not to be touched after it returns. The destructor calls stop().
	 */
	class DeadlineGuard {
	public:
		DeadlineGuard( FetchContext *ctx, DWORD milliseconds );
		~DeadlineGuard();

		DeadlineGuard( const DeadlineGuard & ) = delete;
		DeadlineGuard &operator=( const DeadlineGuard & ) = delete;

		void stop();

	private:
		HANDLE timer_ = nullptr;
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



	// ------------------------------------------------------------------
	// Shared helpers used by both drivers
	// ------------------------------------------------------------------

	/**
	 * @brief Case-insensitive header lookup in a request header map.
	 * @return Iterator to the header, or headers.end() when it is not present.
	 */
	std::map<std::wstring, std::wstring>::const_iterator FindHeader( const std::map<std::wstring, std::wstring> &headers, const wchar_t *name );

	/**
	 * @brief Maps a Win32 / WinHTTP / WinINet error code to a FetchResponse status.
	 *
	 * @param err                   The error code (GetLastError()).
	 * @param timeoutStatus         Status to use when the code means "timed out" - the
	 *                              caller knows which phase it was in.
	 * @param invalidResponseStatus Status to use for "invalid server response".
	 */
	int StatusFromNetError( DWORD err, int timeoutStatus, int invalidResponseStatus = FetchResponse::STATUS_NETWORK_ERROR );

	/**
	 * @brief Human readable (UTF-8) description of a WinHTTP/WinINet error code,
	 *        taken from the owning DLL's message table, with the numeric code appended.
	 * @param moduleName L"winhttp.dll" or L"wininet.dll".
	 */
	std::string FormatNetError( DWORD err, const wchar_t *moduleName );

	/// Percent-encodes spaces/control/non-ASCII characters of the URL (the scheme and
	/// the authority - user, host, port - are left untouched). See FetchRequest::onlyASCII.
	std::wstring EncodeUrlToAscii( const std::wstring &url );

	/// URL safe to write to the log: user info, query string and fragment are removed.
	std::wstring SanitizeUrlForLog( const std::wstring &url );

	/**
	 * @brief Stores the response payload in `text` OR `body`, never in both.
	 *
	 * The payload is considered text when the Content-Type is textual (text/ subtypes,
	 * json, xml, javascript, form-urlencoded, ...). Without a Content-Type the
	 * content itself is inspected (no NUL / binary control bytes).
	 * The headers must already be filled in.
	 */
	void StoreBody( FetchResponse &response, std::vector<BYTE> &&data );

	/**
	 * @brief Reads Content-Length from the (case-insensitive) response headers.
	 * @return true if present and valid.
	 */
	bool GetContentLength( const FetchResponse &response, size_t &length );



	// Public API functions
	void CleanupContexts( Rain *rain );

} // namespace core
