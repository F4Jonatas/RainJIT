/**
 * @file http.cpp
 * @brief WinHTTP implementation for fetch module
 * @license GPL v2.0 License
 */

#include "http.hpp"
#include "lua.hpp"

#include <Includes/rain.hpp>
#include <RainmeterAPI.hpp>
#include <utils/strings.hpp>

#include <chrono>
#include <sstream>
#include <thread>
#include <windows.h>
#include <winhttp.h>



#pragma comment( lib, "winhttp.lib" )

#define FETCH_USER_AGENT L"RainJIT/1.0"
#define FETCH_USER_AGENT_A "RainJIT/1.0"

// Available since Windows 8.1. Defined here so older SDK headers still compile.
#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
	#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif



// WM_FETCH_COMPLETE is defined once in core.hpp (included through http.hpp).



namespace http {

	std::string WinHttpErrorToString( DWORD errorCode ) {
		// clang-format off
		switch ( errorCode ) {
			case ERROR_WINHTTP_NAME_NOT_RESOLVED: // 12007
				return "Cannot resolve hostname. Check URL or internet connection.";
			case ERROR_WINHTTP_CANNOT_CONNECT: // 12029
				return "Cannot connect to server. Server may be down.";
			case ERROR_WINHTTP_CONNECTION_ERROR: // 12030
				return "Connection error. Network may be unstable.";
			case ERROR_WINHTTP_TIMEOUT: // 12002
				return "Request timeout. Server not responding.";
			case ERROR_WINHTTP_INVALID_URL: // 12005
				return "Invalid URL format.";
			case ERROR_WINHTTP_INVALID_SERVER_RESPONSE: // 12152
				return "Invalid server response.";
			case ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED: // 12044
				return "Client certificate required.";
			case ERROR_WINHTTP_SECURE_FAILURE: // 12175
				return "SSL/TLS security error.";
			case ERROR_WINHTTP_AUTO_PROXY_SERVICE_ERROR: // 12178
				return "Proxy configuration error.";

			default:
				// Description taken from winhttp.dll's message table, plus the numeric code.
				return core::FormatNetError( errorCode, L"winhttp.dll" );
		}
		// clang-format on
	}



	/**
	 * @brief Closer registered through FetchContext::trackHandle() for WinHTTP handles.
	 *
	 * Closing a handle from another thread aborts any synchronous call that is
	 * blocked on it (the call fails with ERROR_WINHTTP_OPERATION_CANCELLED).
	 */
	static void CloseWinHttpHandle( void *handle ) {
		WinHttpCloseHandle( static_cast<HINTERNET>( handle ) );
	}



	/**
	 * @brief Throws the FetchError that matches a failed WinHTTP call.
	 *
	 * The status comes from the Win32 error code - never from searching the message.
	 *
	 * @param what          Prefix for the message ("Send failed: ").
	 * @param err           GetLastError() captured right after the failed call.
	 * @param timeoutStatus Status to report if the error means "timed out" in this phase.
	 * @param invalidResponseStatus Status to report for "invalid server response".
	 */
	[[noreturn]] static void ThrowWinHttpError( const char *what, DWORD err, int timeoutStatus, int invalidResponseStatus = core::FetchResponse::STATUS_NETWORK_ERROR ) {
		throw core::FetchError( core::StatusFromNetError( err, timeoutStatus, invalidResponseStatus ), std::string( what ) + WinHttpErrorToString( err ) );
	}



	/**
	 * @brief Throws the FetchError for a request that was stopped from outside
	 *        (cancelled by the user, or killed by the total-deadline watchdog).
	 */
	[[noreturn]] static void ThrowStopped( core::FetchContext &ctx ) {
		if ( ctx.cancelled.load() )
			throw core::FetchError( core::FetchResponse::STATUS_CANCELLED, "Request cancelled" );

		throw core::FetchError( core::FetchResponse::STATUS_NETWORK_ERROR, "Total operation timeout exceeded" );
	}



