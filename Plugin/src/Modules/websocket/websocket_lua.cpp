/**
 * @file websocket_lua.cpp
 * @brief Lua bindings implementation for websocket module
 * @license GPL v2.0 License
 *
 * The server object (websocket.server), the connection object (handed to the server
 * callbacks) and the client object (websocket.connect, handed to the client callbacks)
 * are full userdata, each with one metatable per Lua state.
 *
 * A server or a client runs on its own thread (see websocket_network.cpp). Everything it reports is
 * queued as an Event and announced with WM_WEBSOCKET_EVENT; the callbacks then
 * run here, on the Rainmeter thread, so Lua is never touched by two threads.
 * Everything Lua asks of a connection is queued as a Command for the server thread.
 */

#include <lua.hpp>
#include <cctype>
#include <chrono>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "websocket_core.hpp"
#include "websocket_network.hpp"
#include "websocket_lua.hpp"

#include <Includes/rain.hpp>
#include <RainmeterAPI.hpp>
#include <Utils/strings.hpp>

// Global map associating each Rain* with the HWND of its notification window
static std::unordered_map<Rain *, HWND> g_notifyWindows;
static std::mutex g_notifyMutex;

// The window class is registered when the first window is created and removed
// again when the last one is destroyed, so nothing stays registered after the
// plugin DLL is unloaded.
static bool g_classRegistered = false;
static const wchar_t *const NOTIFY_CLASS_NAME = L"RainJIT_WebSocketNotifyWindow";



namespace websocket::lua {

	// Names of the metatables stored in the Lua registry
	static const char *const SERVER_MT = "RainJIT.WebSocketServer";
	static const char *const CONNECTION_MT = "RainJIT.WebSocketConnection";
	static const char *const CLIENT_MT = "RainJIT.WebSocketClient";

	// Event names, indexed by core::Event::OPEN ... core::Event::RECONNECTING
	static const char *const EVENT_NAMES[core::Event::COUNT] = { "open", "message", "close", "drain", "ping", "pong", "error", "reconnecting" };

	/// Events handed to Lua per window message. The rest is announced with a new message,
	/// so a flood of messages never freezes the Rainmeter thread.
	static constexpr size_t MAX_EVENTS_PER_DISPATCH = 500;

	/// How long listen() waits for the server thread to report success or failure.
	static constexpr int START_TIMEOUT_MS = 10000;

	/// Userdata payload of a server object.
	struct ServerUD {
		std::shared_ptr<core::Context> ctx;
	};

	/// Userdata payload of a client object.
	struct ClientUD {
		std::shared_ptr<core::Context> ctx;
	};

