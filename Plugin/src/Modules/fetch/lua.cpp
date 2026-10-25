/**
 * @file lua.cpp
 * @brief Lua bindings implementation for fetch module
 * @license GPL v2.0 License
 *
 * Both the request object (fetch.async) and the response object are full
 * userdata sharing one metatable per Lua state. Compared with tables this
 * avoids allocating a table plus one closure per method for every response.
 */

#include <lua.hpp>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <thread>
#include <unordered_map>
#include <vector>

#include "http.hpp"
#include "wininet.hpp"
#include "lua.hpp"

#include <Includes/rain.hpp>
#include <RainmeterAPI.hpp>
#include <Utils/filesystem.hpp>

#include "../json5/json.hpp"
#include "../html/html.hpp"
#include "../xml/xml.hpp"

// Global map associating each Rain* with the HWND of its notification window
static std::unordered_map<Rain *, HWND> g_notifyWindows;
static std::mutex g_notifyMutex;

// The window class is registered when the first window is created and removed
// again when the last one is destroyed, so nothing stays registered after the
// plugin DLL is unloaded.
static bool g_classRegistered = false;
static const wchar_t *const NOTIFY_CLASS_NAME = L"RainJIT_FetchNotifyWindow";



namespace lua {

	// Names of the metatables stored in the Lua registry
	static const char *const REQUEST_MT  = "RainJIT.FetchRequest";
	static const char *const RESPONSE_MT = "RainJIT.FetchResponse";
	static const char *const HEADERS_MT  = "RainJIT.FetchHeaders";

	/// Userdata payload of a request object (fetch.async).
	struct RequestUD {
		std::shared_ptr<core::FetchContext> ctx;
	};

	/// Userdata payload of a response object.
	struct ResponseUD {
		core::FetchResponse response;
	};



	// Logs through Rainmeter when a log target is available
	static void LogMsg( Rain *rain, int level, const std::wstring &msg ) {
#ifdef __RAINMETERAPI_H__
		if ( rain && rain->rm )
			RmLog( rain->rm, level, msg.c_str() );
#else
		(void)rain;
		(void)level;
		(void)msg;
#endif
	}



