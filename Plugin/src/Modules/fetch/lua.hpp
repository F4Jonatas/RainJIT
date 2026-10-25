/**
 * @file lua.hpp
 * @brief Lua bindings for fetch module
 * @license GPL v2.0 License
 *
 * Contains all Lua-C++ binding functions for the fetch API.
 */



#pragma once

#include <Windows.h>

#include <lua.hpp>
#include <string>

#include "core.hpp"
#include <Utils/strings.hpp>


struct lua_State;



namespace lua {

	HWND GetNotifyWindow( Rain *rain );
	void DestroyNotifyWindow( Rain *rain );

	// Helpers shared with the notification window
	std::shared_ptr<core::FetchContext> GetFetchContext( lua_State *L, int idx );
	void PushResponse( lua_State *L, core::FetchResponse &&response );

	// Lua binding functions (methods of the request object)
	int fetch_send( lua_State *L );
	int fetch_hasCompleted( lua_State *L );
	int fetch_dispatch( lua_State *L );
	int fetch_cancel( lua_State *L );

	// Lua binding functions (module functions)
	int fetch_async( lua_State *L );
	int fetch_sync( lua_State *L );
	int fetch_default( lua_State *L );

	// Module registration
	int luaopen_fetch( lua_State *L );
	void RegisterModule( lua_State *L, Rain *rain );

} // namespace lua
