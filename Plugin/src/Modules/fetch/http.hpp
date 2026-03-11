/**
 * @file http.hpp
 * @brief HTTP client implementation using WinHTTP
 * @license GPL v2.0 License
 *
 * Contains the actual HTTP request execution logic using WinHTTP API.
 */

#pragma once

#include "core.hpp"

namespace http {

	/**
	 * @brief Check if internet connection is available
	 * @return true if connected, false otherwise
	 */
	bool IsInternetConnected();


	/**
	 * @brief Worker thread function that executes HTTP request
	 * @param ctx Shared pointer to FetchContext
	 */
	void ExecuteFetchThread( std::shared_ptr<core::FetchContext> ctx );

} // namespace http