	// Window procedure
	// wParam carries the context ID (ctx->refSelf)
	LRESULT CALLBACK FetchNotifyWndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam ) {
		if ( msg == WM_FETCH_COMPLETE ) {
			int ctxId = static_cast<int>( wParam );
			auto ctx = core::ContextRegistry::instance().getContext( ctxId );

			if ( ctx && ctx->completed.load() && ctx->rain && ctx->rain->L && ctx->refSelfLua != LUA_NOREF ) {
				lua_State *L = ctx->rain->L;

				// Whatever happens below, the Lua stack ends exactly as it was found.
				const int top = lua_gettop( L );

				lua_pushcfunction( L, lua::fetch_dispatch );
				lua_rawgeti( L, LUA_REGISTRYINDEX, ctx->refSelfLua ); // request object

				if ( lua_pcall( L, 1, 0, 0 ) != LUA_OK ) {
					const char *err = lua_tostring( L, -1 );
					if ( err )
						LogMsg( ctx->rain, LOG_ERROR, utf8_to_wstring( err ) );
				}

				lua_settop( L, top );
			}

			return 0;
		}

		return DefWindowProc( hWnd, msg, wParam, lParam );
	}



	// HINSTANCE of this DLL (not of the host executable)
	static HINSTANCE ModuleInstance() {
		static HINSTANCE instance = nullptr;
		if ( !instance ) {
			HMODULE module = nullptr;
			if ( GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>( &FetchNotifyWndProc ), &module ) )
				instance = module;
			else
				instance = GetModuleHandle( nullptr );
		}

		return instance;
	}



	// Creates the notification window for a Rain*
	void CreateNotifyWindow( Rain *rain ) {
		std::lock_guard<std::mutex> lock( g_notifyMutex );

		if ( g_notifyWindows.find( rain ) != g_notifyWindows.end() )
			return;

		// Register the window class with the first window
		if ( !g_classRegistered ) {
			WNDCLASSEX wc = { sizeof( WNDCLASSEX ) };
			wc.lpfnWndProc = FetchNotifyWndProc;
			wc.hInstance = ModuleInstance();
			wc.lpszClassName = NOTIFY_CLASS_NAME;

			if ( !RegisterClassEx( &wc ) ) {
				DWORD err = GetLastError();
				if ( err != ERROR_CLASS_ALREADY_EXISTS ) {
					LogMsg( rain, LOG_ERROR, L"Failed to register fetch notify window class" );
					return;
				}
			}

			g_classRegistered = true;
		}

		// clang-format off
		HWND hWnd = CreateWindowEx(
			0,
			NOTIFY_CLASS_NAME,
			L"", 0, 0, 0, 0, 0,
			nullptr,
			nullptr,
			ModuleInstance(),
			nullptr
		);
		// clang-format on

		if ( hWnd ) {
			// Store the Rain* pointer in the window (optional, useful for debugging)
			SetWindowLongPtr( hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( rain ) );
			g_notifyWindows[rain] = hWnd;
		}

		else
			LogMsg( rain, LOG_ERROR, L"Failed to create fetch notify window" );
	}



	// Returns the HWND associated with the Rain*
	HWND GetNotifyWindow( Rain *rain ) {
		std::lock_guard<std::mutex> lock( g_notifyMutex );
		auto it = g_notifyWindows.find( rain );
		return ( it != g_notifyWindows.end() ) ? it->second : nullptr;
	}



	// Destroys the window, removes it from the map and, with the last window,
	// unregisters the window class.
	void DestroyNotifyWindow( Rain *rain ) {
		std::lock_guard<std::mutex> lock( g_notifyMutex );

		auto it = g_notifyWindows.find( rain );
		if ( it != g_notifyWindows.end() ) {
			HWND hWnd = it->second;
			g_notifyWindows.erase( it );

			if ( hWnd )
				DestroyWindow( hWnd );
		}

		if ( g_notifyWindows.empty() && g_classRegistered ) {
			UnregisterClass( NOTIFY_CLASS_NAME, ModuleInstance() );
			g_classRegistered = false;
		}
	}



	// ------------------------------------------------------------------
	// Userdata helpers
	// ------------------------------------------------------------------

	// Raises a Lua error unless argument `idx` is a request object.
	static RequestUD *CheckRequest( lua_State *L, int idx ) {
		return static_cast<RequestUD *>( luaL_checkudata( L, idx, REQUEST_MT ) );
	}

	// Raises a Lua error unless argument `idx` is a response object.
	static ResponseUD *CheckResponse( lua_State *L, int idx ) {
		return static_cast<ResponseUD *>( luaL_checkudata( L, idx, RESPONSE_MT ) );
	}

	std::shared_ptr<core::FetchContext> GetFetchContext( lua_State *L, int idx ) {
		return CheckRequest( L, idx )->ctx;
	}



	// ------------------------------------------------------------------
	// Response object
	// ------------------------------------------------------------------

	// Builds a table with the given header/cookie pairs. Keys are stored lowercased
	// and a shared metatable lowercases every lookup, so t["Content-Type"] works.
	static void PushCaseInsensitiveTable( lua_State *L, const std::map<std::string, std::string> &map ) {
		lua_newtable( L );
		for ( const auto &pair : map ) {
			std::string lowerKey = pair.first;
			for ( char &c : lowerKey )
				c = static_cast<char>( tolower( static_cast<unsigned char>( c ) ) );

			lua_pushlstring( L, lowerKey.data(), lowerKey.size() );
			lua_pushlstring( L, pair.second.data(), pair.second.size() );
			lua_rawset( L, -3 );
		}

		luaL_getmetatable( L, HEADERS_MT );
		lua_setmetatable( L, -2 );
	}

	// __index of the shared headers/cookies metatable: lowercases the key, then raw get
	static int headers_index( lua_State *L ) {
		const char *key = lua_tostring( L, 2 );
		if ( !key )
			return 0;

		std::string lower( key );
		for ( char &c : lower )
			c = static_cast<char>( tolower( static_cast<unsigned char>( c ) ) );

		lua_pushlstring( L, lower.data(), lower.size() );
		lua_rawget( L, 1 );
		return 1;
	}

	// Moves the response into a new userdata. The payload is kept in exactly one
	// place on the C++ side (`text` or `body`, see core::StoreBody).
	void PushResponse( lua_State *L, core::FetchResponse &&response ) {
		void *mem = lua_newuserdata( L, sizeof( ResponseUD ) );
		new ( mem ) ResponseUD{ std::move( response ) };

		luaL_getmetatable( L, RESPONSE_MT );
		lua_setmetatable( L, -2 );
	}

	static int response_gc( lua_State *L ) {
		auto *r = static_cast<ResponseUD *>( lua_touserdata( L, 1 ) );
		if ( r )
			r->~ResponseUD();
		return 0;
	}

	// response.ok / response.status / response.error are computed on access;
	// everything else is looked up in the shared method table (upvalue 1).
	static int response_index( lua_State *L ) {
		ResponseUD *r = CheckResponse( L, 1 );

		if ( lua_type( L, 2 ) == LUA_TSTRING ) {
			const char *key = lua_tostring( L, 2 );

			if ( strcmp( key, "ok" ) == 0 ) {
				lua_pushboolean( L, r->response.status >= 200 && r->response.status < 300 );
				return 1;
			}

			if ( strcmp( key, "status" ) == 0 ) {
				lua_pushinteger( L, r->response.status );
				return 1;
			}

			if ( strcmp( key, "error" ) == 0 ) {
				lua_pushlstring( L, r->response.error.data(), r->response.error.size() );
				return 1;
			}
		}

		lua_pushvalue( L, lua_upvalueindex( 1 ) );
		lua_pushvalue( L, 2 );
		lua_rawget( L, -2 );
		return 1;
	}

	static int response_newindex( lua_State *L ) {
		return luaL_error( L, "fetch response is read-only" );
	}

	// Pushes the payload (text or binary) as a Lua string
	static void PushPayload( lua_State *L, const ResponseUD *r ) {
		if ( r->response.size() > 0 )
			lua_pushlstring( L, r->response.data(), r->response.size() );
		else
			lua_pushliteral( L, "" );
	}

	// :text() / :bytes() - the raw body (Lua strings are byte arrays)
	static int response_text( lua_State *L ) {
		PushPayload( L, CheckResponse( L, 1 ) );
		return 1;
	}

	// :headers()
	static int response_headers( lua_State *L ) {
		PushCaseInsensitiveTable( L, CheckResponse( L, 1 )->response.headers );
		return 1;
	}

	// :cookies()
	static int response_cookies( lua_State *L ) {
		PushCaseInsensitiveTable( L, CheckResponse( L, 1 )->response.cookies );
		return 1;
	}

	/**
	 * @brief Lua: response:xml( [options] ) -> xml.document | nil, err
	 *
	 * Parses the body with xml::Parse (the parser behind xml.parse()), without going
	 * through require(). The xml metatables are registered on demand by xml::Parse.
	 *
	 * @par Stack
	 * - [1] response
	 * - [2] optional table: cache (boolean), parse_full (boolean)
	 *
	 * @note Upvalue 1: Rain*.
	 */
	static int response_xml( lua_State *L ) {
		// Checked first: on failure it raises a Lua error, which must not skip destructors.
		ResponseUD *r = CheckResponse( L, 1 );
		const int hasOptions = lua_istable( L, 2 );

		xml::ParseOptions opts;
		if ( hasOptions ) {
			lua_getfield( L, 2, "cache" );
			if ( lua_isboolean( L, -1 ) )
				opts.cache = lua_toboolean( L, -1 ) != 0;
			lua_pop( L, 1 );

			lua_getfield( L, 2, "parse_full" );
			if ( lua_isboolean( L, -1 ) )
				opts.parse_full = lua_toboolean( L, -1 ) != 0;
			lua_pop( L, 1 );
		}
		lua_settop( L, 1 );

		Rain *rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );
		const char *data = r->response.data();
		std::string err;

		if ( xml::Parse( L, std::string_view( data ? data : "", r->response.size() ), opts, err ) ) {
			lua_pushnil( L );
			return 2;
		}

		LogMsg( rain, LOG_ERROR, L"[RainJIT:Fetch] response:xml() - " + utf8_to_wstring( err ) );
		lua_pushnil( L );
		lua_pushlstring( L, err.data(), err.size() );
		return 2;
	}

	/**
	 * @brief Lua: response:html() -> html.document | nil, err
	 *
	 * Parses the body with html::Parse (the parser behind html.parse()), without going
	 * through require(). The html metatables are registered on demand by html::Parse.
	 *
	 * @note Upvalue 1: Rain*.
	 */
	static int response_html( lua_State *L ) {
		// Checked first: on failure it raises a Lua error, which must not skip destructors.
		ResponseUD *r = CheckResponse( L, 1 );
		lua_settop( L, 1 );

		Rain *rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );
		const char *data = r->response.data();
		std::string err;

		if ( html::Parse( L, std::string_view( data ? data : "", r->response.size() ), err ) ) {
			lua_pushnil( L );
			return 2;
		}

		LogMsg( rain, LOG_ERROR, L"[RainJIT:Fetch] response:html() - " + utf8_to_wstring( err ) );
		lua_pushnil( L );
		lua_pushlstring( L, err.data(), err.size() );
		return 2;
	}

	/**
	 * @brief Lua: response:json() -> value | nil, err
	 *
	 * Parses the response payload with the json library of the plugin, calling
	 * json::Decode directly (no require, no intermediate Lua string). The result
	 * is the same as json.decode( response:text() ): JSON null becomes json.null
	 * and the root must be an object or an array.
	 *
	 * Upvalue 1: Rain* (used for logging).
	 *
	 * @param L Lua state; argument 1 is the response object.
	 * @return 1 value on success, or 2 values (nil, message) on failure. The
	 *         message carries the line and column of the syntax error. Failures
	 *         are also written to the Rainmeter log.
	 */
	static int response_json( lua_State *L ) {
		// Checked first: on failure it raises a Lua error, which must not skip destructors.
		ResponseUD *r = CheckResponse( L, 1 );
		lua_settop( L, 1 );

		Rain *rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );

		const char *data = r->response.data();
		std::string err;

		if ( json::Decode( L, std::string_view( data ? data : "", r->response.size() ), err ) )
			return 1;

		LogMsg( rain, LOG_ERROR, L"[RainJIT:Fetch] response:json() - parse failed: " + utf8_to_wstring( err ) );

		lua_settop( L, 1 );
		lua_pushnil( L );
		lua_pushlstring( L, err.data(), err.size() );
		return 2;
	}

	// :save(path) - resolves Rainmeter variables and relative paths via absPath
	static int response_save( lua_State *L ) {
		Rain *rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );

		ResponseUD *r = CheckResponse( L, 1 );
		const char *filePath = luaL_checkstring( L, 2 );

		if ( !*filePath ) {
			lua_pushboolean( L, 0 );
			lua_pushliteral( L, "Invalid file path" );
			return 2;
		}

		// Resolve Rainmeter variables and canonicalize path
		std::wstring wPath = utf8_to_wstring( filePath );
		if ( rain ) {
			std::wstring resolved = rain->absPath( wPath );
			if ( !resolved.empty() )
				wPath = resolved;
		}

		if ( wPath.empty() ) {
			lua_pushboolean( L, 0 );
			lua_pushliteral( L, "Invalid file path encoding" );
			return 2;
		}

		const char *data = r->response.data();
		const size_t size = r->response.size();

		if ( !data || size == 0 ) {
			lua_pushboolean( L, 0 );
			lua_pushliteral( L, "Empty response body" );
			return 2;
		}

		if ( !fs::CreateDirectoriesRecursive( wPath ) ) {
			lua_pushboolean( L, 0 );
			lua_pushliteral( L, "Failed to create directory" );
			return 2;
		}

		if ( fs::SaveToFile( wPath, reinterpret_cast<const BYTE *>( data ), size ) ) {
			lua_pushboolean( L, 1 );
			return 1;
		}

		lua_pushboolean( L, 0 );
		lua_pushliteral( L, "Failed to write file" );
		return 2;
	}



	// ------------------------------------------------------------------
	// Request object (fetch.async)
	// ------------------------------------------------------------------

	// __gc: the request object is no longer reachable from Lua. A request that is
	// still running is kept alive through ctx->refSelfLua, so here no worker can be
	// waiting for it; the context just has to leave the registry.
	static int request_gc( lua_State *L ) {
		auto *ud = static_cast<RequestUD *>( lua_touserdata( L, 1 ) );
		if ( !ud )
			return 0;

		if ( ud->ctx ) {
			{
				std::lock_guard<std::mutex> lock( ud->ctx->mutex );

				// Registry references that are still held are released with the state
				// (this runs while the state is being collected or closed).
				if ( ud->ctx->refCallback != LUA_NOREF ) {
					luaL_unref( L, LUA_REGISTRYINDEX, ud->ctx->refCallback );
					ud->ctx->refCallback = LUA_NOREF;
				}

				if ( ud->ctx->refSelfLua != LUA_NOREF ) {
					luaL_unref( L, LUA_REGISTRYINDEX, ud->ctx->refSelfLua );
					ud->ctx->refSelfLua = LUA_NOREF;
				}
			}

			core::ContextRegistry::instance().removeContext( ud->ctx->refSelf );
		}

		ud->~RequestUD();
		return 0;
	}



	// request:send( [callback] ) -> request
	//
	// Starts the request. The optional callback receives ( request, response ) when
	// the request finishes. It is registered only if the request is actually
	// started: calling :send() on a request that is already running changes nothing.
	int fetch_send( lua_State *L ) {
		RequestUD *ud = CheckRequest( L, 1 );

		const bool hasCallback = !lua_isnoneornil( L, 2 );
		if ( hasCallback && !lua_isfunction( L, 2 ) )
			return luaL_argerror( L, 2, "function expected" );

		const std::shared_ptr<core::FetchContext> &ctx = ud->ctx;

		{
			std::lock_guard<std::mutex> lock( ctx->mutex );

			// Already in progress, or the owner (skin) is gone
			if ( ctx->threadActive || !ctx->rainValid.load() || !ctx->rain || !ctx->rain->L ) {
				lua_pushvalue( L, 1 );
				return 1;
			}

			// Callback of this run (the previous one, if any, is replaced). The registry is
			// shared by all coroutines of the state, so it does not matter which one calls.
			if ( ctx->refCallback != LUA_NOREF ) {
				luaL_unref( L, LUA_REGISTRYINDEX, ctx->refCallback );
				ctx->refCallback = LUA_NOREF;
			}

			if ( hasCallback ) {
				lua_pushvalue( L, 2 );
				ctx->refCallback = luaL_ref( L, LUA_REGISTRYINDEX );
			}

			// Keep the request object alive while it runs - the script may drop it
			// right away, e.g. fetch.async( url ):send( callback ).
			if ( ctx->refSelfLua == LUA_NOREF ) {
				lua_pushvalue( L, 1 );
				ctx->refSelfLua = luaL_ref( L, LUA_REGISTRYINDEX );
			}

			// Configure state
			ctx->threadActive = true;
			ctx->completed = false;
			ctx->dispatched = false;
			ctx->cancelled = false;
			ctx->timedOut = false;
			ctx->response = core::FetchResponse();

			// Spawn thread using the configured driver. workerAlive is decremented as the
			// very last action of the thread, so cleanup can wait for it before the DLL
			// goes away.
			ctx->workerAlive.fetch_add( 1 );

			try {
				std::shared_ptr<core::FetchContext> worker = ctx;
				std::thread( [worker]() {
					if ( worker->request.driver == "wininet" )
						wininet::ExecuteFetchThread( worker );
					else
						http::ExecuteFetchThread( worker );

					worker->workerAlive.fetch_sub( 1 );
				} ).detach();
			} catch ( const std::exception &e ) {
				ctx->workerAlive.fetch_sub( 1 );
				ctx->threadActive = false;
				ctx->completed = true;
				ctx->response.error = "Failed to start request: " + std::string( e.what() );
				ctx->response.status = core::FetchResponse::STATUS_THREAD_ERROR;

				// No worker will ever post the completion message, so do it here; without it
				// the callback would never run for this request.
				if ( ctx->hNotifyWindow )
					PostMessage( ctx->hNotifyWindow, WM_FETCH_COMPLETE, static_cast<WPARAM>( ctx->refSelf ), 0 );
			}
		}

		lua_pushvalue( L, 1 );
		return 1;
	}



	// request:hasCompleted() -> boolean
	// true from the moment the request has finished until the next :send(),
	// whether or not its callback has already run.
	int fetch_hasCompleted( lua_State *L ) {
		RequestUD *ud = CheckRequest( L, 1 );

		bool done = false;
		{
			std::lock_guard<std::mutex> lock( ud->ctx->mutex );
			done = ud->ctx->completed || ud->ctx->dispatched;
		}

		lua_pushboolean( L, done ? 1 : 0 );
		return 1;
	}



	// Runs the callback of a finished request. Called from the notification window
	// (main thread) with the request object as argument 1.
	int fetch_dispatch( lua_State *L ) {
		RequestUD *ud = CheckRequest( L, 1 );
		const std::shared_ptr<core::FetchContext> &ctx = ud->ctx;

		bool fire = false;
		core::FetchResponse response;
		int refCallback = LUA_NOREF;

		{
			std::lock_guard<std::mutex> lock( ctx->mutex );

			if ( !ctx->completed )
				return 0;

			// A cancelled request is consumed silently: its callback never runs,
			// whatever result the worker thread ended up producing.
			fire = !ctx->cancelled.load();

			if ( fire ) {
				// Moved, not copied: the result is consumed here, and this frees the
				// (possibly large) payload held by the context right away.
				response = std::move( ctx->response );
				ctx->response = core::FetchResponse();
			}

			// The callback and the self reference belong to this run: release them now.
			// Argument 1 keeps the request object alive for the rest of this call, and a
			// :send() made from inside the callback registers them again.
			refCallback = ctx->refCallback;
			ctx->refCallback = LUA_NOREF;

			if ( ctx->refSelfLua != LUA_NOREF ) {
				luaL_unref( L, LUA_REGISTRYINDEX, ctx->refSelfLua );
				ctx->refSelfLua = LUA_NOREF;
			}

			ctx->completed = false; // Reset to avoid running the callback twice
			ctx->dispatched = true;
		}

		if ( refCallback == LUA_NOREF )
			return 0;

		lua_rawgeti( L, LUA_REGISTRYINDEX, refCallback );
		luaL_unref( L, LUA_REGISTRYINDEX, refCallback );

		if ( !fire || !lua_isfunction( L, -1 ) ) {
			lua_pop( L, 1 );
			return 0;
		}

		lua_pushvalue( L, 1 );                      // self
		PushResponse( L, std::move( response ) ); // response

		if ( lua_pcall( L, 2, 0, 0 ) != LUA_OK ) {
			const char *err = lua_tostring( L, -1 );
			LogMsg( ctx->rain, LOG_WARNING, L"[RainJIT:Fetch] Callback error: " + utf8_to_wstring( err ? err : "unknown error" ) );
			lua_pop( L, 1 );
		}

		return 0;
	}



	// request:cancel() -> request
	int fetch_cancel( lua_State *L ) {
		RequestUD *ud = CheckRequest( L, 1 );
		const std::shared_ptr<core::FetchContext> &ctx = ud->ctx;

		// Only meaningful while a worker thread is running (threadActive) or when its
		// result is finished but not yet dispatched (completed). In both cases the
		// callback is suppressed (see fetch_dispatch) and, if a worker is still
		// running, its blocking network call is aborted by closing the handles.
		// The worker thread itself finalizes the state (threadActive / completed /
		// STATUS_CANCELLED), so no state is faked here.
		if ( ctx->threadActive.load() || ctx->completed.load() )
			ctx->requestCancel();

		lua_pushvalue( L, 1 );
		return 1;
	}



	static void requestOptions( lua_State *L, auto ctx ) {
		// Parse driver - must be read first so warnings about unsupported options
		// can reference the active driver.
		lua_getfield( L, 2, "driver" );
		if ( lua_isstring( L, -1 ) ) {
			const char *driver = lua_tostring( L, -1 );
			ctx->request.driver = driver;

			if ( ctx->request.driver != "winhttp" && ctx->request.driver != "wininet" ) {
				std::wstring msg = L"[RainJIT:Fetch] Unknown driver \"" +
				                   utf8_to_wstring( ctx->request.driver ) +
				                   L"\" - falling back to \"winhttp\".";
				if ( ctx->rain && ctx->rain->rm )
					RmLog( ctx->rain->rm, LOG_WARNING, msg.c_str() );
				ctx->request.driver = "winhttp";
			}
		}
		lua_pop( L, 1 );

		lua_getfield( L, 2, "httpVersion" );
		if ( lua_isstring( L, -1 ) ) {
			const char *version = lua_tostring( L, -1 );
			ctx->request.HTTPVersion = ( strcmp( version, "1.0" ) == 0 );
		}
		lua_pop( L, 1 );

		// Parse headers
		lua_getfield( L, 2, "headers" );
		if ( lua_istable( L, -1 ) ) {
			lua_pushnil( L );
			while ( lua_next( L, -2 ) != 0 ) {
				const char *key   = lua_tostring( L, -2 );
				const char *value = lua_tostring( L, -1 );
				if ( key && value )
					ctx->request.headers[utf8_to_wstring( key )] = utf8_to_wstring( value );
				lua_pop( L, 1 );
			}
		}
		lua_pop( L, 1 );

		// Parse body
		lua_getfield( L, 2, "body" );
		if ( lua_isstring( L, -1 ) ) {
			size_t      len;
			const char *body = lua_tolstring( L, -1, &len );
			ctx->request.body.assign( body, body + len );
		}
		lua_pop( L, 1 );

		// Parse timeout
		lua_getfield( L, 2, "timeout" );
		if ( lua_isnumber( L, -1 ) ) {
			int timeout = static_cast<int>( lua_tointeger( L, -1 ) );
			if ( timeout < 1000 ) {
				std::wstring msg = L"[RainJIT:Fetch] timeout = " + std::to_wstring( timeout ) +
				                   L" ms is below the minimum - using 1000 ms.";
				if ( ctx->rain && ctx->rain->rm )
					RmLog( ctx->rain->rm, LOG_WARNING, msg.c_str() );

				timeout = 1000;
			}
			ctx->request.timeout = timeout;
		}
		lua_pop( L, 1 );

		// Parse phase-specific timeouts - warn if driver is wininet
		auto checkPhaseTimeout = [&]( const char *field, int &target ) {
			lua_getfield( L, 2, field );
			if ( lua_isnumber( L, -1 ) ) {
				target = static_cast<int>( lua_tointeger( L, -1 ) );

				if ( ctx->request.driver == "wininet" && target > 0 ) {
					std::wstring msg = L"[RainJIT:Fetch] driver=\"wininet\" - \"" +
					                   utf8_to_wstring( field ) +
					                   L"\" is not supported by WinINet and will be ignored.";
					if ( ctx->rain && ctx->rain->rm )
						RmLog( ctx->rain->rm, LOG_WARNING, msg.c_str() );
				}
			}
			lua_pop( L, 1 );
		};

		checkPhaseTimeout( "dnsTimeout",     ctx->request.dnsTimeout     );
		checkPhaseTimeout( "connectTimeout",  ctx->request.connectTimeout );
		checkPhaseTimeout( "sendTimeout",     ctx->request.sendTimeout    );
		checkPhaseTimeout( "receiveTimeout",  ctx->request.receiveTimeout );

		// Parse followRedirects
		ctx->request.followRedirects = true;
		lua_getfield( L, 2, "followRedirects" );
		if ( lua_isboolean( L, -1 ) )
			ctx->request.followRedirects = lua_toboolean( L, -1 ) != 0;
		lua_pop( L, 1 );

		// Parse onlyASCII: true = percent-encode spaces / non-ASCII characters of the URL;
		// false (default) = send the URL exactly as written.
		ctx->request.onlyASCII = false;
		lua_getfield( L, 2, "onlyASCII" );
		if ( lua_isboolean( L, -1 ) )
			ctx->request.onlyASCII = lua_toboolean( L, -1 ) != 0;
		lua_pop( L, 1 );
	}






	int fetch_async( lua_State *L ) {
		auto rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );
		const char *url = luaL_checkstring( L, 1 );

		auto ctx = std::make_shared<core::FetchContext>( rain );
		ctx->request.url = utf8_to_wstring( url );
		ctx->request.method = L"GET";
		ctx->hNotifyWindow = GetNotifyWindow( rain );
		ctx->rainValid = true;


		if ( lua_istable( L, 2 ) ) {
			requestOptions( L, ctx );
			// Parse method
			lua_getfield( L, 2, "method" );
			if ( lua_isstring( L, -1 ) ) {
				const char *method = lua_tostring( L, -1 );
				ctx->request.method = utf8_to_wstring( string::ToUpperCase( method ) );
			}
			lua_pop( L, 1 );
		}

		// Register context (the notification window finds it by this id)
		ctx->refSelf = core::ContextRegistry::instance().registerContext( ctx );

		// The request object. It is only referenced from the registry while it runs
		// (see fetch_send), so an unused request is garbage collected.
		void *mem = lua_newuserdata( L, sizeof( RequestUD ) );
		new ( mem ) RequestUD{ ctx };

		luaL_getmetatable( L, REQUEST_MT );
		lua_setmetatable( L, -2 );

		return 1;
	}



	int fetch_sync( lua_State *L ) {
		auto rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );
		const char *method = lua_tostring( L, lua_upvalueindex( 2 ) );
		const char *url = luaL_checkstring( L, 1 );

		auto ctx = std::make_shared<core::FetchContext>( rain );
		ctx->request.url    = utf8_to_wstring( url );
		ctx->request.method = utf8_to_wstring( string::ToUpperCase( method ) );

		if ( lua_istable( L, 2 ) )
			requestOptions( L, ctx );

		// Register so CleanupContexts() can mark rainValid=false if Finalize()
		// is called while the request is in flight.
		ctx->refSelf = core::ContextRegistry::instance().registerContext( ctx );

		// Execute directly on the current thread - no busy-wait, no worker thread.
		if ( ctx->request.driver == "wininet" )
			wininet::ExecuteFetchThread( ctx );
		else
			http::ExecuteFetchThread( ctx );

		core::ContextRegistry::instance().removeContext( ctx->refSelf );

		core::FetchResponse response;
		{
			std::lock_guard<std::mutex> lock( ctx->mutex );
			response = std::move( ctx->response );
		}

		PushResponse( L, std::move( response ) );
		return 1;
	}



	int fetch_default( lua_State *L ) {
		auto rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );
		const char *url = luaL_checkstring( L, 2 );

		// Call synchronous fetch.get
		lua_pushlightuserdata( L, rain );
		lua_pushstring( L, "get" );
		lua_pushcclosure( L, fetch_sync, 2 );
		lua_pushvalue( L, 2 );


		if ( lua_istable( L, 3 ) ) {
			lua_pushvalue( L, 3 );
			lua_call( L, 2, 1 );
		}

		else
			lua_call( L, 1, 1 );

		return 1;
	}



	// Creates the metatables of the request / response objects and of the
	// headers/cookies tables once per Lua state (they live in its registry).
	static void RegisterMetatables( lua_State *L, Rain *rain ) {
		// response:json() calls json::Decode directly, so the json.array / json.object
		// marker metatables must exist even if the script never requires "json".
		// Same registry names as json.cpp; luaL_newmetatable is a no-op when they exist.
		luaL_newmetatable( L, "json.array" );
		lua_pop( L, 1 );
		luaL_newmetatable( L, "json.object" );
		lua_pop( L, 1 );

		// --- Request object: methods send / hasCompleted / cancel
		if ( luaL_newmetatable( L, REQUEST_MT ) ) {
			// clang-format off
			static const struct luaL_Reg methods[] = {
				{ "send",         fetch_send         },
				{ "hasCompleted", fetch_hasCompleted },
				{ "cancel",       fetch_cancel       },
				{ NULL, NULL }
			};
			// clang-format on

			lua_newtable( L );
			for ( const luaL_Reg *m = methods; m->name; m++ ) {
				lua_pushcfunction( L, m->func );
				lua_setfield( L, -2, m->name );
			}
			lua_setfield( L, -2, "__index" );

			lua_pushcfunction( L, request_gc );
			lua_setfield( L, -2, "__gc" );
		}
		lua_pop( L, 1 );

		// --- Response object: ok / status / error are computed, the rest are methods
		if ( luaL_newmetatable( L, RESPONSE_MT ) ) {
			lua_newtable( L ); // method table (upvalue of __index)

			lua_pushcfunction( L, response_text );
			lua_setfield( L, -2, "text" );

			lua_pushcfunction( L, response_text );
			lua_setfield( L, -2, "bytes" );

			lua_pushcfunction( L, response_headers );
			lua_setfield( L, -2, "headers" );

			lua_pushcfunction( L, response_cookies );
			lua_setfield( L, -2, "cookies" );

			// :xml() - xml::Parse (parser of the xml module), no require
			lua_pushlightuserdata( L, rain );
			lua_pushcclosure( L, response_xml, 1 );
			lua_setfield( L, -2, "xml" );

			// :html() - html::Parse (parser of the html module), no require
			lua_pushlightuserdata( L, rain );
			lua_pushcclosure( L, response_html, 1 );
			lua_setfield( L, -2, "html" );

			// :json() - decoded directly by the json library of the plugin
			lua_pushlightuserdata( L, rain );
			lua_pushcclosure( L, response_json, 1 );
			lua_setfield( L, -2, "json" );

			lua_pushlightuserdata( L, rain );
			lua_pushcclosure( L, response_save, 1 );
			lua_setfield( L, -2, "save" );

			lua_pushcclosure( L, response_index, 1 ); // consumes the method table
			lua_setfield( L, -2, "__index" );

			lua_pushcfunction( L, response_newindex );
			lua_setfield( L, -2, "__newindex" );

			lua_pushcfunction( L, response_gc );
			lua_setfield( L, -2, "__gc" );
		}
		lua_pop( L, 1 );

		// --- Headers / cookies tables (case-insensitive lookups)
		if ( luaL_newmetatable( L, HEADERS_MT ) ) {
			lua_pushcfunction( L, headers_index );
			lua_setfield( L, -2, "__index" );
		}
		lua_pop( L, 1 );
	}



	int luaopen_fetch( lua_State *L ) {
		auto rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );

		RegisterMetatables( L, rain );

		lua_newtable( L );

		// Async method
		lua_pushlightuserdata( L, rain );
		lua_pushcclosure( L, fetch_async, 1 );
		lua_setfield( L, -2, "async" );

		// Synchronous methods
		const char *sync_methods[] = { "get", "post", "put", "patch", "delete", "head", "options" };
		for ( const char *method : sync_methods ) {
			lua_pushlightuserdata( L, rain );
			lua_pushstring( L, method );
			lua_pushcclosure( L, fetch_sync, 2 );
			lua_setfield( L, -2, method );
		}

		// __call metamethod (so that fetch(url) behaves like fetch.get)
		lua_newtable( L );
		lua_pushlightuserdata( L, rain );

		lua_pushcclosure( L, fetch_default, 1 );
		lua_setfield( L, -2, "__call" );

		lua_setmetatable( L, -2 );

		return 1;
	}



	void RegisterModule( lua_State *L, Rain *rain ) {
		// Create the notification window for this measure
		CreateNotifyWindow( rain );

		lua_getglobal( L, "package" );
		lua_getfield( L, -1, "preload" );

		lua_pushlightuserdata( L, rain );
		lua_pushcclosure( L, luaopen_fetch, 1 );
		lua_setfield( L, -2, "fetch" );

		lua_pop( L, 2 );
	}

} // namespace lua



namespace fetch {
	void CleanupAutoDispatch( Rain *rain ) {
		lua::DestroyNotifyWindow( rain );
	}

} // namespace fetch