	void ExecuteFetchThread( std::shared_ptr<core::FetchContext> ctx ) {
		core::FetchResponse response;
		response.status = core::FetchResponse::STATUS_NETWORK_ERROR;
		response.error = "Request not completed";

		HINTERNET hSession = NULL;
		HINTERNET hConnect = NULL;
		HINTERNET hRequest = NULL;

		core::FetchRequest request;
		{
			std::lock_guard<std::mutex> lock( ctx->mutex );
			request = ctx->request;
		}

		LPCWSTR HTTPVersion = request.HTTPVersion ? L"HTTP/1.0" : NULL; // NULL = default (1.1)

		// TIMEOUT CONFIGURATION
		// If phase-specific timeouts are not set, fall back to the general timeout
		int dnsTimeout = request.dnsTimeout > 0 ? request.dnsTimeout : request.timeout;
		int connectTimeout = request.connectTimeout > 0 ? request.connectTimeout : request.timeout;
		int sendTimeout = request.sendTimeout > 0 ? request.sendTimeout : request.timeout;
		int receiveTimeout = request.receiveTimeout > 0 ? request.receiveTimeout : request.timeout;

		// Enforce reasonable minimums
		if ( dnsTimeout < 1000 )
			dnsTimeout = 5000; // DNS: minimum 5s
		if ( connectTimeout < 1000 )
			connectTimeout = 10000; // Connect: minimum 10s
		if ( sendTimeout < 1000 )
			sendTimeout = 30000; // Send: minimum 30s
		if ( receiveTimeout < 1000 )
			receiveTimeout = 60000; // Receive: minimum 60s

		// TOTAL TIMEOUT
		// Covers everything up to the arrival of the response headers (DNS, connect,
		// TLS, send, wait for the response). Reading the body is not limited by it:
		// there the receive timeout applies to every individual read, so large
		// downloads are never cut off just because they take long.
		auto totalTimeout = std::chrono::milliseconds( request.timeout );
		if ( totalTimeout.count() < 1000 )
			totalTimeout = std::chrono::milliseconds( 120000 ); // Fallback: 120s

		auto operationStart = std::chrono::steady_clock::now();
		auto totalDeadline = operationStart + totalTimeout;

		// Watchdog: when the deadline passes it closes the open handles, which
		// interrupts a blocked call right away instead of waiting for the call's own
		// (possibly much longer) timeout.
		core::DeadlineGuard deadline( ctx.get(), static_cast<DWORD>( totalTimeout.count() ) );

		try {
			if ( ctx->cancelled.load() )
				throw core::FetchError( core::FetchResponse::STATUS_CANCELLED, "Request cancelled before start" );

			// Parse URL using WinHttpCrackUrl - handles IPv6, credentials,
			// fragments, and custom ports correctly without manual string parsing.
			std::wstring url = request.url;

			// Normalize schemeless URLs
			if ( url.find( L"://" ) == std::wstring::npos ) {
				if ( url.size() >= 2 && url[0] == L'/' && url[1] == L'/' )
					url = L"http:" + url;
				else
					url = L"http://" + url;
			}

			// Optional: percent-encode spaces and non-ASCII characters (never the host).
			if ( request.onlyASCII )
				url = core::EncodeUrlToAscii( url );

			URL_COMPONENTS uc = {};
			uc.dwStructSize = sizeof( uc );

			// Set non-zero lengths to request allocation of each component.
			uc.dwSchemeLength = (DWORD)-1;
			uc.dwHostNameLength = (DWORD)-1;
			uc.dwUrlPathLength = (DWORD)-1;
			uc.dwExtraInfoLength = (DWORD)-1;
			uc.dwUserNameLength = (DWORD)-1;
			uc.dwPasswordLength = (DWORD)-1;

			if ( !WinHttpCrackUrl( url.c_str(), 0, 0, &uc ) ) {
				DWORD err = GetLastError();
				throw core::FetchError( core::FetchResponse::STATUS_INVALID_URL, "Invalid URL: " + WinHttpErrorToString( err ) );
			}

			// Extract components as std::wstring
			std::wstring host( uc.lpszHostName, uc.dwHostNameLength );
			std::wstring path;
			if ( uc.dwUrlPathLength > 0 )
				path.assign( uc.lpszUrlPath, uc.dwUrlPathLength );
			if ( uc.dwExtraInfoLength > 0 )
				path.append( uc.lpszExtraInfo, uc.dwExtraInfoLength );
			if ( path.empty() )
				path = L"/";

			// Extract Basic Auth credentials from URL (user:password@host).
			// WinHTTP does not send credentials from the URL automatically -
			// WinHttpSetCredentials must be called explicitly after WinHttpOpenRequest.
			std::wstring urlUser;
			std::wstring urlPassword;
			if ( uc.dwUserNameLength > 0 )
				urlUser.assign( uc.lpszUserName, uc.dwUserNameLength );
			if ( uc.dwPasswordLength > 0 )
				urlPassword.assign( uc.lpszPassword, uc.dwPasswordLength );

			INTERNET_PORT port = uc.nPort;
			DWORD flags = ( uc.nScheme == INTERNET_SCHEME_HTTPS ) ? WINHTTP_FLAG_SECURE : 0;

			// Debug log. The URL is sanitized: credentials, query string and fragment
			// (API keys, tokens) are never written to the log.
			std::wstring debugMsg = L"[RainJIT:Fetch] Starting request to: \n" + core::SanitizeUrlForLog( url );
#ifdef __RAINMETERAPI_H__
			if ( ctx->rainValid && ctx->rain && ctx->rain->rm )
				RmLog( ctx->rain->rm, LOG_DEBUG, debugMsg.c_str() );
#endif

			// Open session
			// AUTOMATIC_PROXY follows the user's proxy settings (per-user proxy, PAC
			// script, WPAD auto-detection); DEFAULT_PROXY only reads the machine-wide
			// WinHTTP proxy and so ignores most proxies configured in Windows.
			// clang-format off
			hSession = WinHttpOpen(
				FETCH_USER_AGENT,
				WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
				WINHTTP_NO_PROXY_NAME,
				WINHTTP_NO_PROXY_BYPASS,
				0
			);

			// Systems that do not know AUTOMATIC_PROXY (older than Windows 8.1) fail here.
			if ( !hSession ) {
				hSession = WinHttpOpen(
					FETCH_USER_AGENT,
					WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
					WINHTTP_NO_PROXY_NAME,
					WINHTTP_NO_PROXY_BYPASS,
					0
				);
			}
			// clang-format on

			if ( !hSession ) {
				DWORD err = GetLastError();
				throw core::FetchError( core::FetchResponse::STATUS_NETWORK_ERROR, "WinHttpOpen failed: " + core::FormatNetError( err, L"winhttp.dll" ) );
			}

			// Track the session so :cancel() can close it from another thread.
			// If the request was cancelled in the meantime the handle is already closed.
			if ( !ctx->trackHandle( hSession, CloseWinHttpHandle ) )
				ThrowStopped( *ctx );

			// Set modern TLS options
			// clang-format off
			DWORD secureProtocols =
				WINHTTP_FLAG_SECURE_PROTOCOL_TLS1 |
				WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 |
				WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 |
				WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;

			WinHttpSetOption(
				hSession,
				WINHTTP_OPTION_SECURE_PROTOCOLS,
				&secureProtocols,
				sizeof( secureProtocols )
			);

			// CONFIGURE PHASE-SPECIFIC TIMEOUTS
			WinHttpSetTimeouts( hSession,
				dnsTimeout, // DNS resolve timeout
				connectTimeout, // Connect timeout
				sendTimeout, // Send timeout
				receiveTimeout // Receive timeout
			);
			// clang-format on


			// Helper function to check the TOTAL deadline and external stops (safety net)
			auto checkTotalTimeout = [&]() {
				if ( ctx->cancelled.load() || ctx->timedOut.load() || std::chrono::steady_clock::now() > totalDeadline )
					ThrowStopped( *ctx );
			};

			checkTotalTimeout();


			// Connect
			hConnect = WinHttpConnect( hSession, host.c_str(), port, 0 );
			if ( !hConnect ) {
				DWORD err = GetLastError();
				if ( err == ERROR_WINHTTP_CANNOT_CONNECT || err == ERROR_WINHTTP_CONNECTION_ERROR || err == ERROR_WINHTTP_NAME_NOT_RESOLVED )
					throw core::FetchError( core::StatusFromNetError( err, core::FetchResponse::STATUS_TIMEOUT_CONNECT ), WinHttpErrorToString( err ) );

				ThrowWinHttpError( "WinHttpConnect failed: ", err, core::FetchResponse::STATUS_TIMEOUT_CONNECT );
			}

			if ( !ctx->trackHandle( hConnect, CloseWinHttpHandle ) )
				ThrowStopped( *ctx );

			checkTotalTimeout();

			// Open request
			hRequest = WinHttpOpenRequest( hConnect, request.method.c_str(), path.c_str(), HTTPVersion, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags );

			if ( !hRequest ) {
				DWORD err = GetLastError();
				ThrowWinHttpError( "WinHttpOpenRequest failed: ", err, core::FetchResponse::STATUS_TIMEOUT_CONNECT );
			}

			if ( !ctx->trackHandle( hRequest, CloseWinHttpHandle ) )
				ThrowStopped( *ctx );

			// Apply Basic Auth credentials extracted from the URL, if present.
			if ( !urlUser.empty() ) {
				WinHttpSetCredentials( hRequest, WINHTTP_AUTH_TARGET_SERVER, WINHTTP_AUTH_SCHEME_BASIC, urlUser.c_str(), urlPassword.c_str(), nullptr );
			}

			// Configure request options
			// Disable automatic decompression - WinHTTP's built-in decompressor
			// has known issues with chunked responses, causing WinHttpReadData to
			// return 0 bytes immediately. Request identity encoding so the server
			// sends uncompressed data, which WinHttpReadData handles correctly.
			// Header names are compared case-insensitively, as HTTP defines them.
			if ( core::FindHeader( request.headers, L"Accept-Encoding" ) == request.headers.end() ) {
				WinHttpAddRequestHeaders( hRequest, L"Accept-Encoding: identity", -1L, WINHTTP_ADDREQ_FLAG_ADD );
			}

			// Follow redirects, but never silently downgrade a secure (https) request to http.
			DWORD redirectPolicy = request.followRedirects ? WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP : WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
			WinHttpSetOption( hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof( redirectPolicy ) );

			// Add headers
			for ( const auto &header : request.headers ) {
				std::wstring hdr = header.first + L": " + header.second;
				WinHttpAddRequestHeaders( hRequest, hdr.c_str(), -1L, WINHTTP_ADDREQ_FLAG_ADD );
			}

			// Default headers (only when the caller did not supply their own)
			if ( core::FindHeader( request.headers, L"User-Agent" ) == request.headers.end() ) {
				WinHttpAddRequestHeaders( hRequest, L"User-Agent: " FETCH_USER_AGENT, -1L, WINHTTP_ADDREQ_FLAG_ADD );
			}

			if ( core::FindHeader( request.headers, L"Accept" ) == request.headers.end() ) {
				WinHttpAddRequestHeaders( hRequest, L"Accept: */*", -1L, WINHTTP_ADDREQ_FLAG_ADD );
			}
			checkTotalTimeout();

			// Send request
			// The body is sent straight from the local request copy (no extra copy).
			BOOL bResults;
			if ( request.body.empty() ) {
				bResults = WinHttpSendRequest( hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0 );
			} else {
				bResults = WinHttpSendRequest( hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, request.body.data(), static_cast<DWORD>( request.body.size() ), static_cast<DWORD>( request.body.size() ), 0 );
			}

			if ( !bResults ) {
				DWORD err = GetLastError();

				// WinHttpSendRequest covers DNS, connect, TLS and the upload itself, so a
				// timeout here is reported as a "connect" timeout, unless a body was being
				// uploaded, in which case it is a "send" timeout.
				int sendTimeoutStatus = request.body.empty() ? core::FetchResponse::STATUS_TIMEOUT_CONNECT : core::FetchResponse::STATUS_TIMEOUT_SEND;
				ThrowWinHttpError( "Send failed: ", err, sendTimeoutStatus );
			}

			checkTotalTimeout();

			// Receive response
			bResults = WinHttpReceiveResponse( hRequest, NULL );
			if ( !bResults ) {
				DWORD err = GetLastError();
				ThrowWinHttpError( "Receive response failed: ", err, core::FetchResponse::STATUS_TIMEOUT_RECEIVE );
			}

			checkTotalTimeout();

			// Get status code
			DWORD dwStatusCode = 0;
			DWORD dwSize = sizeof( dwStatusCode );
			if ( WinHttpQueryHeaders( hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX ) ) {
				response.status = static_cast<int>( dwStatusCode );
			} else {
				DWORD err = GetLastError();
				ThrowWinHttpError( "Failed to get HTTP status code: ", err, core::FetchResponse::STATUS_TIMEOUT_RECEIVE );
			}

			// Get headers
			DWORD headerSize = 0;
			WinHttpQueryHeaders( hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, NULL, &headerSize, WINHTTP_NO_HEADER_INDEX );

			if ( headerSize > 0 ) {
				std::vector<wchar_t> headerBuffer( headerSize / sizeof( wchar_t ) + 1 );
				if ( WinHttpQueryHeaders( hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, headerBuffer.data(), &headerSize, WINHTTP_NO_HEADER_INDEX ) ) {
					std::wstring headersStr( headerBuffer.data() );
					std::wistringstream stream( headersStr );
					std::wstring line;

					while ( std::getline( stream, line ) ) {
						if ( !line.empty() && line.back() == L'\r' ) {
							line.pop_back();
						}

						size_t colonPos = line.find( L':' );
						if ( colonPos != std::wstring::npos && colonPos > 0 ) {
							std::wstring key = line.substr( 0, colonPos );
							std::wstring value = line.substr( colonPos + 1 );

							// Trim whitespace
							while ( !value.empty() && ( value[0] == L' ' || value[0] == L'\t' ) ) {
								value.erase( 0, 1 );
							}
							while ( !value.empty() && ( value.back() == L' ' || value.back() == L'\t' ) ) {
								value.pop_back();
							}

							if ( !key.empty() && !value.empty() ) {
								std::string key_utf8 = wstring_to_utf8( key );
								std::string value_utf8 = wstring_to_utf8( value );

								// Handle cookies
								if ( key_utf8 == "Set-Cookie" || key_utf8 == "set-cookie" ) {
									size_t semicolon = value_utf8.find( ';' );
									std::string cookie_pair = value_utf8.substr( 0, semicolon );

									size_t equals = cookie_pair.find( '=' );
									if ( equals != std::string::npos ) {
										std::string cookie_name = cookie_pair.substr( 0, equals );
										std::string cookie_value = cookie_pair.substr( equals + 1 );
										response.cookies[cookie_name] = cookie_value;
									}
								}

								response.headers[key_utf8] = value_utf8;
							}
						}
					}
				}
			}

			// The response headers arrived: the total deadline has done its job. From
			// here on the body is limited only by the per-read receive timeout.
			deadline.stop();

			// Responses that never carry a body: HEAD, 1xx, 204 and 304. Reading them
			// would only return 0 bytes, which is not a failure.
			const bool noBody = request.method == L"HEAD" || response.status == 204 || response.status == 304 || ( response.status >= 100 && response.status < 200 );

			if ( !noBody ) {
				std::vector<BYTE> buffer;
				size_t totalBytes = 0;
				bool readFailed = false;
				DWORD readError = 0;

				// WinHttpQueryDataAvailable is intentionally avoided - it fails with
				// E_UNEXPECTED on chunked Transfer-Encoding in synchronous mode.
				// WinHttpReadData with a fixed buffer handles both chunked and
				// Content-Length responses: 0 bytes read signals genuine EOF.
				const DWORD CHUNK_SIZE = 16384;
				DWORD bytesRead = 0;

				do {
					size_t offset = buffer.size();
					buffer.resize( offset + CHUNK_SIZE );

					bytesRead = 0;
					if ( !WinHttpReadData( hRequest, buffer.data() + offset, CHUNK_SIZE, &bytesRead ) ) {
						readError = GetLastError();
						readFailed = true;
						buffer.resize( offset );
						break;
					}

					buffer.resize( offset + bytesRead );
					totalBytes += bytesRead;

				} while ( bytesRead > 0 && !ctx->cancelled.load() );

				// A cancel or a deadline expiry closed the handles under our feet: report that,
				// not a misleading read error.
				if ( ctx->cancelled.load() || ctx->timedOut.load() )
					ThrowStopped( *ctx );

				if ( readFailed ) {
					// Some servers close a chunked / unknown-length stream abruptly after
					// sending all the data. Data already received is therefore kept, unless
					// a Content-Length proves that the body is incomplete.
					size_t expected = 0;
					const bool truncated = totalBytes > 0 && core::GetContentLength( response, expected ) && totalBytes < expected;

					if ( totalBytes == 0 || truncated ) {
						// Never hide the failure behind an "ok" response with a missing body.
						ThrowWinHttpError( truncated ? "Response body truncated: " : "Failed to read response body: ", readError, core::FetchResponse::STATUS_TIMEOUT_RECEIVE, core::FetchResponse::STATUS_CHUNKED_ERROR );
					}

#ifdef __RAINMETERAPI_H__
					if ( ctx->rainValid && ctx->rain && ctx->rain->rm )
						RmLog( ctx->rain->rm, LOG_DEBUG, L"[RainJIT:Fetch] Connection closed before the end of the stream - keeping the data received so far" );
#endif
				}

				// Stored once: as text or as binary, never both.
				if ( totalBytes > 0 )
					core::StoreBody( response, std::move( buffer ) );
			}


			// Any HTTP response (2xx, 3xx, 4xx, 5xx) is a completed request: `error` is only
			// meaningful for negative statuses, so it is cleared here.
			if ( response.status > 0 )
				response.error.clear();

			// Log successful completion time
			auto operationEnd = std::chrono::steady_clock::now();
			auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>( operationEnd - operationStart ).count();

			std::wstring timeMsg = L"[RainJIT:Fetch] Request completed in " + std::to_wstring( totalMs ) + L"ms";
#ifdef __RAINMETERAPI_H__
			if ( ctx->rainValid && ctx->rain && ctx->rain->rm )
				RmLog( ctx->rain->rm, LOG_DEBUG, timeMsg.c_str() );
#endif

		} catch ( const core::FetchError &e ) {
			// Already classified: the status travels with the exception.
			response = core::FetchResponse();
			response.status = e.status;
			response.error = e.what();

#ifdef __RAINMETERAPI_H__
			if ( ctx->rainValid && ctx->rain && ctx->rain->rm )
				RmLog( ctx->rain->rm, LOG_DEBUG, ( L"[RainJIT:Fetch] Exception: " + utf8_to_wstring( e.what() ) ).c_str() );
#endif

		} catch ( const std::exception &e ) {
			response = core::FetchResponse();
			response.status = core::FetchResponse::STATUS_NETWORK_ERROR;
			response.error = e.what();

#ifdef __RAINMETERAPI_H__
			if ( ctx->rainValid && ctx->rain && ctx->rain->rm )
				RmLog( ctx->rain->rm, LOG_DEBUG, ( L"[RainJIT:Fetch] Exception: " + utf8_to_wstring( e.what() ) ).c_str() );
#endif

		} catch ( ... ) {
			response = core::FetchResponse();
			response.status = core::FetchResponse::STATUS_NETWORK_ERROR;
			response.error = "Unknown error";
		}

		// Cleanup - the handles are owned by the context. Any handle already closed by
		// :cancel() or by the deadline watchdog (from another thread) was removed from
		// its list, so nothing is ever closed twice.
		deadline.stop();
		ctx->closeHandles();
		hRequest = NULL;
		hConnect = NULL;
		hSession = NULL;

		// A cancelled request always ends as STATUS_CANCELLED, whatever error the
		// aborted network call produced (e.g. -1 "Send failed").
		if ( ctx->cancelled.load() ) {
			response = core::FetchResponse();
			response.status = core::FetchResponse::STATUS_CANCELLED;
			response.error = "Request cancelled";
		}

		// The watchdog closed the handles: whatever error that produced, the real
		// reason is the expired deadline.
		else if ( ctx->timedOut.load() && response.status < 0 ) {
			response = core::FetchResponse();
			response.status = core::FetchResponse::STATUS_NETWORK_ERROR;
			response.error = "Total operation timeout exceeded";
		}

		// Log completion
		if ( ctx->rainValid && ctx->rain && ctx->rain->rm ) {
			std::wstring statusMsg = L"[RainJIT:Fetch] Request completed with status: " + std::to_wstring( response.status );
			if ( !response.error.empty() )
				statusMsg += L" - Error: " + utf8_to_wstring( response.error );

#ifdef __RAINMETERAPI_H__
			if ( ctx->rainValid && ctx->rain && ctx->rain->rm )
				RmLog( ctx->rain->rm, LOG_DEBUG, statusMsg.c_str() );
#endif
		}

		// Store results
		{
			std::lock_guard<std::mutex> lock( ctx->mutex );
			ctx->response = std::move( response );
			ctx->threadActive = false;
			ctx->completed = true;
		}

		HWND hWnd = ctx->hNotifyWindow;
		if ( hWnd )
			PostMessage( hWnd, WM_FETCH_COMPLETE, static_cast<WPARAM>( ctx->refSelf ), 0 );
	}

} // namespace http
