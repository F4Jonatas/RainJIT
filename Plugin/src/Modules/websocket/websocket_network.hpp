/**
 * @file websocket_network.hpp
 * @brief WebSocket network thread (server or ws:// client) for the websocket module
 * @license GPL v2.0 License
 *
 * RFC 6455 server and client on plain Winsock (ws only, no external dependencies).
 */

#pragma once

#include "websocket_core.hpp"

namespace websocket::network {

	/**
	 * @brief Thread body of a server or a client (Config::isClient).
	 *
	 * A client connects asynchronously and reports the result with events.
	 * A server tries to listen (walking up the ports when one is busy), reports the result
	 * through Context::startState and then runs the loop until a STOP
	 * command arrives.
	 *
	 * @param ctx Shared pointer to Context
	 */
	void Run( std::shared_ptr<core::Context> ctx );

	/**
	 * @brief Queues a command for the server thread and wakes its loop. Any thread.
	 */
	void Post( core::Context &ctx, core::Command &&command );

	/**
	 * @brief Asks the server thread to close every connection and leave its loop. Any thread.
	 */
	void Stop( core::Context &ctx );

	/**
	 * @brief Wakes the server thread so it applies the queued commands. Any thread.
	 */
	void Wake( core::Context &ctx );

} // namespace websocket::network
