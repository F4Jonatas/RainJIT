/**
 * @file lua.cpp
 * @brief Lua bindings implementation for fetch module
 * @license GPL v2.0 License
 */

#include <lua.hpp>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "http.hpp"
#include "lua.hpp"

#include <Includes/rain.hpp>
#include <RainmeterAPI.hpp>
#include <Utils/filesystem.hpp>


#define WM_FETCH_COMPLETE ( WM_APP + 3 )

// Mapa global para associar Rain* ao HWND da janela de notificação
static std::unordered_map<Rain *, HWND> g_notifyWindows;
static std::mutex g_notifyMutex;






namespace lua {

	// Procedimento da janela
	// wParam contém o ID do contexto (ctx->refSelf)
	LRESULT CALLBACK FetchNotifyWndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam ) {
		if ( msg == WM_FETCH_COMPLETE ) {
			int ctxId = static_cast<int>( wParam );
			auto ctx = core::ContextRegistry::instance().getContext( ctxId );

			if ( ctx && ctx->completed.load() ) {
				if ( ctx->rain->L ) {
					// Coloca a função fetch_dispatch e o objeto self na pilha
					lua_rawgeti( ctx->rain->L, LUA_REGISTRYINDEX, ctx->refSelfLua ); // self
					lua_pushcfunction( ctx->rain->L, lua::fetch_dispatch ); // function
					lua_pushvalue( ctx->rain->L, -2 ); // self argument

					if ( lua_pcall( ctx->rain->L, 1, 1, 0 ) != LUA_OK ) {
						const char *err = lua_tostring( ctx->rain->L, -1 );
						if ( err )
#ifdef __RAINMETERAPI_H__
							RmLog( ctx->rain->rm, LOG_ERROR, utf8_to_wstring( err ).c_str() );
#endif

						lua_pop( ctx->rain->L, 1 );
					}

					// remove self
					lua_pop( ctx->rain->L, 1 );
				}
			}

			return 0;
		}

		return DefWindowProc( hWnd, msg, wParam, lParam );
	}



	// Cria a janela para um Rain*
	void CreateNotifyWindow( Rain *rain ) {
		// Registra a classe da janela uma única vez
		static bool classRegistered = false;
		if ( !classRegistered ) {
			WNDCLASSEX wc = { sizeof( WNDCLASSEX ) };
			wc.lpfnWndProc = FetchNotifyWndProc;
			wc.hInstance = GetModuleHandle( nullptr );
			wc.lpszClassName = L"RainJIT_FetchNotifyWindow";

			if ( !RegisterClassEx( &wc ) ) {
				DWORD err = GetLastError();
				if ( err != ERROR_CLASS_ALREADY_EXISTS ) {
#ifdef __RAINMETERAPI_H__
					RmLog( rain->rm, LOG_ERROR, L"Failed to register fetch notify window class" );
#endif

					return;
				}
			}

			classRegistered = true;
		}

		// clang-format off
		HWND hWnd = CreateWindowEx(
			0,
			L"RainJIT_FetchNotifyWindow",
			L"", 0, 0, 0, 0, 0,
			nullptr,
			nullptr,
			GetModuleHandle( nullptr ),
			nullptr
		);
		// clang-format on

		if ( hWnd ) {
			// Armazena o ponteiro Rain* na janela (opcional, útil para debug)
			SetWindowLongPtr( hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( rain ) );
			std::lock_guard<std::mutex> lock( g_notifyMutex );
			g_notifyWindows[rain] = hWnd;
		}

		else
#ifdef __RAINMETERAPI_H__
			RmLog( rain->rm, LOG_ERROR, L"Failed to create fetch notify window" );
#endif
	}



	// Obtém o HWND associado ao Rain*
	HWND GetNotifyWindow( Rain *rain ) {
		std::lock_guard<std::mutex> lock( g_notifyMutex );
		auto it = g_notifyWindows.find( rain );
		return ( it != g_notifyWindows.end() ) ? it->second : nullptr;
	}



	// Destroi a janela e remove do mapa
	void DestroyNotifyWindow( Rain *rain ) {
		HWND hWnd = nullptr;
		{
			std::lock_guard<std::mutex> lock( g_notifyMutex );
			auto it = g_notifyWindows.find( rain );
			if ( it != g_notifyWindows.end() ) {
				hWnd = it->second;
				g_notifyWindows.erase( it );
			}
		}

		if ( hWnd )
			DestroyWindow( hWnd );
	}



	std::shared_ptr<core::FetchContext> GetFetchContext( lua_State *L, int idx ) {
		lua_pushstring( L, "__fetch_id" );
		lua_rawget( L, idx );

		std::shared_ptr<core::FetchContext> ctx = nullptr;
		if ( lua_isnumber( L, -1 ) ) {
			int id = (int)lua_tointeger( L, -1 );
			lua_pop( L, 1 );
			ctx = core::ContextRegistry::instance().getContext( id );
		}

		else
			lua_pop( L, 1 );

		return ctx;
	}



	// Função auxiliar (fora de PushResponseTable) para criar tabela case‑insensitive
	// Função auxiliar para criar tabela case‑insensitive (mesma de antes)
	static void createCaseInsensitiveTable( lua_State *L, const std::map<std::string, std::string> &map ) {
		lua_newtable( L );
		for ( const auto &pair : map ) {
			std::string lower_key;
			for ( char c : pair.first )
				lower_key += tolower( c );
			lua_pushstring( L, lower_key.c_str() );
			lua_pushstring( L, pair.second.c_str() );
			lua_rawset( L, -3 );
		}
		lua_newtable( L );
		lua_pushcfunction( L, []( lua_State *L2 ) -> int {
			const char *key = lua_tostring( L2, 2 );
			if ( !key )
				return 0;
			std::string lower;
			for ( const char *p = key; *p; ++p )
				lower += tolower( *p );
			lua_pushstring( L2, lower.c_str() );
			lua_rawget( L2, 1 );
			return 1;
		} );
		lua_setfield( L, -2, "__index" );
		lua_setmetatable( L, -2 );
	}

	void PushResponseTable( lua_State *L, Rain *rain, const core::FetchResponse &response ) {
		lua_newtable( L );

		lua_pushboolean( L, ( response.status >= 200 && response.status < 300 ) );
		lua_setfield( L, -2, "ok" );

		lua_pushinteger( L, response.status );
		lua_setfield( L, -2, "status" );

		// Raw body stored as a Lua string in a private field.
		// Exposed lazily via :text() and :bytes() — never duplicated.
		if ( !response.body.empty() )
			lua_pushlstring( L, reinterpret_cast<const char *>( response.body.data() ), response.body.size() );
		else
			lua_pushstring( L, "" );
		lua_setfield( L, -2, "__body" );

		lua_pushstring( L, response.error.c_str() );
		lua_setfield( L, -2, "error" );

		// Use full userdata (not lightuserdata) so __gc is called by the GC.
		using MapType = std::map<std::string, std::string>;

		auto pushMapUserdata = [&]( const MapType &src ) {
			void *ud = lua_newuserdata( L, sizeof( MapType ) );
			new ( ud ) MapType( src );
			lua_newtable( L );
			lua_pushcfunction( L, []( lua_State *L2 ) -> int {
				MapType *m = static_cast<MapType *>( lua_touserdata( L2, 1 ) );
				if ( m ) m->~MapType();
				return 0;
			} );
			lua_setfield( L, -2, "__gc" );
			lua_setmetatable( L, -2 );
		};

		pushMapUserdata( response.headers );
		lua_setfield( L, -2, "__headers_ud" );

		pushMapUserdata( response.cookies );
		lua_setfield( L, -2, "__cookies_ud" );

		// Metatable com métodos
		lua_newtable( L );
		lua_pushvalue( L, -1 );
		lua_setfield( L, -2, "__index" );

		// :headers()
		lua_pushcfunction( L, []( lua_State *L2 ) -> int {
			lua_getfield( L2, 1, "__headers_ud" );
			auto *map = static_cast<MapType *>( lua_touserdata( L2, -1 ) );
			lua_pop( L2, 1 );
			if ( !map ) { lua_pushnil( L2 ); return 1; }
			createCaseInsensitiveTable( L2, *map );
			return 1;
		} );
		lua_setfield( L, -2, "headers" );

		// :cookies()
		lua_pushcfunction( L, []( lua_State *L2 ) -> int {
			lua_getfield( L2, 1, "__cookies_ud" );
			auto *map = static_cast<MapType *>( lua_touserdata( L2, -1 ) );
			lua_pop( L2, 1 );
			if ( !map ) { lua_pushnil( L2 ); return 1; }
			createCaseInsensitiveTable( L2, *map );
			return 1;
		} );
		lua_setfield( L, -2, "cookies" );

		// :bytes() — returns raw body as a binary Lua string, no conversion
		lua_pushcfunction( L, []( lua_State *L2 ) -> int {
			lua_getfield( L2, 1, "__body" );
			return 1;
		} );
		lua_setfield( L, -2, "bytes" );

		// :text() — returns body interpreted as a UTF-8 string, no conversion needed
		// (body is already stored as raw bytes; Lua strings are byte arrays)
		lua_pushcfunction( L, []( lua_State *L2 ) -> int {
			lua_getfield( L2, 1, "__body" );
			return 1;
		} );
		lua_setfield( L, -2, "text" );

		// :xml() — parses the body as XML and returns doc, err (identical to xml.parse(body))
		// Requires the xml module to be registered via xml::RegisterModule().
		lua_pushlightuserdata( L, rain );
		lua_pushcclosure( L, []( lua_State *L2 ) -> int {
			Rain *r = static_cast<Rain *>( lua_touserdata( L2, lua_upvalueindex( 1 ) ) );

			// Step 1: require("xml")
			lua_getglobal( L2, "require" );
			lua_pushstring( L2, "xml" );
			if ( lua_pcall( L2, 1, 1, 0 ) != LUA_OK ) {
				const char *err = lua_tostring( L2, -1 );
				if ( r && r->rm ) {
					std::wstring msg = L"[RainJIT:Fetch] response:xml() — xml module unavailable: " +
					                   utf8_to_wstring( err ? err : "unknown error" );
					RmLog( r->rm, LOG_ERROR, msg.c_str() );
				}
				lua_pop( L2, 1 );
				lua_pushnil( L2 );
				lua_pushstring( L2, "xml module not available" );
				return 2;
			}
			// Stack: [xml_module]

			// Step 2: get xml.parse
			lua_getfield( L2, -1, "parse" );
			lua_remove( L2, -2 ); // remove xml_module — stack: [xml.parse]

			// Step 3: push body as argument
			lua_getfield( L2, 1, "__body" ); // stack: [xml.parse, body]

			// Step 4: call xml.parse(body) → doc, err  (2 return values)
			if ( lua_pcall( L2, 1, 2, 0 ) != LUA_OK ) {
				const char *err = lua_tostring( L2, -1 );
				if ( r && r->rm ) {
					std::wstring msg = L"[RainJIT:Fetch] response:xml() — parse failed: " +
					                   utf8_to_wstring( err ? err : "unknown error" );
					RmLog( r->rm, LOG_ERROR, msg.c_str() );
				}
				lua_pop( L2, 1 );
				lua_pushnil( L2 );
				lua_pushstring( L2, "xml.parse() call failed" );
				return 2;
			}

			// Stack: [doc, err] — return both to preserve xml.parse() contract
			return 2;
		}, 1 );
		lua_setfield( L, -2, "xml" );

		// :html() — parses the body as HTML and returns doc, err (identical to html.parse(body))
		// Requires the html module to be registered via html::RegisterModule().
		lua_pushlightuserdata( L, rain );
		lua_pushcclosure( L, []( lua_State *L2 ) -> int {
			Rain *r = static_cast<Rain *>( lua_touserdata( L2, lua_upvalueindex( 1 ) ) );

			// Step 1: require("html")
			lua_getglobal( L2, "require" );
			lua_pushstring( L2, "html" );
			if ( lua_pcall( L2, 1, 1, 0 ) != LUA_OK ) {
				const char *err = lua_tostring( L2, -1 );
				if ( r && r->rm ) {
					std::wstring msg = L"[RainJIT:Fetch] response:html() — html module unavailable: " +
					                   utf8_to_wstring( err ? err : "unknown error" );
					RmLog( r->rm, LOG_ERROR, msg.c_str() );
				}
				lua_pop( L2, 1 );
				lua_pushnil( L2 );
				lua_pushstring( L2, "html module not available" );
				return 2;
			}
			// Stack: [html_module]

			// Step 2: get html.parse
			lua_getfield( L2, -1, "parse" );
			lua_remove( L2, -2 ); // remove html_module — stack: [html.parse]

			// Step 3: push body as argument
			lua_getfield( L2, 1, "__body" ); // stack: [html.parse, body]

			// Step 4: call html.parse(body) → doc [, err]  (2 slots)
			if ( lua_pcall( L2, 1, 2, 0 ) != LUA_OK ) {
				const char *err = lua_tostring( L2, -1 );
				if ( r && r->rm ) {
					std::wstring msg = L"[RainJIT:Fetch] response:html() — parse failed: " +
					                   utf8_to_wstring( err ? err : "unknown error" );
					RmLog( r->rm, LOG_ERROR, msg.c_str() );
				}
				lua_pop( L2, 1 );
				lua_pushnil( L2 );
				lua_pushstring( L2, "html.parse() call failed" );
				return 2;
			}

			// Stack: [doc, nil|errmsg] — return both slots
			return 2;
		}, 1 );
		lua_setfield( L, -2, "html" );


		// :json() — parses the body as JSON and returns data (identical to json(body))
		// Requires the json module to be registered via json::RegisterModule().
		lua_pushlightuserdata( L, rain );
		lua_pushcclosure( L, []( lua_State *L2 ) -> int {
			Rain *r = static_cast<Rain *>( lua_touserdata( L2, lua_upvalueindex( 1 ) ) );

			// Step 1: require("json")
			lua_getglobal( L2, "require" );
			lua_pushstring( L2, "json" );
			if ( lua_pcall( L2, 1, 1, 0 ) != LUA_OK ) {
				const char *err = lua_tostring( L2, -1 );
				if ( r && r->rm ) {
					std::wstring msg = L"[RainJIT:Fetch] response:json() — json module unavailable: " +
					                   utf8_to_wstring( err ? err : "unknown error" );
					RmLog( r->rm, LOG_ERROR, msg.c_str() );
				}
				lua_pop( L2, 1 );
				lua_pushnil( L2 );
				return 1;
			}
			// Stack: [json_fn]

			// Step 2: push body as argument
			lua_getfield( L2, 1, "__body" ); // stack: [json_fn, body]

			// Step 3: call json(body) → data  (1 return value)
			if ( lua_pcall( L2, 1, 1, 0 ) != LUA_OK ) {
				const char *err = lua_tostring( L2, -1 );
				if ( r && r->rm ) {
					std::wstring msg = L"[RainJIT:Fetch] response:json() — parse failed: " +
					                   utf8_to_wstring( err ? err : "unknown error" );
					RmLog( r->rm, LOG_ERROR, msg.c_str() );
				}
				lua_pop( L2, 1 );
				lua_pushnil( L2 );
				return 1;
			}

			// Stack: [data]
			return 1;
		}, 1 );
		lua_setfield( L, -2, "json" );


		// :save(path) — resolves Rainmeter variables and relative paths via absPath
		lua_pushlightuserdata( L, rain );
		lua_pushcclosure( L, []( lua_State *L2 ) -> int {
			Rain *r = static_cast<Rain *>( lua_touserdata( L2, lua_upvalueindex( 1 ) ) );

			if ( !lua_istable( L2, 1 ) ) {
				lua_pushboolean( L2, 0 );
				lua_pushstring( L2, "Invalid response table" );
				return 2;
			}

			const char *filePath = luaL_checkstring( L2, 2 );
			if ( !filePath || !*filePath ) {
				lua_pushboolean( L2, 0 );
				lua_pushstring( L2, "Invalid file path" );
				return 2;
			}

			// Resolve Rainmeter variables and canonicalize path
			std::wstring wPath = utf8_to_wstring( filePath );
			if ( r ) {
				std::wstring resolved = r->absPath( wPath );
				if ( !resolved.empty() )
					wPath = resolved;
			}

			if ( wPath.empty() ) {
				lua_pushboolean( L2, 0 );
				lua_pushstring( L2, "Invalid file path encoding" );
				return 2;
			}

			lua_getfield( L2, 1, "__body" );
			if ( !lua_isstring( L2, -1 ) ) {
				lua_pop( L2, 1 );
				lua_pushboolean( L2, 0 );
				lua_pushstring( L2, "No body data in response" );
				return 2;
			}

			size_t bodySize = 0;
			const char *bodyData = lua_tolstring( L2, -1, &bodySize );
			lua_pop( L2, 1 );

			if ( !bodyData || bodySize == 0 ) {
				lua_pushboolean( L2, 0 );
				lua_pushstring( L2, "Empty response body" );
				return 2;
			}

			if ( !fs::CreateDirectoriesRecursive( wPath ) ) {
				lua_pushboolean( L2, 0 );
				lua_pushstring( L2, "Failed to create directory" );
				return 2;
			}

			bool ok = fs::SaveToFile( wPath, reinterpret_cast<const BYTE *>( bodyData ), bodySize );
			lua_pushboolean( L2, ok ? 1 : 0 );
			lua_pushstring( L2, ok ? nullptr : "Failed to write file" );
			return ok ? 1 : 2;
		}, 1 );
		lua_setfield( L, -2, "save" );

		lua_setmetatable( L, -2 );
	}



	/// MÉTODOS DO OBJETO FETCH
	int response_save( lua_State *L ) {
		// Get response table from stack
		if ( !lua_istable( L, 1 ) ) {
			lua_pushboolean( L, 0 );
			lua_pushstring( L, "Invalid response table" );
			return 2;
		}

		// Get file path
		const char *filePath = luaL_checkstring( L, 2 );
		if ( !filePath || !*filePath ) {
			lua_pushboolean( L, 0 );
			lua_pushstring( L, "Invalid file path" );
			return 2;
		}

		// Convert to UTF-16
		std::wstring wFilePath = utf8_to_wstring( filePath );
		if ( wFilePath.empty() ) {
			lua_pushboolean( L, 0 );
			lua_pushstring( L, "Invalid file path encoding" );
			return 2;
		}

		// Get body from response table
		lua_getfield( L, 1, "__body" );
		if ( !lua_isstring( L, -1 ) ) {
			lua_pop( L, 1 );
			lua_pushboolean( L, 0 );
			lua_pushstring( L, "No body data in response" );
			return 2;
		}

		size_t bodySize = 0;
		const char *bodyData = lua_tolstring( L, -1, &bodySize );
		lua_pop( L, 1 );

		if ( !bodyData || bodySize == 0 ) {
			lua_pushboolean( L, 0 );
			lua_pushstring( L, "Empty response body" );
			return 2;
		}

		// Create directories if needed
		if ( !fs::CreateDirectoriesRecursive( wFilePath ) ) {
			lua_pushboolean( L, 0 );
			lua_pushstring( L, "Failed to create directory" );
			return 2;
		}

		// Save file
		bool success = fs::SaveToFile( wFilePath, reinterpret_cast<const BYTE *>( bodyData ), bodySize );
		if ( success ) {
			lua_pushboolean( L, 1 );
			lua_pushnil( L );
		}

		else {
			lua_pushboolean( L, 0 );

			// Get Windows error
			DWORD error = GetLastError();
			char errorMsg[256];
			snprintf( errorMsg, sizeof( errorMsg ), "Failed to save file (Error %lu)", error );
			lua_pushstring( L, errorMsg );
		}

		return 2;
	}



	int fetch_send( lua_State *L ) {
		auto ctx = GetFetchContext( L, 1 );

		// Sempre prepara para retornar self
		lua_pushvalue( L, 1 ); // self na stack para retorno

		if ( !ctx )
			return 1; // Retorna self (contexto inválido)


		std::lock_guard<std::mutex> lock( ctx->mutex );
		if ( ctx->threadActive )
			return 1; // Já está em andamento


		// Configura estado
		ctx->threadActive = true;
		ctx->completed = false;
		ctx->cancelled = false;
		ctx->response = core::FetchResponse();

		// Spawn thread
		try {
			std::thread( http::ExecuteFetchThread, ctx ).detach();
		} catch ( const std::exception &e ) {
			ctx->threadActive = false;
			ctx->completed = true;
			ctx->response.error = "Failed to start request: " + std::string( e.what() );
			ctx->response.status = core::FetchResponse::STATUS_THREAD_ERROR;
		}

		return 1;
	}



	int fetch_callback( lua_State *L ) {
		// Preserva self para retorno
		lua_pushvalue( L, 1 );
		auto ctx = GetFetchContext( L, 1 );
		if ( !ctx )
			return 1; // Retorna self


		std::lock_guard<std::mutex> lock( ctx->mutex );

		// Limpa callback anterior
		if ( ctx->refCallback != LUA_NOREF && ctx->rain && ctx->rain->L )
			luaL_unref( ctx->rain->L, LUA_REGISTRYINDEX, ctx->refCallback );


		if ( lua_isfunction( L, 2 ) ) {
			if ( ctx->rain && ctx->rain->L == L ) {
				lua_pushvalue( L, 2 );
				ctx->refCallback = luaL_ref( L, LUA_REGISTRYINDEX );
			}
		}

		else
			ctx->refCallback = LUA_NOREF;

		return 1;
	}



	int fetch_hasCompleted( lua_State *L ) {
		auto ctx = GetFetchContext( L, 1 );
		if ( !ctx ) {
			lua_pushboolean( L, 0 );
			return 1;
		}

		bool completed = false;
		{
			std::lock_guard<std::mutex> lock( ctx->mutex );
			completed = ctx->completed;
		}

		lua_pushboolean( L, completed ? 1 : 0 );
		return 1;
	}



	int fetch_dispatch( lua_State *L ) {
		auto ctx = GetFetchContext( L, 1 );
		lua_pushvalue( L, 1 ); // Retorna self

		if ( !ctx )
			return 1;

		bool fire = false;
		core::FetchResponse response;
		int refCallback = LUA_NOREF;

		{
			std::lock_guard<std::mutex> lock( ctx->mutex );
			if ( ctx->completed ) {
				fire = true;
				response = ctx->response;
				refCallback = ctx->refCallback;
				ctx->completed = false; // Reseta para evitar re-execução
			}
		}

		if ( fire && refCallback != LUA_NOREF && ctx->rain && ctx->rain->L == L ) {
			lua_rawgeti( L, LUA_REGISTRYINDEX, refCallback );

			if ( lua_isfunction( L, -1 ) ) {
				lua_rawgeti( L, LUA_REGISTRYINDEX, ctx->refSelfLua ); // self
				PushResponseTable( L, ctx->rain, response ); // response

				if ( lua_pcall( L, 2, 0, 0 ) != 0 ) {
					const char *err = lua_tostring( L, -1 );
					if ( err && ctx->rain ) {
						std::wstring werr = L"[RainJIT:Fetch] Callback error: " + utf8_to_wstring( err );
						// clang-format off
						#ifdef __RAINMETERAPI_H__
							RmLog( ctx->rain->rm, LOG_WARNING, werr.c_str() );
						#endif
						// clang-format on
					}
					lua_pop( L, 1 );
				}
			}

			else
				lua_pop( L, 1 ); // Remove não-função
		}

		return 1;
	}



	int fetch_cancel( lua_State *L ) {
		auto ctx = GetFetchContext( L, 1 );
		lua_pushvalue( L, 1 ); // Prepara retorno self

		if ( ctx ) {
			std::lock_guard<std::mutex> lock( ctx->mutex );
			ctx->cancelled = true;
			ctx->threadActive = false;
			ctx->completed = true;
			ctx->response.error = "Request cancelled";
			ctx->response.status = core::FetchResponse::STATUS_CANCELLED;
		}

		return 1;
	}



	/**
	 */
	static void requestOptions( lua_State *L, auto ctx ) {
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
				const char *key = lua_tostring( L, -2 );
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
			size_t len;
			const char *body = lua_tolstring( L, -1, &len );
			ctx->request.body.assign( body, body + len );
		}
		lua_pop( L, 1 );

		// Parse timeout
		lua_getfield( L, 2, "timeout" );
		if ( lua_isnumber( L, -1 ) ) {
			int timeout = static_cast<int>( lua_tointeger( L, -1 ) );
			if ( timeout < 1000 )
				timeout = 1000;
			ctx->request.timeout = timeout;
		}
		lua_pop( L, 1 );

		// Parse followRedirects
		ctx->request.followRedirects = true;
		lua_getfield( L, 2, "followRedirects" );
		if ( lua_isboolean( L, -1 ) )
			ctx->request.followRedirects = lua_toboolean( L, -1 ) != 0;
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

		// Register context
		ctx->refSelf = core::ContextRegistry::instance().registerContext( ctx );

		// Create Lua table for fetch object
		lua_newtable( L );

		// Store fetch ID
		lua_pushstring( L, "__fetch_id" );
		lua_pushinteger( L, ctx->refSelf );
		lua_rawset( L, -3 );

		// Create metatable with methods
		lua_newtable( L );
		lua_pushstring( L, "__index" );

		// Tabela de métodos usando luaL_Reg
		lua_newtable( L );

		// clang-format off
		static const struct luaL_Reg methods[] = {
			{ "send",         fetch_send         },
			{ "callback",     fetch_callback     },
			{ "hasCompleted", fetch_hasCompleted },
			{ "cancel",       fetch_cancel       },
			{ NULL, NULL }
		};
		// clang-format on

		for ( const luaL_Reg *m = methods; m->name; m++ ) {
			lua_pushcfunction( L, m->func );
			lua_setfield( L, -2, m->name );
		}

		lua_rawset( L, -3 ); // Define __index na metatable
		lua_setmetatable( L, -2 ); // Aplica metatable ao objeto

		// Store self reference in Lua registry
		lua_pushvalue( L, -1 );
		ctx->refSelfLua = luaL_ref( L, LUA_REGISTRYINDEX );

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

		// Execute directly on the current thread — no busy-wait, no worker thread.
		http::ExecuteFetchThread( ctx );

		core::ContextRegistry::instance().removeContext( ctx->refSelf );

		std::lock_guard<std::mutex> lock( ctx->mutex );
		PushResponseTable( L, rain, ctx->response );
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



	int luaopen_fetch( lua_State *L ) {
		auto rain = static_cast<Rain *>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );

		lua_newtable( L );

		// Método async
		lua_pushlightuserdata( L, rain );
		lua_pushcclosure( L, fetch_async, 1 );
		lua_setfield( L, -2, "async" );

		// Métodos síncronos
		const char *sync_methods[] = { "get", "post", "put", "patch", "delete", "head", "options" };
		for ( const char *method : sync_methods ) {
			lua_pushlightuserdata( L, rain );
			lua_pushstring( L, method );
			lua_pushcclosure( L, fetch_sync, 2 );
			lua_setfield( L, -2, method );
		}

		// __call metamethod (para fetch(url) estilo fetch.get)
		lua_newtable( L );
		lua_pushlightuserdata( L, rain );

		lua_pushcclosure( L, fetch_default, 1 );
		lua_setfield( L, -2, "__call" );

		lua_setmetatable( L, -2 );

		return 1;
	}



	void RegisterModule( lua_State *L, Rain *rain ) {
		// Cria a janela de notificação para esta medida
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
