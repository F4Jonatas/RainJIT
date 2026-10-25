/**
 * @file wininet.hpp
 * @brief HTTP client implementation using WinINet
 * @license GPL v2.0 License
 *
 * Provides an alternative HTTP driver based on the WinINet stack.
 *
 * WinINet inherits the IE/Edge session context (cookies, proxy settings,
 * authentication) from the current Windows user profile, making it suitable
 * for requests to sites that reject non-browser clients (e.g. Cloudflare,
 * Yahoo consent endpoints).
 *
 * @note Phase-specific timeouts (dnsTimeout, connectTimeout, sendTimeout,
 *       receiveTimeout) are not supported by WinINet and are ignored.
 *       Only the general `timeout` field is honoured.
 *
 * @see http.hpp for the default WinHTTP driver.
 */

#pragma once

#include "core.hpp"

namespace wininet {

	/**
	 * @brief Worker thread function that executes an HTTP request via WinINet.
	 *
	 * Mirrors the signature of http::ExecuteFetchThread so the two drivers
	 * are interchangeable from the perspective of fetch_send / fetch_sync.
	 *
	 * @param ctx Shared pointer to FetchContext carrying the request
	 *            configuration and response storage.
	 */
	void ExecuteFetchThread( std::shared_ptr<core::FetchContext> ctx );

} // namespace wininet