	/// Userdata payload of a connection object.
	struct ConnectionUD {
		std::shared_ptr<core::Connection> conn;
		std::weak_ptr<core::Context> ctx;
		int refData = LUA_NOREF; // value kept by setData()
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
	LRESULT CALLBACK WebSocketNotifyWndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam ) {
		if ( msg == WM_WEBSOCKET_EVENT ) {
			int ctxId = static_cast<int>( wParam );
			auto ctx = core::ContextRegistry::instance().getContext( ctxId );

			if ( !ctx )
				return 0;

			if ( !ctx->stopped.load() && ctx->rain && ctx->rain->L && ctx->refSelfLua != LUA_NOREF ) {
				lua_State *L = ctx->rain->L;

				// Whatever happens below, the Lua stack ends exactly as it was found.
				const int top = lua_gettop( L );

				lua_pushcfunction( L, ctx->config.isClient ? client_dispatch : server_dispatch );
				lua_rawgeti( L, LUA_REGISTRYINDEX, ctx->refSelfLua ); // server object

				if ( lua_pcall( L, 1, 0, 0 ) != LUA_OK ) {
					const char *err = lua_tostring( L, -1 );
					if ( err )
						LogMsg( ctx->rain, LOG_ERROR, utf8_to_wstring( err ) );
				}

				lua_settop( L, top );
			}

			else {
				// Nobody is going to take these events: free them.
				ctx->notifyPending.store( false );

				std::lock_guard<std::mutex> lock( ctx->eventMutex );
				ctx->events.clear();
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
			if ( GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>( &WebSocketNotifyWndProc ), &module ) )
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
			wc.lpfnWndProc = WebSocketNotifyWndProc;
			wc.hInstance = ModuleInstance();
			wc.lpszClassName = NOTIFY_CLASS_NAME;

			if ( !RegisterClassEx( &wc ) ) {
				DWORD err = GetLastError();
				if ( err != ERROR_CLASS_ALREADY_EXISTS ) {
					LogMsg( rain, LOG_ERROR, L"Failed to register websocket notify window class" );
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
			LogMsg( rain, LOG_ERROR, L"Failed to create websocket notify window" );
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

	// Raises a Lua error unless argument `idx` is a server object.
	static ServerUD *CheckServer( lua_State *L, int idx ) {
		return static_cast<ServerUD *>( luaL_checkudata( L, idx, SERVER_MT ) );
	}

	// Raises a Lua error unless argument `idx` is a connection object.
	static ConnectionUD *CheckConnection( lua_State *L, int idx ) {
		return static_cast<ConnectionUD *>( luaL_checkudata( L, idx, CONNECTION_MT ) );
	}

	// Raises a Lua error unless argument `idx` is a client object.
	static ClientUD *CheckClient( lua_State *L, int idx ) {
		return static_cast<ClientUD *>( luaL_checkudata( L, idx, CLIENT_MT ) );
	}

	std::shared_ptr<core::Context> GetContext( lua_State *L, int idx ) {
		return CheckServer( L, idx )->ctx;
	}



	static std::string ToLower( std::string text ) {
		for ( char &c : text )
			c = static_cast<char>( tolower( static_cast<unsigned char>( c ) ) );

		return text;
	}



	// Returns the index of an event name (case-insensitive), or -1.
	static int EventIndex( const char *name ) {
		const std::string lower = ToLower( name ? name : "" );

		for ( int i = 0; i < core::Event::COUNT; i++ ) {
			if ( lower == EVENT_NAMES[i] )
				return i;
		}

		return -1;
	}



	// ------------------------------------------------------------------
	// Options
	// ------------------------------------------------------------------

	// Optional string field. Fails (with `error` set) when the field has another type.
	static bool OptString( lua_State *L, int tbl, const char *name, std::string &out, bool &present, std::string &error ) {
		present = false;

		lua_getfield( L, tbl, name );
		if ( lua_isnil( L, -1 ) ) {
			lua_pop( L, 1 );
			return true;
		}

		if ( lua_type( L, -1 ) != LUA_TSTRING ) {
			lua_pop( L, 1 );
			error = std::string( name ) + " must be a string";
			return false;
		}

		size_t length = 0;
		const char *text = lua_tolstring( L, -1, &length );
		out.assign( text, length );
		present = true;

		lua_pop( L, 1 );
		return true;
	}



	// Optional integer field within [minValue, maxValue].
	static bool OptInt( lua_State *L, int tbl, const char *name, long long minValue, long long maxValue, long long &out, bool &present, std::string &error ) {
		present = false;

		lua_getfield( L, tbl, name );
		if ( lua_isnil( L, -1 ) ) {
			lua_pop( L, 1 );
			return true;
		}

		if ( lua_type( L, -1 ) != LUA_TNUMBER ) {
			lua_pop( L, 1 );
			error = std::string( name ) + " must be a number";
			return false;
		}

		const double value = lua_tonumber( L, -1 );
		lua_pop( L, 1 );

		if ( value < static_cast<double>( minValue ) || value > static_cast<double>( maxValue ) ) {
			error = std::string( name ) + " must be between " + std::to_string( minValue ) + " and " + std::to_string( maxValue );
			return false;
		}

		if ( value != static_cast<double>( static_cast<long long>( value ) ) ) {
			error = std::string( name ) + " must be an integer";
			return false;
		}

		out = static_cast<long long>( value );
		present = true;
		return true;
	}



	// Optional boolean field. `out` keeps its value when the field is absent.
	static bool OptBool( lua_State *L, int tbl, const char *name, bool &out, std::string &error ) {
		lua_getfield( L, tbl, name );

		if ( lua_isnil( L, -1 ) ) {
			lua_pop( L, 1 );
			return true;
		}

		if ( !lua_isboolean( L, -1 ) ) {
			lua_pop( L, 1 );
			error = std::string( name ) + " must be a boolean";
			return false;
		}

		out = lua_toboolean( L, -1 ) != 0;
		lua_pop( L, 1 );
		return true;
	}



	// Registers (or, with a non-function, removes) the handler of an event.
	// `idx` is an index of the value on the stack.
	static void SetHandler( lua_State *L, core::Context &ctx, int type, int idx ) {
		if ( ctx.refHandlers[type] != LUA_NOREF ) {
			luaL_unref( L, LUA_REGISTRYINDEX, ctx.refHandlers[type] );
			ctx.refHandlers[type] = LUA_NOREF;
		}

		if ( lua_isfunction( L, idx ) ) {
			lua_pushvalue( L, idx );
			ctx.refHandlers[type] = luaL_ref( L, LUA_REGISTRYINDEX );
		}

		// open / close / error are always reported: they keep Lua's view of the connections in sync.
		const bool always = type == core::Event::OPEN || type == core::Event::CLOSE || type == core::Event::SERVER_ERROR;
		ctx.wanted[type].store( always || ctx.refHandlers[type] != LUA_NOREF );
	}



	static bool IsTokenChar( unsigned char c ) {
		return c != 0 && ( isalnum( c ) || strchr( "!#$%&'*+-.^_`|~", c ) != nullptr );
	}



	// A credential has to survive the place it travels in.
	static bool ValidateSecret( int type, const std::string &secret, std::string &error ) {
		if ( secret.empty() ) {
			error = "auth secret must not be empty";
			return false;
		}

		for ( unsigned char c : secret ) {
			if ( type == core::AuthConfig::AUTH_PROTOCOL && !IsTokenChar( c ) ) {
				error = "auth secret has characters that are not valid in Sec-WebSocket-Protocol (use letters, digits and - . _ ~ + ! # $ % & ' * ^ ` |)";
				return false;
			}

			if ( c < 0x20 || c == 0x7F ) {
				error = "auth secret must not contain control characters";
				return false;
			}
		}

		return true;
	}



	// auth = { type = "header" | "query" | "protocol", name = "...", secret = "..." | secrets = { "...", ... } }
	static bool ParseAuth( lua_State *L, int tbl, core::AuthConfig &auth, std::string &error ) {
		bool present = false;
		std::string type;

		if ( !OptString( L, tbl, "type", type, present, error ) )
			return false;

		if ( !present ) {
			error = "auth.type is required (\"header\", \"query\" or \"protocol\")";
			return false;
		}

		type = ToLower( type );

		if ( type == "header" )
			auth.type = core::AuthConfig::AUTH_HEADER;
		else if ( type == "query" )
			auth.type = core::AuthConfig::AUTH_QUERY;
		else if ( type == "protocol" )
			auth.type = core::AuthConfig::AUTH_PROTOCOL;
		else {
			error = "auth.type must be \"header\", \"query\" or \"protocol\"";
			return false;
		}

		if ( auth.type != core::AuthConfig::AUTH_PROTOCOL ) {
			if ( !OptString( L, tbl, "name", auth.name, present, error ) )
				return false;

			if ( !present || auth.name.empty() ) {
				error = "auth.name is required (header name or query parameter)";
				return false;
			}

			// Header names are matched in lowercase; the query parameter is matched case-insensitively.
			auth.name = ToLower( auth.name );

			for ( unsigned char c : auth.name ) {
				if ( !isalnum( c ) && c != '-' && c != '_' && c != '.' ) {
					error = "auth.name may only contain letters, digits, '-', '_' and '.'";
					return false;
				}
			}
		}

		std::string secret;
		if ( !OptString( L, tbl, "secret", secret, present, error ) )
			return false;

		if ( present )
			auth.secrets.push_back( secret );

		lua_getfield( L, tbl, "secrets" );
		if ( !lua_isnil( L, -1 ) ) {
			if ( !lua_istable( L, -1 ) ) {
				lua_pop( L, 1 );
				error = "auth.secrets must be a list of strings";
				return false;
			}

			const int count = static_cast<int>( lua_objlen( L, -1 ) );
			for ( int i = 1; i <= count; i++ ) {
				lua_rawgeti( L, -1, i );

				if ( lua_type( L, -1 ) != LUA_TSTRING ) {
					lua_pop( L, 2 );
					error = "auth.secrets must be a list of strings";
					return false;
				}

				size_t length = 0;
				const char *text = lua_tolstring( L, -1, &length );
				auth.secrets.emplace_back( text, length );
				lua_pop( L, 1 );
			}
		}
		lua_pop( L, 1 );

		if ( auth.secrets.empty() ) {
			error = "auth needs a secret (or a list in secrets)";
			return false;
		}

		for ( const std::string &entry : auth.secrets ) {
			if ( !ValidateSecret( auth.type, entry, error ) )
				return false;
		}

		return true;
	}



	static bool IsLoopbackHost( const std::string &host ) {
		return host == "127.0.0.1" || host == "::1" || host == "[::1]" || host.compare( 0, 4, "127." ) == 0;
	}



	// Options that servers and clients share: limits, keep-alive and callbacks.
	static bool ParseCommonOptions( lua_State *L, int tbl, Rain *rain, core::Context &ctx, std::string &error ) {
		core::Config &cfg = ctx.config;

		bool present = false;
		long long number = 0;

		if ( !OptInt( L, tbl, "maxPayload", 1, 2147483647, number, present, error ) )
			return false;
		if ( present )
			cfg.maxPayload = static_cast<unsigned int>( number );

		if ( !OptInt( L, tbl, "maxBackpressure", 0, 2147483647, number, present, error ) )
			return false;
		if ( present )
			cfg.maxBackpressure = static_cast<unsigned int>( number );

		if ( !OptInt( L, tbl, "idleTimeout", 0, 960, number, present, error ) )
			return false;
		if ( present )
			cfg.idleTimeout = static_cast<unsigned short>( number );

		if ( !OptBool( L, tbl, "sendPings", cfg.sendPings, error ) )
			return false;

		// Not supported: say so instead of silently doing something else.
		lua_getfield( L, tbl, "tls" );
		const bool hasTls = !lua_isnil( L, -1 );
		lua_pop( L, 1 );

		if ( hasTls ) {
			error = "tls (wss) is not supported: this module speaks ws only";
			return false;
		}

		bool compression = false;
		if ( !OptBool( L, tbl, "compression", compression, error ) )
			return false;

		if ( compression )
			LogMsg( rain, LOG_WARNING, L"[RainJIT:WebSocket] compression is not supported and is ignored" );

		// Callbacks ("reconnecting" belongs to clients only)
		for ( int i = 0; i < core::Event::COUNT; i++ ) {
			if ( i == core::Event::RECONNECTING && !cfg.isClient )
				continue;

			lua_getfield( L, tbl, EVENT_NAMES[i] );

			if ( !lua_isnil( L, -1 ) ) {
				if ( !lua_isfunction( L, -1 ) ) {
					lua_pop( L, 1 );
					error = std::string( EVENT_NAMES[i] ) + " must be a function";
					return false;
				}

				SetHandler( L, ctx, i, lua_gettop( L ) );
			}

			lua_pop( L, 1 );
		}

		return true;
	}



	static bool ParseOptions( lua_State *L, int tbl, Rain *rain, core::Context &ctx, std::string &error ) {
		core::Config &cfg = ctx.config;

		bool present = false;
		long long number = 0;
		std::string text;

		// Listening address. "localhost" is mapped to the IPv4 loopback: it may resolve to IPv6.
		if ( !OptString( L, tbl, "host", text, present, error ) )
			return false;

		if ( present ) {
			if ( text.empty() ) {
				error = "host must not be empty";
				return false;
			}

			cfg.host = ToLower( text ) == "localhost" ? "127.0.0.1" : text;
		}

		if ( !OptInt( L, tbl, "port", 0, 65535, number, present, error ) )
			return false;
		if ( present )
			cfg.port = static_cast<int>( number );

		if ( !OptInt( L, tbl, "portTries", 1, 100, number, present, error ) )
			return false;
		if ( present )
			cfg.portTries = static_cast<int>( number );

		if ( !OptString( L, tbl, "path", text, present, error ) )
			return false;

		if ( present ) {
			if ( text.empty() || text[0] != '/' ) {
				error = "path must start with '/'";
				return false;
			}

			cfg.path = text;
		}

		if ( !OptInt( L, tbl, "maxConnections", 0, 1000000, number, present, error ) )
			return false;
		if ( present )
			cfg.maxConnections = static_cast<int>( number );

		// Reconnecting makes no sense for a server: it only accepts connections.
		lua_getfield( L, tbl, "reconnect" );
		const bool hasReconnect = !lua_isnil( L, -1 );
		lua_pop( L, 1 );

		if ( hasReconnect ) {
			error = "reconnect is only available on clients (websocket.connect)";
			return false;
		}

		lua_getfield( L, tbl, "auth" );
		if ( !lua_isnil( L, -1 ) ) {
			if ( !lua_istable( L, -1 ) ) {
				lua_pop( L, 1 );
				error = "auth must be a table { type = \"header\", name = \"...\", secret = \"...\" }";
				return false;
			}

			if ( !ParseAuth( L, lua_gettop( L ), cfg.auth, error ) ) {
				lua_pop( L, 1 );
				return false;
			}
		}
		lua_pop( L, 1 );

		return ParseCommonOptions( L, tbl, rain, ctx, error );
	}



	// "ws://host[:port][/path][?query]" -> host, port, request target and Host header.
	static bool ParseWsUrl( const std::string &url, core::Config &cfg, std::string &error ) {
		const std::string lower = ToLower( url.substr( 0, 6 ) );

		if ( lower.compare( 0, 6, "wss://" ) == 0 ) {
			error = "wss:// is not supported: this module connects with ws:// only";
			return false;
		}

		if ( lower.compare( 0, 5, "ws://" ) != 0 ) {
			error = "url must start with ws://";
			return false;
		}

		for ( unsigned char c : url ) {
			if ( c <= 0x20 || c == 0x7F ) {
				error = "url must not contain spaces or control characters";
				return false;
			}
		}

		if ( url.find( '#' ) != std::string::npos ) {
			error = "url must not contain a fragment (#)";
			return false;
		}

		const size_t authorityStart = 5;
		const size_t authorityEnd = url.find_first_of( "/?", authorityStart );
		const std::string authority = url.substr( authorityStart, authorityEnd == std::string::npos ? std::string::npos : authorityEnd - authorityStart );

		if ( authority.empty() ) {
			error = "url has no host";
			return false;
		}

		if ( authority.find( '@' ) != std::string::npos ) {
			error = "url must not contain credentials (user:password@): use headers instead";
			return false;
		}

		std::string host;
		std::string portText;

		if ( authority[0] == '[' ) { // [::1]:3000
			const size_t close = authority.find( ']' );
			if ( close == std::string::npos ) {
				error = "url has an invalid IPv6 address";
				return false;
			}

			host = authority.substr( 1, close - 1 );

			if ( close + 1 < authority.size() ) {
				if ( authority[close + 1] != ':' ) {
					error = "url has an invalid port";
					return false;
				}

				portText = authority.substr( close + 2 );
			}
		}

		else {
			const size_t colon = authority.rfind( ':' );

			if ( colon == std::string::npos )
				host = authority;
			else {
				host = authority.substr( 0, colon );
				portText = authority.substr( colon + 1 );
			}
		}

		if ( host.empty() ) {
			error = "url has no host";
			return false;
		}

		int port = 80;

		if ( !portText.empty() ) {
			port = 0;

			for ( unsigned char c : portText ) {
				if ( !isdigit( c ) || port > 65535 ) {
					error = "url has an invalid port";
					return false;
				}

				port = port * 10 + ( c - '0' );
			}

			if ( port < 1 || port > 65535 ) {
				error = "url has an invalid port";
				return false;
			}
		}

		std::string target = authorityEnd == std::string::npos ? std::string( "/" ) : url.substr( authorityEnd );
		if ( target[0] == '?' )
			target = "/" + target;

		cfg.host = host;
		cfg.port = port;
		cfg.client.url = url;
		cfg.client.target = target;

		const bool v6 = host.find( ':' ) != std::string::npos;
		cfg.client.hostHeader = ( v6 ? "[" + host + "]" : host ) + ( port == 80 ? std::string() : ":" + std::to_string( port ) );

		return true;
	}



	// Request headers the handshake sets itself.
	static bool IsReservedHeader( const std::string &lowerName ) {
		return lowerName == "host" || lowerName == "upgrade" || lowerName == "connection" || lowerName == "content-length" || lowerName.compare( 0, 14, "sec-websocket-" ) == 0;
	}



	// headers = { ["Name"] = "value", ... }
	static bool ParseClientHeaders( lua_State *L, int tbl, core::ClientConfig &client, std::string &error ) {
		if ( !lua_istable( L, tbl ) ) {
			error = "headers must be a table { [\"Name\"] = \"value\" }";
			return false;
		}

		lua_pushnil( L );

		while ( lua_next( L, tbl ) != 0 ) {
			if ( lua_type( L, -2 ) != LUA_TSTRING || lua_type( L, -1 ) != LUA_TSTRING ) {
				lua_pop( L, 2 );
				error = "headers must map header names (strings) to values (strings)";
				return false;
			}

			size_t nameLength = 0;
			const char *name = lua_tolstring( L, -2, &nameLength );
			size_t valueLength = 0;
			const char *value = lua_tolstring( L, -1, &valueLength );

			const std::string headerName( name, nameLength );
			const std::string headerValue( value, valueLength );
			lua_pop( L, 1 ); // the value; the key stays for lua_next

			if ( headerName.empty() ) {
				lua_pop( L, 1 );
				error = "header names must not be empty";
				return false;
			}

			for ( unsigned char c : headerName ) {
				if ( !IsTokenChar( c ) ) {
					lua_pop( L, 1 );
					error = "invalid character in header name \"" + headerName + "\"";
					return false;
				}
			}

			if ( IsReservedHeader( ToLower( headerName ) ) ) {
				lua_pop( L, 1 );
				error = "header \"" + headerName + "\" is set by the module and cannot be overridden";
				return false;
			}

			for ( unsigned char c : headerValue ) {
				if ( ( c < 0x20 && c != '\t' ) || c == 0x7F ) {
					lua_pop( L, 1 );
					error = "header \"" + headerName + "\" has a control character in its value";
					return false;
				}
			}

			client.headers.emplace_back( headerName, headerValue );
		}

		return true;
	}



	// protocols = { "a", "b" } or protocol = "a"
	static bool ParseClientProtocols( lua_State *L, int tbl, core::ClientConfig &client, std::string &error ) {
		auto add = [&client, &error]( const char *text, size_t length ) {
			const std::string protocol( text, length );

			if ( protocol.empty() ) {
				error = "protocol names must not be empty";
				return false;
			}

			for ( unsigned char c : protocol ) {
				if ( !IsTokenChar( c ) ) {
					error = "invalid character in protocol \"" + protocol + "\"";
					return false;
				}
			}

			client.protocols.push_back( protocol );
			return true;
		};

		bool present = false;
		std::string single;

		if ( !OptString( L, tbl, "protocol", single, present, error ) )
			return false;

		if ( present && !add( single.data(), single.size() ) )
			return false;

		lua_getfield( L, tbl, "protocols" );

		if ( !lua_isnil( L, -1 ) ) {
			if ( !lua_istable( L, -1 ) ) {
				lua_pop( L, 1 );
				error = "protocols must be a list of strings";
				return false;
			}

			const int count = static_cast<int>( lua_objlen( L, -1 ) );

			for ( int i = 1; i <= count; i++ ) {
				lua_rawgeti( L, -1, i );

				size_t length = 0;
				const char *text = lua_type( L, -1 ) == LUA_TSTRING ? lua_tolstring( L, -1, &length ) : nullptr;

				if ( !text ) {
					lua_pop( L, 2 );
					error = "protocols must be a list of strings";
					return false;
				}

				const bool ok = add( text, length );
				lua_pop( L, 1 );

				if ( !ok ) {
					lua_pop( L, 1 );
					return false;
				}
			}
		}

		lua_pop( L, 1 );
		return true;
	}



	// reconnect = true | false | { delay = ms, maxDelay = ms, maxTries = n }
	static bool ParseReconnect( lua_State *L, int tbl, core::ClientConfig::Reconnect &rc, std::string &error ) {
		lua_getfield( L, tbl, "reconnect" );

		if ( lua_isnil( L, -1 ) ) {
			lua_pop( L, 1 );
			return true;
		}

		if ( lua_isboolean( L, -1 ) ) {
			rc.enabled = lua_toboolean( L, -1 ) != 0;
			lua_pop( L, 1 );
			return true;
		}

		if ( !lua_istable( L, -1 ) ) {
			lua_pop( L, 1 );
			error = "reconnect must be a boolean or a table { delay = 1000, maxDelay = 30000, maxTries = 0 }";
			return false;
		}

		const int sub = lua_gettop( L );
		rc.enabled = true;

		bool present = false;
		long long number = 0;
		bool ok = true;

		if ( ok && ( ok = OptInt( L, sub, "delay", 100, 3600000, number, present, error ) ) && present )
			rc.delay = static_cast<unsigned int>( number );

		if ( ok && ( ok = OptInt( L, sub, "maxDelay", 100, 3600000, number, present, error ) ) && present )
			rc.maxDelay = static_cast<unsigned int>( number );

		if ( ok && ( ok = OptInt( L, sub, "maxTries", 0, 1000000, number, present, error ) ) && present )
			rc.maxTries = static_cast<unsigned int>( number );

		lua_pop( L, 1 );

		if ( !ok )
			return false;

		if ( rc.maxDelay < rc.delay ) {
			error = "reconnect.maxDelay must not be smaller than reconnect.delay";
			return false;
		}

		return true;
	}



	// url, headers, protocol(s), connectTimeout, reconnect + the common options.
	static bool ParseClientOptions( lua_State *L, int tbl, Rain *rain, core::Context &ctx, std::string &error ) {
		core::Config &cfg = ctx.config;

		cfg.isClient = true;
		cfg.idleTimeout = 0; // a client does not drop a quiet server unless asked to

		bool present = false;
		long long number = 0;
		std::string text;

		if ( !OptString( L, tbl, "url", text, present, error ) )
			return false;

		if ( !present ) {
			error = "url is required (ws://host[:port][/path])";
			return false;
		}

		if ( !ParseWsUrl( text, cfg, error ) )
			return false;

		if ( !OptInt( L, tbl, "connectTimeout", 100, 120000, number, present, error ) )
			return false;
		if ( present )
			cfg.client.connectTimeout = static_cast<unsigned int>( number );

		lua_getfield( L, tbl, "headers" );
		if ( !lua_isnil( L, -1 ) ) {
			if ( !ParseClientHeaders( L, lua_gettop( L ), cfg.client, error ) ) {
				lua_pop( L, 1 );
				return false;
			}
		}
		lua_pop( L, 1 );

		if ( !ParseClientProtocols( L, tbl, cfg.client, error ) )
			return false;

		if ( !ParseReconnect( L, tbl, cfg.client.reconnect, error ) )
			return false;

		return ParseCommonOptions( L, tbl, rain, ctx, error );
	}



	// ------------------------------------------------------------------
	// Server object
	// ------------------------------------------------------------------

	// Stops the server (idempotent). Rainmeter thread only.
	static void StopContext( core::Context &ctx ) {
		ctx.stopped.store( true );

		if ( ctx.running.load() || ctx.workerAlive.load() > 0 )
			network::Stop( ctx );

		{
			std::lock_guard<std::mutex> lock( ctx.eventMutex );
			ctx.events.clear();
		}

		ctx.releaseLuaRefs();
	}



	// ws://127.0.0.1:8765. Empty until the server has a port.
	static std::string BuildUrl( core::Context &ctx ) {
		const int port = ctx.boundPort.load();
		if ( port <= 0 )
			return std::string();

		std::string host = ctx.config.host;
		if ( host.find( ':' ) != std::string::npos && host.front() != '[' )
			host = "[" + host + "]";

		std::string url = "ws://";
		url += host + ":" + std::to_string( port );

		const std::string &path = ctx.config.path;
		if ( path != "/" && path.find_first_of( "*:" ) == std::string::npos )
			url += path;

		return url;
	}



	// __gc: the server object is no longer reachable from Lua. A running server is kept
	// alive through ctx->refSelfLua, so here it is idle (or already stopped).
	static int server_gc( lua_State *L ) {
		auto *ud = static_cast<ServerUD *>( lua_touserdata( L, 1 ) );
		if ( !ud )
			return 0;

		if ( ud->ctx ) {
			StopContext( *ud->ctx );
			core::ContextRegistry::instance().removeContext( ud->ctx->refSelf );
		}

		ud->~ServerUD();
		return 0;
	}



	// Starts the thread of a server or a client and waits (briefly) until it reports
	// success or failure. `selfIdx` is the stack index of its Lua object, which is kept
	// alive in the registry while the thread runs. After a failed start everything is
	// shut down again (single-use).
	static bool StartContext( lua_State *L, int selfIdx, const std::shared_ptr<core::Context> &ctx, std::string &error ) {
		if ( !ctx->rainValid.load() || !ctx->rain || !ctx->rain->L ) {
			error = "The skin is being unloaded";
			return false;
		}

		if ( !ctx->hNotifyWindow ) {
			error = "The notification window is not available";
			return false;
		}

		if ( ctx->workerAlive.load() > 0 ) {
			error = "It is already starting";
			return false;
		}

		{
			std::lock_guard<std::mutex> lock( ctx->startMutex );
			ctx->startState = core::Context::START_PENDING;
			ctx->startError.clear();
		}

		// Keep the object alive while it runs - the script may drop it right away,
		// e.g. websocket.server( options ):listen().
		if ( ctx->refSelfLua == LUA_NOREF ) {
			lua_pushvalue( L, selfIdx );
			ctx->refSelfLua = luaL_ref( L, LUA_REGISTRYINDEX );
		}

		// workerAlive is decremented as the very last action of the thread, so cleanup
		// can wait for it before the DLL goes away.
		ctx->workerAlive.fetch_add( 1 );

		try {
			std::shared_ptr<core::Context> worker = ctx;
			std::thread( [worker]() {
				network::Run( worker );
				worker->workerAlive.fetch_sub( 1 );
			} ).detach();
		}

		catch ( const std::exception &e ) {
			ctx->workerAlive.fetch_sub( 1 );
			StopContext( *ctx );

			error = "Failed to start the thread: " + std::string( e.what() );
			return false;
		}

		{
			std::unique_lock<std::mutex> lock( ctx->startMutex );

			const bool answered = ctx->startCv.wait_for( lock, std::chrono::milliseconds( START_TIMEOUT_MS ), [&ctx]() {
				return ctx->startState != core::Context::START_PENDING;
			} );

			if ( !answered )
				error = "It did not start in time";
			else if ( ctx->startState != core::Context::START_OK )
				error = ctx->startError.empty() ? std::string( "It could not start" ) : ctx->startError;
		}

		if ( !error.empty() ) {
			StopContext( *ctx );
			return false;
		}

		return true;
	}



	// server:listen() -> server | nil, err
	//
	// Starts the server thread and waits (briefly) until it is listening or has failed.
	int server_listen( lua_State *L ) {
		ServerUD *ud = CheckServer( L, 1 );
		std::shared_ptr<core::Context> ctx = ud->ctx;

		// Already listening: nothing to do.
		if ( ctx->running.load() ) {
			lua_pushvalue( L, 1 );
			return 1;
		}

		std::string error;

		if ( ctx->stopped.load() )
			error = "The server was stopped. Create a new one with websocket.server()";
		else
			StartContext( L, 1, ctx, error );

		if ( !error.empty() ) {
			LogMsg( ctx->rain, LOG_ERROR, L"[RainJIT:WebSocket] " + utf8_to_wstring( error ) );

			lua_pushnil( L );
			lua_pushlstring( L, error.data(), error.size() );
			return 2;
		}

		LogMsg( ctx->rain, LOG_DEBUG, L"[RainJIT:WebSocket] Listening on " + utf8_to_wstring( BuildUrl( *ctx ) ) );

		lua_pushvalue( L, 1 );
		return 1;
	}



	// server:stop() -> server
	// Closes every connection and the port. Pending events are discarded.
	int server_stop( lua_State *L ) {
		ServerUD *ud = CheckServer( L, 1 );

		StopContext( *ud->ctx );

		lua_pushvalue( L, 1 );
		return 1;
	}



	// server:on( event, callback ) -> server
	// A nil callback removes the handler.
	int server_on( lua_State *L ) {
		ServerUD *ud = CheckServer( L, 1 );
		const int type = EventIndex( luaL_checkstring( L, 2 ) );

		if ( type < 0 || type == core::Event::RECONNECTING )
			return luaL_argerror( L, 2, "unknown event (open, message, close, drain, ping, pong, error)" );

		if ( !lua_isnoneornil( L, 3 ) && !lua_isfunction( L, 3 ) )
			return luaL_argerror( L, 3, "function expected" );

		if ( !ud->ctx->stopped.load() )
			SetHandler( L, *ud->ctx, type, 3 );

		lua_pushvalue( L, 1 );
		return 1;
	}



	// server:publish( topic, data [, binary] ) -> boolean
	// Sends to every connection subscribed to the topic.
	int server_publish( lua_State *L ) {
		ServerUD *ud = CheckServer( L, 1 );

		size_t topicLength = 0;
		const char *topic = luaL_checklstring( L, 2, &topicLength );
		size_t dataLength = 0;
		const char *data = luaL_checklstring( L, 3, &dataLength );
		const bool binary = lua_toboolean( L, 4 ) != 0;

		core::Context &ctx = *ud->ctx;

		if ( ctx.stopped.load() || !ctx.running.load() ) {
			lua_pushboolean( L, 0 );
			return 1;
		}

		core::Command cmd;
		cmd.type = core::Command::PUBLISH;
		cmd.connId = 0;
		cmd.topic.assign( topic, topicLength );
		cmd.data.assign( data, dataLength );
		cmd.binary = binary;

		network::Post( ctx, std::move( cmd ) );

		lua_pushboolean( L, 1 );
		return 1;
	}



	// server:broadcast( data [, binary] ) -> boolean
	// Sends to every open connection.
	int server_broadcast( lua_State *L ) {
		ServerUD *ud = CheckServer( L, 1 );

		size_t dataLength = 0;
		const char *data = luaL_checklstring( L, 2, &dataLength );
		const bool binary = lua_toboolean( L, 3 ) != 0;

		core::Context &ctx = *ud->ctx;

		if ( ctx.stopped.load() || !ctx.running.load() ) {
			lua_pushboolean( L, 0 );
			return 1;
		}

		core::Command cmd;
		cmd.type = core::Command::BROADCAST;
		cmd.data.assign( data, dataLength );
		cmd.binary = binary;

		network::Post( ctx, std::move( cmd ) );

		lua_pushboolean( L, 1 );
		return 1;
	}



	// server:connection( id ) -> connection | nil
	int server_connection( lua_State *L ) {
		ServerUD *ud = CheckServer( L, 1 );
		const double id = luaL_checknumber( L, 2 );

		if ( id >= 0 ) {
			auto it = ud->ctx->connRefs.find( static_cast<uint64_t>( id ) );

			if ( it != ud->ctx->connRefs.end() ) {
				lua_rawgeti( L, LUA_REGISTRYINDEX, it->second );
				return 1;
			}
		}

		lua_pushnil( L );
		return 1;
	}



	// server:host() -> string
	int server_host( lua_State *L ) {
		ServerUD *ud = CheckServer( L, 1 );

		lua_pushlstring( L, ud->ctx->config.host.data(), ud->ctx->config.host.size() );
		return 1;
	}



	// server:port() -> number | nil (the port actually in use; nil until listening)
	int server_port( lua_State *L ) {
		ServerUD *ud = CheckServer( L, 1 );
		const int port = ud->ctx->boundPort.load();

		if ( port > 0 )
			lua_pushinteger( L, port );
		else
			lua_pushnil( L );

		return 1;
	}



	// server:url() -> string | nil ("ws://127.0.0.1:8765", nil until listening)
	int server_url( lua_State *L ) {
		ServerUD *ud = CheckServer( L, 1 );
		const std::string url = BuildUrl( *ud->ctx );

		if ( url.empty() )
			lua_pushnil( L );
		else
			lua_pushlstring( L, url.data(), url.size() );

		return 1;
	}



	// server:running() -> boolean
	int server_running( lua_State *L ) {
		ServerUD *ud = CheckServer( L, 1 );

		lua_pushboolean( L, ud->ctx->running.load() && !ud->ctx->stopped.load() ? 1 : 0 );
		return 1;
	}



	// server:connections() -> number of open connections
	int server_connections( lua_State *L ) {
		ServerUD *ud = CheckServer( L, 1 );

		lua_pushinteger( L, ud->ctx->connectionCount.load() );
		return 1;
	}



	// server:dropped() -> number of message / ping / pong / drain events discarded because
	// the Rainmeter thread could not keep up.
	int server_dropped( lua_State *L ) {
		ServerUD *ud = CheckServer( L, 1 );

		lua_pushnumber( L, static_cast<lua_Number>( ud->ctx->droppedEvents.load() ) );
		return 1;
	}



	// ------------------------------------------------------------------
	// Events -> Lua callbacks
	// ------------------------------------------------------------------

	// Calls the function and its `nargs` arguments already on the stack.
	static void CallHandler( lua_State *L, core::Context &ctx, int nargs, const char *event ) {
		if ( lua_pcall( L, nargs, 0, 0 ) != LUA_OK ) {
			const char *err = lua_tostring( L, -1 );
			LogMsg( ctx.rain, LOG_WARNING, L"[RainJIT:WebSocket] Callback error (" + utf8_to_wstring( event ) + L"): " + utf8_to_wstring( err ? err : "unknown error" ) );
			lua_pop( L, 1 );
		}
	}



	// Hands one event to its Lua callback. Never raises.
	static void DispatchEvent( lua_State *L, core::Context &ctx, core::Event &ev ) {
		const int top = lua_gettop( L );

		if ( ev.type == core::Event::SERVER_ERROR ) {
			LogMsg( ctx.rain, LOG_ERROR, L"[RainJIT:WebSocket] Server error: " + utf8_to_wstring( ev.data ) );

			if ( ctx.refHandlers[ev.type] != LUA_NOREF && ctx.refSelfLua != LUA_NOREF ) {
				lua_rawgeti( L, LUA_REGISTRYINDEX, ctx.refHandlers[ev.type] );
				lua_rawgeti( L, LUA_REGISTRYINDEX, ctx.refSelfLua );
				lua_pushlstring( L, ev.data.data(), ev.data.size() );
				CallHandler( L, ctx, 2, EVENT_NAMES[ev.type] );
			}

			// The server thread is gone: nothing else will ever arrive.
			StopContext( ctx );
		}

		else if ( ev.type == core::Event::OPEN ) {
			if ( ev.conn && ctx.connRefs.find( ev.connId ) == ctx.connRefs.end() ) {
				void *mem = lua_newuserdata( L, sizeof( ConnectionUD ) );
				new ( mem ) ConnectionUD{ ev.conn, ctx.weak_from_this(), LUA_NOREF };

				luaL_getmetatable( L, CONNECTION_MT );
				lua_setmetatable( L, -2 );

				const int wsIndex = lua_gettop( L );

				lua_pushvalue( L, wsIndex );
				ctx.connRefs[ev.connId] = luaL_ref( L, LUA_REGISTRYINDEX );

				if ( ctx.refHandlers[ev.type] != LUA_NOREF ) {
					lua_rawgeti( L, LUA_REGISTRYINDEX, ctx.refHandlers[ev.type] );
					lua_pushvalue( L, wsIndex );
					CallHandler( L, ctx, 1, EVENT_NAMES[ev.type] );
				}
			}
		}

		else {
			auto it = ctx.connRefs.find( ev.connId );

			if ( it != ctx.connRefs.end() ) {
				const int handler = ctx.refHandlers[ev.type];

				if ( handler != LUA_NOREF ) {
					lua_rawgeti( L, LUA_REGISTRYINDEX, handler );
					lua_rawgeti( L, LUA_REGISTRYINDEX, it->second );

					int nargs = 1;

					if ( ev.type == core::Event::MESSAGE ) {
						lua_pushlstring( L, ev.data.data(), ev.data.size() );
						lua_pushboolean( L, ev.binary ? 1 : 0 );
						nargs = 3;
					}

					else if ( ev.type == core::Event::CLOSE ) {
						lua_pushinteger( L, ev.code );
						lua_pushlstring( L, ev.data.data(), ev.data.size() );
						nargs = 3;
					}

					else if ( ev.type == core::Event::PING || ev.type == core::Event::PONG ) {
						lua_pushlstring( L, ev.data.data(), ev.data.size() );
						nargs = 2;
					}

					CallHandler( L, ctx, nargs, EVENT_NAMES[ev.type] );
				}

				// The connection is over: its object is no longer kept alive by the registry
				// (looked up again, the callback may have stopped the server).
				if ( ev.type == core::Event::CLOSE ) {
					auto again = ctx.connRefs.find( ev.connId );

					if ( again != ctx.connRefs.end() ) {
						luaL_unref( L, LUA_REGISTRYINDEX, again->second );
						ctx.connRefs.erase( again );
					}
				}
			}
		}

		lua_settop( L, top );
	}



	// Runs the callbacks of the queued events. Called from the notification window
	// (Rainmeter thread) with the server object as argument 1.
	int server_dispatch( lua_State *L ) {
		ServerUD *ud = CheckServer( L, 1 );
		std::shared_ptr<core::Context> ctx = ud->ctx;

		// Re-armed first: an event that arrives while this batch is processed posts a new message.
		ctx->notifyPending.store( false );

		std::deque<core::Event> batch;
		bool more = false;
		{
			std::lock_guard<std::mutex> lock( ctx->eventMutex );

			while ( !ctx->events.empty() && batch.size() < MAX_EVENTS_PER_DISPATCH ) {
				batch.push_back( std::move( ctx->events.front() ) );
				ctx->events.pop_front();
			}

			more = !ctx->events.empty();
		}

		// More left: announce them with a new message, so other windows get their turn.
		if ( more && ctx->hNotifyWindow && !ctx->notifyPending.exchange( true ) ) {
			if ( !PostMessage( ctx->hNotifyWindow, WM_WEBSOCKET_EVENT, static_cast<WPARAM>( ctx->refSelf ), 0 ) )
				ctx->notifyPending.store( false );
		}

		for ( core::Event &ev : batch ) {
			// stop() was called from a callback: no more callbacks.
			if ( ctx->stopped.load() )
				break;

			DispatchEvent( L, *ctx, ev );
		}

		return 0;
	}



	// ------------------------------------------------------------------
	// Client object
	// ------------------------------------------------------------------

	static bool ValidCloseCode( lua_Integer code ) {
		return code == 1000 || ( code >= 1001 && code <= 1003 ) || ( code >= 1007 && code <= 1014 ) || ( code >= 3000 && code <= 4999 );
	}



	// __gc: the client object is no longer reachable from Lua (a running client is kept
	// alive through ctx->refSelfLua).
	static int client_gc( lua_State *L ) {
		auto *ud = static_cast<ClientUD *>( lua_touserdata( L, 1 ) );
		if ( !ud )
			return 0;

		if ( ud->ctx ) {
			StopContext( *ud->ctx );
			core::ContextRegistry::instance().removeContext( ud->ctx->refSelf );
		}

		ud->~ClientUD();
		return 0;
	}



	// The context of a client that can still talk, or nullptr.
	static std::shared_ptr<core::Context> LiveClient( const ClientUD *ud ) {
		const std::shared_ptr<core::Context> &ctx = ud->ctx;

		if ( ctx->stopped.load() || !ctx->running.load() || ctx->clientClosing.load() || ctx->connectionCount.load() <= 0 )
			return nullptr;

		return ctx;
	}



	// client:send( data [, binary] ) -> boolean
	// true when the message was queued; false until the connection is open and after it closes.
	static int client_send( lua_State *L ) {
		ClientUD *ud = CheckClient( L, 1 );

		size_t length = 0;
		const char *data = luaL_checklstring( L, 2, &length );
		const bool binary = lua_toboolean( L, 3 ) != 0;

		std::shared_ptr<core::Context> ctx = LiveClient( ud );

		if ( !ctx ) {
			lua_pushboolean( L, 0 );
			return 1;
		}

		core::Command cmd;
		cmd.type = core::Command::SEND;
		cmd.connId = 0; // a client has one connection: the network thread resolves it
		cmd.data.assign( data, length );
		cmd.binary = binary;

		network::Post( *ctx, std::move( cmd ) );

		lua_pushboolean( L, 1 );
		return 1;
	}



	// client:close( [code [, reason]] ) -> boolean
	// Closes the connection (close handshake). While it is still connecting, gives up
	// silently: no callback runs.
	static int client_close( lua_State *L ) {
		ClientUD *ud = CheckClient( L, 1 );

		const lua_Integer code = luaL_optinteger( L, 2, 1000 );
		size_t length = 0;
		const char *reason = luaL_optlstring( L, 3, "", &length );

		if ( !ValidCloseCode( code ) )
			return luaL_argerror( L, 2, "invalid WebSocket close code" );

		if ( length > 123 )
			length = 123;

		core::Context &ctx = *ud->ctx;

		if ( ctx.stopped.load() || !ctx.running.load() || ctx.clientClosing.load() ) {
			lua_pushboolean( L, 0 );
			return 1;
		}

		if ( ctx.connectionCount.load() <= 0 ) {
			StopContext( ctx );

			lua_pushboolean( L, 1 );
			return 1;
		}

		ctx.clientClosing.store( true );

		core::Command cmd;
		cmd.type = core::Command::CLOSE;
		cmd.connId = 0;
		cmd.code = static_cast<int>( code );
		cmd.data.assign( reason, length );

		network::Post( ctx, std::move( cmd ) );

		lua_pushboolean( L, 1 );
		return 1;
	}



	// client:on( event, callback ) -> client
	// A nil callback removes the handler.
	static int client_on( lua_State *L ) {
		ClientUD *ud = CheckClient( L, 1 );
		const int type = EventIndex( luaL_checkstring( L, 2 ) );

		if ( type < 0 )
			return luaL_argerror( L, 2, "unknown event (open, message, close, drain, ping, pong, error, reconnecting)" );

		if ( !lua_isnoneornil( L, 3 ) && !lua_isfunction( L, 3 ) )
			return luaL_argerror( L, 3, "function expected" );

		if ( !ud->ctx->stopped.load() )
			SetHandler( L, *ud->ctx, type, 3 );

		lua_pushvalue( L, 1 );
		return 1;
	}



	// client:isOpen() -> boolean (the handshake is done and the connection is not closing)
	static int client_isOpen( lua_State *L ) {
		ClientUD *ud = CheckClient( L, 1 );

		lua_pushboolean( L, LiveClient( ud ) ? 1 : 0 );
		return 1;
	}



	// client:url() -> string
	static int client_url( lua_State *L ) {
		ClientUD *ud = CheckClient( L, 1 );

		const std::string &url = ud->ctx->config.client.url;
		lua_pushlstring( L, url.data(), url.size() );
		return 1;
	}



	// Hands one event to its client callback. Never raises.
	// open( client, protocol ), message( client, data, isBinary ), close( client, code, reason ),
	// drain( client ), ping / pong( client, data ), error( client, message ),
	// reconnecting( client, attempt, delayMs )
	static void DispatchClientEvent( lua_State *L, core::Context &ctx, core::Event &ev ) {
		const int top = lua_gettop( L );
		const int handler = ctx.refHandlers[ev.type];

		if ( ev.type == core::Event::SERVER_ERROR )
			LogMsg( ctx.rain, ev.retry ? LOG_WARNING : LOG_ERROR, L"[RainJIT:WebSocket] Client error: " + utf8_to_wstring( ev.data ) );

		if ( handler != LUA_NOREF && ctx.refSelfLua != LUA_NOREF ) {
			lua_rawgeti( L, LUA_REGISTRYINDEX, handler );
			lua_rawgeti( L, LUA_REGISTRYINDEX, ctx.refSelfLua );

			int nargs = 1;

			switch ( ev.type ) {
				case core::Event::OPEN:
					if ( ev.data.empty() )
						lua_pushnil( L );
					else
						lua_pushlstring( L, ev.data.data(), ev.data.size() );

					nargs = 2;
					break;

				case core::Event::MESSAGE:
					lua_pushlstring( L, ev.data.data(), ev.data.size() );
					lua_pushboolean( L, ev.binary ? 1 : 0 );
					nargs = 3;
					break;

				case core::Event::CLOSE:
					lua_pushinteger( L, ev.code );
					lua_pushlstring( L, ev.data.data(), ev.data.size() );
					nargs = 3;
					break;

				case core::Event::PING:
				case core::Event::PONG:
				case core::Event::SERVER_ERROR:
					lua_pushlstring( L, ev.data.data(), ev.data.size() );
					nargs = 2;
					break;

				case core::Event::RECONNECTING:
					lua_pushinteger( L, ev.code );
					lua_pushinteger( L, static_cast<lua_Integer>( ev.delay ) );
					nargs = 3;
					break;

				default:
					break;
			}

			CallHandler( L, ctx, nargs, EVENT_NAMES[ev.type] );
		}

		// Closed or failed for good (no reconnection follows): nothing else will arrive.
		if ( ( ev.type == core::Event::CLOSE || ev.type == core::Event::SERVER_ERROR ) && !ev.retry )
			StopContext( ctx );

		lua_settop( L, top );
	}



	// Runs the callbacks of the queued events. Called from the notification window
	// (Rainmeter thread) with the client object as argument 1.
	int client_dispatch( lua_State *L ) {
		ClientUD *ud = CheckClient( L, 1 );
		std::shared_ptr<core::Context> ctx = ud->ctx;

		ctx->notifyPending.store( false );

		std::deque<core::Event> batch;
		bool more = false;
		{
			std::lock_guard<std::mutex> lock( ctx->eventMutex );

			while ( !ctx->events.empty() && batch.size() < MAX_EVENTS_PER_DISPATCH ) {
				batch.push_back( std::move( ctx->events.front() ) );
				ctx->events.pop_front();
			}

			more = !ctx->events.empty();
		}

		if ( more && ctx->hNotifyWindow && !ctx->notifyPending.exchange( true ) ) {
			if ( !PostMessage( ctx->hNotifyWindow, WM_WEBSOCKET_EVENT, static_cast<WPARAM>( ctx->refSelf ), 0 ) )
				ctx->notifyPending.store( false );
		}

		for ( core::Event &ev : batch ) {
			// close() while connecting, or stop from a callback: no more callbacks.
			if ( ctx->stopped.load() )
				break;

			DispatchClientEvent( L, *ctx, ev );
		}

		return 0;
	}



	// ------------------------------------------------------------------
	// Connection object
	// ------------------------------------------------------------------

	// __gc: releases the value kept by setData()
	static int connection_gc( lua_State *L ) {
		auto *ud = static_cast<ConnectionUD *>( lua_touserdata( L, 1 ) );
		if ( !ud )
			return 0;

		if ( ud->refData != LUA_NOREF ) {
			luaL_unref( L, LUA_REGISTRYINDEX, ud->refData );
			ud->refData = LUA_NOREF;
		}

		ud->~ConnectionUD();
		return 0;
	}



	// The server of an open connection, or nullptr when it can no longer be reached.
	static std::shared_ptr<core::Context> LiveServer( const ConnectionUD *ud ) {
		std::shared_ptr<core::Context> ctx = ud->ctx.lock();

		if ( !ctx || ctx->stopped.load() || !ctx->running.load() || !ud->conn->open.load() )
			return nullptr;

		return ctx;
	}



	// connection:send( data [, binary] ) -> boolean
	// true when the message was queued; false when the connection is closed.
	int connection_send( lua_State *L ) {
		ConnectionUD *ud = CheckConnection( L, 1 );

		size_t length = 0;
		const char *data = luaL_checklstring( L, 2, &length );
		const bool binary = lua_toboolean( L, 3 ) != 0;

		std::shared_ptr<core::Context> ctx = LiveServer( ud );

		if ( !ctx ) {
			lua_pushboolean( L, 0 );
			return 1;
		}

		core::Command cmd;
		cmd.type = core::Command::SEND;
		cmd.connId = ud->conn->id;
		cmd.data.assign( data, length );
		cmd.binary = binary;

		network::Post( *ctx, std::move( cmd ) );

		lua_pushboolean( L, 1 );
		return 1;
	}



	// connection:close( [code [, reason]] ) -> boolean
	// code: 1000 (default), 1001-1003, 1007-1014 or 3000-4999. reason: up to 123 bytes.
	int connection_close( lua_State *L ) {
		ConnectionUD *ud = CheckConnection( L, 1 );

		const lua_Integer code = luaL_optinteger( L, 2, 1000 );
		size_t length = 0;
		const char *reason = luaL_optlstring( L, 3, "", &length );

		if ( !ValidCloseCode( code ) )
			return luaL_argerror( L, 2, "invalid WebSocket close code" );

		if ( length > 123 )
			length = 123;

		std::shared_ptr<core::Context> ctx = LiveServer( ud );

		if ( !ctx ) {
			lua_pushboolean( L, 0 );
			return 1;
		}

		core::Command cmd;
		cmd.type = core::Command::CLOSE;
		cmd.connId = ud->conn->id;
		cmd.code = static_cast<int>( code );
		cmd.data.assign( reason, length );

		network::Post( *ctx, std::move( cmd ) );

		lua_pushboolean( L, 1 );
		return 1;
	}



	// connection:subscribe( topic ) -> boolean
	int connection_subscribe( lua_State *L ) {
		ConnectionUD *ud = CheckConnection( L, 1 );

		size_t length = 0;
		const char *topic = luaL_checklstring( L, 2, &length );

		std::shared_ptr<core::Context> ctx = LiveServer( ud );

		if ( !ctx ) {
			lua_pushboolean( L, 0 );
			return 1;
		}

		core::Command cmd;
		cmd.type = core::Command::SUBSCRIBE;
		cmd.connId = ud->conn->id;
		cmd.topic.assign( topic, length );

		network::Post( *ctx, std::move( cmd ) );

		lua_pushboolean( L, 1 );
		return 1;
	}



	// connection:unsubscribe( topic ) -> boolean
	int connection_unsubscribe( lua_State *L ) {
		ConnectionUD *ud = CheckConnection( L, 1 );

		size_t length = 0;
		const char *topic = luaL_checklstring( L, 2, &length );

		std::shared_ptr<core::Context> ctx = LiveServer( ud );

		if ( !ctx ) {
			lua_pushboolean( L, 0 );
			return 1;
		}

		core::Command cmd;
		cmd.type = core::Command::UNSUBSCRIBE;
		cmd.connId = ud->conn->id;
		cmd.topic.assign( topic, length );

		network::Post( *ctx, std::move( cmd ) );

		lua_pushboolean( L, 1 );
		return 1;
	}



	// connection:publish( topic, data [, binary] ) -> boolean
	// Sends to every subscriber of the topic except this connection.
	int connection_publish( lua_State *L ) {
		ConnectionUD *ud = CheckConnection( L, 1 );

		size_t topicLength = 0;
		const char *topic = luaL_checklstring( L, 2, &topicLength );
		size_t dataLength = 0;
		const char *data = luaL_checklstring( L, 3, &dataLength );
		const bool binary = lua_toboolean( L, 4 ) != 0;

		std::shared_ptr<core::Context> ctx = LiveServer( ud );

		if ( !ctx ) {
			lua_pushboolean( L, 0 );
			return 1;
		}

		core::Command cmd;
		cmd.type = core::Command::PUBLISH;
		cmd.connId = ud->conn->id;
		cmd.topic.assign( topic, topicLength );
		cmd.data.assign( data, dataLength );
		cmd.binary = binary;

		network::Post( *ctx, std::move( cmd ) );

		lua_pushboolean( L, 1 );
		return 1;
	}



	// connection:ip() -> string
	int connection_ip( lua_State *L ) {
		ConnectionUD *ud = CheckConnection( L, 1 );

		lua_pushlstring( L, ud->conn->ip.data(), ud->conn->ip.size() );
		return 1;
	}



	// connection:id() -> number (unique within the server)
	int connection_id( lua_State *L ) {
		ConnectionUD *ud = CheckConnection( L, 1 );

		lua_pushnumber( L, static_cast<lua_Number>( ud->conn->id ) );
		return 1;
	}



	// connection:isOpen() -> boolean
	int connection_isOpen( lua_State *L ) {
		ConnectionUD *ud = CheckConnection( L, 1 );

		lua_pushboolean( L, ud->conn->open.load() ? 1 : 0 );
		return 1;
	}



	// connection:setData( value ) -> connection
	// Keeps any Lua value with the connection (nil clears it).
	int connection_setData( lua_State *L ) {
		ConnectionUD *ud = CheckConnection( L, 1 );

		if ( ud->refData != LUA_NOREF ) {
			luaL_unref( L, LUA_REGISTRYINDEX, ud->refData );
			ud->refData = LUA_NOREF;
		}

		if ( !lua_isnoneornil( L, 2 ) ) {
			lua_pushvalue( L, 2 );
			ud->refData = luaL_ref( L, LUA_REGISTRYINDEX );
		}

		lua_pushvalue( L, 1 );
		return 1;
	}



	// connection:getData() -> value | nil
	int connection_getData( lua_State *L ) {
		ConnectionUD *ud = CheckConnection( L, 1 );

		if ( ud->refData == LUA_NOREF )
			lua_pushnil( L );
		else
			lua_rawgeti( L, LUA_REGISTRYINDEX, ud->refData );

		return 1;
	}



	// ------------------------------------------------------------------
	// Module
	// ------------------------------------------------------------------

	// websocket.server( options ) -> server | nil, err
	//
	// Options (all optional, see ParseOptions):
	//   host, port, portTries, path, maxPayload, maxBackpressure, idleTimeout, maxConnections,
	//   sendPings, auth = { type, name, secret | secrets },
	//   open, message, close, drain, ping, pong, error (callbacks)
	int websocket_server( lua_State *L ) {
		auto rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );
		luaL_checktype( L, 1, LUA_TTABLE );

		auto ctx = std::make_shared<core::Context>( rain );
		std::string error;

		const int top = lua_gettop( L );
		const bool ok = ParseOptions( L, 1, rain, *ctx, error );
		lua_settop( L, top );

		if ( !ok ) {
			// Handlers referenced so far are released with the context.
			ctx.reset();

			LogMsg( rain, LOG_ERROR, L"[RainJIT:WebSocket] " + utf8_to_wstring( error ) );

			lua_pushnil( L );
			lua_pushlstring( L, error.data(), error.size() );
			return 2;
		}

		const bool loopback = IsLoopbackHost( ctx->config.host );

		if ( !loopback && ctx->config.auth.type == core::AuthConfig::AUTH_NONE )
			LogMsg( rain, LOG_WARNING, L"[RainJIT:WebSocket] Listening on a non-loopback address without auth: anyone on the network can connect" );

		if ( !loopback )
			LogMsg( rain, LOG_WARNING, L"[RainJIT:WebSocket] Listening on a non-loopback address: ws is not encrypted, the traffic (and the credential) can be read on the network" );

		ctx->hNotifyWindow = GetNotifyWindow( rain );
		ctx->rainValid = true;

		// Register context (the notification window finds it by this id)
		ctx->refSelf = core::ContextRegistry::instance().registerContext( ctx );

		// The server object. It is only referenced from the registry while it runs
		// (see server_listen), so an unused server is garbage collected.
		void *mem = lua_newuserdata( L, sizeof( ServerUD ) );
		new ( mem ) ServerUD{ ctx };

		luaL_getmetatable( L, SERVER_MT );
		lua_setmetatable( L, -2 );

		return 1;
	}



	// websocket.connect( options ) -> client | nil, err
	//
	// Options: url (required, ws://host[:port][/path][?query]), headers = { Name = "value" },
	//   protocol | protocols, connectTimeout (ms), maxPayload, maxBackpressure, idleTimeout,
	//   sendPings, reconnect = true | { delay, maxDelay, maxTries },
	//   open, message, close, drain, ping, pong, error, reconnecting (callbacks)
	//
	// Returns at once: the TCP connect and the handshake run on the client thread and are
	// reported with the open (or error) callback.
	int websocket_connect( lua_State *L ) {
		auto rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );
		luaL_checktype( L, 1, LUA_TTABLE );

		auto ctx = std::make_shared<core::Context>( rain );
		std::string error;

		const int top = lua_gettop( L );
		const bool ok = ParseClientOptions( L, 1, rain, *ctx, error );
		lua_settop( L, top );

		if ( !ok ) {
			ctx.reset();

			LogMsg( rain, LOG_ERROR, L"[RainJIT:WebSocket] " + utf8_to_wstring( error ) );

			lua_pushnil( L );
			lua_pushlstring( L, error.data(), error.size() );
			return 2;
		}

		ctx->hNotifyWindow = GetNotifyWindow( rain );
		ctx->rainValid = true;
		ctx->refSelf = core::ContextRegistry::instance().registerContext( ctx );

		void *mem = lua_newuserdata( L, sizeof( ClientUD ) );
		new ( mem ) ClientUD{ ctx };

		luaL_getmetatable( L, CLIENT_MT );
		lua_setmetatable( L, -2 );

		// A failed start shuts the context down; the object is collected by Lua later.
		if ( !StartContext( L, lua_gettop( L ), ctx, error ) ) {
			LogMsg( rain, LOG_ERROR, L"[RainJIT:WebSocket] " + utf8_to_wstring( error ) );

			lua_pop( L, 1 );
			lua_pushnil( L );
			lua_pushlstring( L, error.data(), error.size() );
			return 2;
		}

		LogMsg( rain, LOG_DEBUG, L"[RainJIT:WebSocket] Connecting to " + utf8_to_wstring( ctx->config.client.url ) );

		return 1;
	}



	// Creates the metatables of the server / connection / client objects once per Lua state
	// (they live in its registry).
	static void RegisterMetatables( lua_State *L ) {
		// --- Server object
		if ( luaL_newmetatable( L, SERVER_MT ) ) {
			// clang-format off
			static const struct luaL_Reg methods[] = {
				{ "listen",      server_listen      },
				{ "stop",        server_stop        },
				{ "on",          server_on          },
				{ "publish",     server_publish     },
				{ "broadcast",   server_broadcast   },
				{ "connection",  server_connection  },
				{ "host",        server_host        },
				{ "port",        server_port        },
				{ "url",         server_url         },
				{ "running",     server_running     },
				{ "connections", server_connections },
				{ "dropped",     server_dropped     },
				{ NULL, NULL }
			};
			// clang-format on

			lua_newtable( L );
			for ( const luaL_Reg *m = methods; m->name; m++ ) {
				lua_pushcfunction( L, m->func );
				lua_setfield( L, -2, m->name );
			}
			lua_setfield( L, -2, "__index" );

			lua_pushcfunction( L, server_gc );
			lua_setfield( L, -2, "__gc" );
		}
		lua_pop( L, 1 );

		// --- Client object
		if ( luaL_newmetatable( L, CLIENT_MT ) ) {
			// clang-format off
			static const struct luaL_Reg methods[] = {
				{ "send",   client_send   },
				{ "close",  client_close  },
				{ "on",     client_on     },
				{ "isOpen", client_isOpen },
				{ "url",    client_url    },
				{ NULL, NULL }
			};
			// clang-format on

			lua_newtable( L );
			for ( const luaL_Reg *m = methods; m->name; m++ ) {
				lua_pushcfunction( L, m->func );
				lua_setfield( L, -2, m->name );
			}
			lua_setfield( L, -2, "__index" );

			lua_pushcfunction( L, client_gc );
			lua_setfield( L, -2, "__gc" );
		}
		lua_pop( L, 1 );

		// --- Connection object
		if ( luaL_newmetatable( L, CONNECTION_MT ) ) {
			// clang-format off
			static const struct luaL_Reg methods[] = {
				{ "send",        connection_send        },
				{ "close",       connection_close       },
				{ "subscribe",   connection_subscribe   },
				{ "unsubscribe", connection_unsubscribe },
				{ "publish",     connection_publish     },
				{ "ip",          connection_ip          },
				{ "id",          connection_id          },
				{ "isOpen",      connection_isOpen      },
				{ "setData",     connection_setData     },
				{ "getData",     connection_getData     },
				{ NULL, NULL }
			};
			// clang-format on

			lua_newtable( L );
			for ( const luaL_Reg *m = methods; m->name; m++ ) {
				lua_pushcfunction( L, m->func );
				lua_setfield( L, -2, m->name );
			}
			lua_setfield( L, -2, "__index" );

			lua_pushcfunction( L, connection_gc );
			lua_setfield( L, -2, "__gc" );
		}
		lua_pop( L, 1 );
	}



	int luaopen_websocket( lua_State *L ) {
		auto rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );

		RegisterMetatables( L );

		lua_newtable( L );

		lua_pushlightuserdata( L, rain );
		lua_pushcclosure( L, websocket_server, 1 );
		lua_setfield( L, -2, "server" );

		lua_pushlightuserdata( L, rain );
		lua_pushcclosure( L, websocket_connect, 1 );
		lua_setfield( L, -2, "connect" );

		return 1;
	}



	void RegisterModule( lua_State *L, Rain *rain ) {
		// Create the notification window for this measure
		CreateNotifyWindow( rain );

		lua_getglobal( L, "package" );
		lua_getfield( L, -1, "preload" );

		lua_pushlightuserdata( L, rain );
		lua_pushcclosure( L, luaopen_websocket, 1 );
		lua_setfield( L, -2, "websocket" );

		lua_pop( L, 2 );
	}

} // namespace websocket::lua



namespace websocket {
	void CleanupAutoDispatch( Rain *rain ) {
		lua::DestroyNotifyWindow( rain );
	}

} // namespace websocket
