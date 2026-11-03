/**
 * @file websocket.hpp
 * @brief Main header for the WebSocket module (server and ws:// client) - includes all components
 * @license GPL v2.0 License
 *
 * This is the main include file for the websocket module.
 * It includes all subcomponents and provides the public API.
 */

#pragma once



// Include all component headers
#include "websocket_core.hpp"
#include "websocket_network.hpp"
#include "websocket_lua.hpp"


/**
 * @namespace websocket
 * @brief WebSocket server and client module for RainJIT (plain Winsock, one thread per server / client)
 *
 * Call RegisterModule() when a skin is loaded, and on unload call
 * CleanupContexts() first and CleanupAutoDispatch() afterwards.
 */
namespace websocket {
	void CleanupAutoDispatch( Rain *rain );

	// Re-export commonly used functions
	using core::CleanupContexts;
	using lua::RegisterModule;
} // namespace websocket
