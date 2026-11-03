/**
 * @file websocket_lua.hpp
 * @brief Lua bindings for websocket module
 * @license GPL v2.0 License
 *
 * Contains all Lua-C++ binding functions for the websocket API
 * (server, connection and client objects).
 */



#pragma once

#include <Windows.h>

#include <lua.hpp>
#include <memory>
#include <string>

#include "websocket_core.hpp"


struct lua_State;



namespace websocket::lua {

	HWND GetNotifyWindow( Rain *rain );
	void CreateNotifyWindow( Rain *rain );
	void DestroyNotifyWindow( Rain *rain );

	// Helper shared with the notification window
	std::shared_ptr<core::Context> GetContext( lua_State *L, int idx );

	// Lua binding functions (methods of the server object)
	int server_listen( lua_State *L );
	int server_stop( lua_State *L );
	int server_on( lua_State *L );
	int server_publish( lua_State *L );
	int server_broadcast( lua_State *L );
	int server_connection( lua_State *L );
	int server_host( lua_State *L );
	int server_port( lua_State *L );
	int server_url( lua_State *L );
	int server_running( lua_State *L );
	int server_connections( lua_State *L );
	int server_dropped( lua_State *L );
	int server_dispatch( lua_State *L );

	// Dispatch of the client object (see websocket_connect)
	int client_dispatch( lua_State *L );

	// Lua binding functions (methods of the connection object)
	int connection_send( lua_State *L );
	int connection_close( lua_State *L );
	int connection_subscribe( lua_State *L );
	int connection_unsubscribe( lua_State *L );
	int connection_publish( lua_State *L );
	int connection_ip( lua_State *L );
	int connection_id( lua_State *L );
	int connection_isOpen( lua_State *L );
	int connection_setData( lua_State *L );
	int connection_getData( lua_State *L );

	// Lua binding functions (module functions)
	int websocket_server( lua_State *L );
	int websocket_connect( lua_State *L );

	// Module registration
	int luaopen_websocket( lua_State *L );
	void RegisterModule( lua_State *L, Rain *rain );

} // namespace websocket::lua
