/**
 * @file fetch.hpp
 * @brief Main header for HTTP fetch module - includes all components
 * @license GPL v2.0 License
 *
 * This is the main include file for the fetch module.
 * It includes all subcomponents and provides the public API.
 */

#pragma once



// Include all component headers
#include "core.hpp"
#include "http.hpp"
#include "lua.hpp"



/**
 * @namespace fetch
 * @brief HTTP client module for RainJIT with async support
 *
 * This module provides asynchronous HTTP request capabilities
 * using WinHTTP with worker threads and Lua bindings.
 */
namespace fetch {
	void CleanupAutoDispatch( Rain *rain );

	// Re-export commonly used functions
	using core::CleanupContexts;
	using lua::RegisterModule;
} // namespace fetch
