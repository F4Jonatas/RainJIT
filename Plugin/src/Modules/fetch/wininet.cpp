/**
 * @file wininet.cpp
 * @brief HTTP client implementation using WinINet - InternetOpenUrlW path.
 * @license GPL v2.0 License
 *
 * Uses InternetOpenUrlW (high-level WinINet API) instead of the low-level
 * InternetConnectW + HttpOpenRequestW + HttpSendRequestW chain.
 *
 * InternetOpenUrlW inherits the full IE/Edge session context (persistent
 * cookies, proxy settings, cached credentials), which is what Cloudflare
 * and similar bot-detection systems recognise as a legitimate browser client.
 * This mirrors exactly what Rainmeter's WebParser does internally.
 *
 * @note Only GET requests are supported via InternetOpenUrlW.
 *       POST/PUT/etc. require the low-level API and are not compatible
 *       with this driver - a warning is logged if a non-GET method is used.
 */

// WinINet must come before any other Windows headers.
#include <Windows.h>
#include <wininet.h>
#pragma comment( lib, "wininet.lib" )

#include "wininet.hpp"

#include <chrono>
#include <stdexcept>
#include <string>
#include <vector>

#include <Includes/rain.hpp>
#include <Utils/strings.hpp>

// Default User-Agent - matches a real Chrome browser so servers accept the request.
#define WININET_USER_AGENT L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"



namespace wininet {

	/**
	 * @brief Internal log helper.
	 */
	static void Log( std::shared_ptr<core::FetchContext> &ctx, int level, const wchar_t *msg ) {
#ifdef __RAINMETERAPI_H__
		if ( ctx->rainValid && ctx->rain && ctx->rain->rm )
			RmLog( ctx->rain->rm, level, msg );
#else
		(void)ctx;
		(void)level;
		(void)msg;
#endif
	}



	/**
	 * @brief Closer registered through FetchContext::trackHandle() for WinINet handles.
	 *
	 * Closing a handle from another thread aborts any synchronous call that is
	 * blocked on it (InternetOpenUrlW, InternetReadFile, ...).
	 */
	static void CloseWinInetHandle( void *handle ) {
		InternetCloseHandle( static_cast<HINTERNET>( handle ) );
	}



	/**
	 * @brief Throws the FetchError that matches a failed WinINet call.
	 *
	 * The status comes from the Win32 error code - never from searching the message.
	 */
	[[noreturn]] static void ThrowWinInetError( const char *what, DWORD err, int timeoutStatus, int invalidResponseStatus = core::FetchResponse::STATUS_NETWORK_ERROR ) {
		throw core::FetchError( core::StatusFromNetError( err, timeoutStatus, invalidResponseStatus ), std::string( what ) + core::FormatNetError( err, L"wininet.dll" ) );
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
		core::FetchRequest request;
		{
			std::lock_guard<std::mutex> lock( ctx->mutex );
			request = ctx->request;
		}

		core::FetchResponse response;

		auto operationStart = std::chrono::steady_clock::now();

		HINTERNET hInternet = nullptr;
		HINTERNET hUrl = nullptr;

		// ----------------------------------------------------------------
		// Validate: InternetOpenUrlW only supports GET natively.
		// ----------------------------------------------------------------
		if ( request.method != L"GET" && request.method != L"get" ) {
			Log( ctx, LOG_WARNING,
					 L"[RainJIT:Fetch] driver=\"wininet\" - only GET requests are supported "
					 L"via InternetOpenUrlW. Use driver=\"winhttp\" for POST/PUT/DELETE." );
		}

		// Warn about unsupported phase-specific timeouts.
		if ( request.dnsTimeout > 0 || request.connectTimeout > 0 || request.sendTimeout > 0 || request.receiveTimeout > 0 ) {
			Log( ctx, LOG_WARNING,
					 L"[RainJIT:Fetch] driver=\"wininet\" - phase-specific timeouts "
					 L"(dnsTimeout, connectTimeout, sendTimeout, receiveTimeout) are not "
					 L"supported and will be ignored. Use 'timeout' instead." );
		}

		// TOTAL TIMEOUT
		// Covers everything up to the arrival of the response headers. Reading the
		// body is limited only by the per-read receive timeout, so large downloads
		// are never cut off just because they take long.
		DWORD totalTimeout = static_cast<DWORD>( request.timeout );

		// Watchdog: when the deadline passes it closes the open handles, which
		// interrupts a blocked call right away.
		core::DeadlineGuard deadline( ctx.get(), totalTimeout );

		try {
			if ( ctx->cancelled.load() )
				throw core::FetchError( core::FetchResponse::STATUS_CANCELLED, "Request cancelled before start" );

			// ----------------------------------------------------------------
			// Open Internet session.
			//
			// INTERNET_OPEN_TYPE_PRECONFIG uses the system proxy settings,
			// exactly as Rainmeter's WebParser does.
			// The User-Agent is passed here and inherited by InternetOpenUrlW.
			// ----------------------------------------------------------------
			const wchar_t *userAgent = WININET_USER_AGENT;

			// If the caller supplied a custom User-Agent header (any letter case), use it.
			auto uaIt = core::FindHeader( request.headers, L"User-Agent" );
			std::wstring customUA;
			if ( uaIt != request.headers.end() ) {
				customUA = uaIt->second;
				userAgent = customUA.c_str();
			}

			hInternet = InternetOpenW( userAgent, INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0 );

			if ( !hInternet ) {
				DWORD err = GetLastError();
				throw core::FetchError( core::FetchResponse::STATUS_NETWORK_ERROR, "InternetOpenW failed: " + core::FormatNetError( err, L"wininet.dll" ) );
			}

			// Track the session so :cancel() can close it from another thread.
			// If the request was cancelled in the meantime the handle is already closed.
			if ( !ctx->trackHandle( hInternet, CloseWinInetHandle ) )
				ThrowStopped( *ctx );

			// Apply general timeout to all phases.
			DWORD timeout = static_cast<DWORD>( request.timeout );
			InternetSetOptionW( hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof( timeout ) );
			InternetSetOptionW( hInternet, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof( timeout ) );
			InternetSetOptionW( hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof( timeout ) );

			if ( ctx->cancelled.load() || ctx->timedOut.load() )
				ThrowStopped( *ctx );

			// ----------------------------------------------------------------
			// Build additional headers string.
			//
			// InternetOpenUrlW accepts a raw header block (key: value\r\n).
			// Skip User-Agent - already passed to InternetOpenW above.
			// ----------------------------------------------------------------
			std::wstring extraHeaders;
			for ( const auto &[key, value] : request.headers ) {
				if ( _wcsicmp( key.c_str(), L"User-Agent" ) == 0 )
					continue;
				extraHeaders += key + L": " + value + L"\r\n";
			}

			// ----------------------------------------------------------------
			// Flags - mirror what WebParser uses.
			//
			// INTERNET_FLAG_RELOAD       - bypass cache, always fetch fresh.
			// INTERNET_FLAG_NO_CACHE_WRITE - do not store response in cache.
			// INTERNET_FLAG_SECURE       - enforce HTTPS when scheme is https.
			// INTERNET_FLAG_NO_AUTO_REDIRECT - honour followRedirects setting.
			// ----------------------------------------------------------------
			DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;

			if ( !request.followRedirects )
				flags |= INTERNET_FLAG_NO_AUTO_REDIRECT;

			// InternetOpenUrlW handles HTTPS automatically - no explicit flag needed.

			// Optional: percent-encode spaces and non-ASCII characters (never the host).
			std::wstring url = request.onlyASCII ? core::EncodeUrlToAscii( request.url ) : request.url;

			// ----------------------------------------------------------------
			// Open URL - the single high-level call that does everything.
			// This is what WebParser calls and what Cloudflare accepts.
			// ----------------------------------------------------------------
			hUrl = InternetOpenUrlW( hInternet, url.c_str(), extraHeaders.empty() ? nullptr : extraHeaders.c_str(), extraHeaders.empty() ? 0 : static_cast<DWORD>( extraHeaders.size() ), flags, 0 );

			if ( !hUrl ) {
				DWORD err = GetLastError();

				// A closed handle (cancel / deadline) is the real reason, not the error it caused.
				if ( ctx->cancelled.load() || ctx->timedOut.load() )
					ThrowStopped( *ctx );

				ThrowWinInetError( "InternetOpenUrlW failed: ", err, core::FetchResponse::STATUS_TIMEOUT_CONNECT );
			}

			if ( !ctx->trackHandle( hUrl, CloseWinInetHandle ) )
				ThrowStopped( *ctx );

			if ( ctx->cancelled.load() || ctx->timedOut.load() )
				ThrowStopped( *ctx );

			// ----------------------------------------------------------------
			// Read HTTP status code.
			// ----------------------------------------------------------------
			DWORD statusCode = 0;
			DWORD statusSize = sizeof( statusCode );
			HttpQueryInfoW( hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &statusSize, nullptr );
			response.status = static_cast<int>( statusCode );

			// ----------------------------------------------------------------
			// Read response headers.
			// ----------------------------------------------------------------
			DWORD headerSize = 0;
			HttpQueryInfoW( hUrl, HTTP_QUERY_RAW_HEADERS_CRLF, nullptr, &headerSize, nullptr );

			if ( headerSize > 0 ) {
				std::vector<wchar_t> headerBuf( headerSize / sizeof( wchar_t ) + 1 );

				if ( HttpQueryInfoW( hUrl, HTTP_QUERY_RAW_HEADERS_CRLF, headerBuf.data(), &headerSize, nullptr ) ) {
					const wchar_t *p = headerBuf.data();
					const wchar_t *end = p + wcslen( p );

					while ( p < end ) {
						const wchar_t *lineEnd = wcschr( p, L'\n' );
						if ( !lineEnd )
							lineEnd = end;

						std::wstring line( p, lineEnd );

						// Trim \r\n
						while ( !line.empty() && ( line.back() == L'\r' || line.back() == L'\n' ) )
							line.pop_back();

						auto pos = line.find( L':' );
						if ( pos != std::wstring::npos ) {
							std::wstring hKey = line.substr( 0, pos );
							std::wstring hVal = line.substr( pos + 1 );

							// Trim leading space from value
							if ( !hVal.empty() && hVal.front() == L' ' )
								hVal.erase( 0, 1 );

							if ( !hKey.empty() )
								response.headers[wstring_to_utf8( hKey )] = wstring_to_utf8( hVal );
						}

						p = lineEnd + 1;
					}
				}
			}

			// Extract Set-Cookie headers.
			{
				DWORD cookieIndex = 0;
				DWORD cookieBufSize = 0;

				while ( true ) {
					cookieBufSize = 0;
					HttpQueryInfoW( hUrl, HTTP_QUERY_SET_COOKIE, nullptr, &cookieBufSize, &cookieIndex );
					if ( cookieBufSize == 0 )
						break;

					std::vector<wchar_t> cookieBuf( cookieBufSize / sizeof( wchar_t ) + 1 );

					if ( HttpQueryInfoW( hUrl, HTTP_QUERY_SET_COOKIE, cookieBuf.data(), &cookieBufSize, &cookieIndex ) ) {
						std::wstring cookieLine( cookieBuf.data() );
						auto semi = cookieLine.find( L';' );
						std::wstring pair = cookieLine.substr( 0, semi );
						auto eq = pair.find( L'=' );
						if ( eq != std::wstring::npos ) {
							std::wstring cKey = pair.substr( 0, eq );
							std::wstring cVal = pair.substr( eq + 1 );
							response.cookies[wstring_to_utf8( cKey )] = wstring_to_utf8( cVal );
						}
					}

					cookieIndex++;
				}
			}

			// The response headers arrived: the total deadline has done its job. From
			// here on the body is limited only by the per-read receive timeout.
			deadline.stop();

			if ( ctx->cancelled.load() || ctx->timedOut.load() )
				ThrowStopped( *ctx );

			// ----------------------------------------------------------------
			// Read body.
			//
			// Responses without a body (204, 304, ...) simply read 0 bytes, which is
			// not a failure; an actual read error is reported, never hidden.
			// ----------------------------------------------------------------
			std::vector<BYTE> body;
			body.reserve( 65536 );

			BYTE chunk[8192];
			DWORD bytesRead = 0;
			bool readFailed = false;
			DWORD readError = 0;

			while ( true ) {
				bytesRead = 0;

				if ( !InternetReadFile( hUrl, chunk, sizeof( chunk ), &bytesRead ) ) {
					readError = GetLastError();
					readFailed = true;
					break;
				}

				if ( bytesRead == 0 )
					break; // genuine end of the response

				if ( ctx->cancelled.load() )
					ThrowStopped( *ctx );

				body.insert( body.end(), chunk, chunk + bytesRead );
			}

			// A cancel or a deadline expiry closed the handles under our feet: report that,
			// not a misleading read error.
			if ( ctx->cancelled.load() || ctx->timedOut.load() )
				ThrowStopped( *ctx );

			if ( readFailed ) {
				// Data already received is kept when the stream was simply cut off, unless
				// a Content-Length proves that the body is incomplete.
				size_t expected = 0;
				const bool truncated = !body.empty() && core::GetContentLength( response, expected ) && body.size() < expected;

				if ( body.empty() || truncated )
					ThrowWinInetError( truncated ? "Response body truncated: " : "Failed to read response body: ", readError, core::FetchResponse::STATUS_TIMEOUT_RECEIVE, core::FetchResponse::STATUS_CHUNKED_ERROR );

				Log( ctx, LOG_DEBUG, L"[RainJIT:Fetch] Connection closed before the end of the stream - keeping the data received so far" );
			}

			// Stored once: as text or as binary, never both.
			if ( !body.empty() )
				core::StoreBody( response, std::move( body ) );

			// Any HTTP response (2xx, 3xx, 4xx, 5xx) is a completed request: `error` is only
			// meaningful for negative statuses.
			if ( response.status > 0 )
				response.error.clear();

		} catch ( const core::FetchError &e ) {
			// Already classified: the status travels with the exception.
			response = core::FetchResponse();
			response.status = e.status;
			response.error = e.what();
		} catch ( const std::exception &e ) {
			response = core::FetchResponse();
			response.status = core::FetchResponse::STATUS_NETWORK_ERROR;
			response.error = e.what();
		} catch ( ... ) {
			response = core::FetchResponse();
			response.status = core::FetchResponse::STATUS_NETWORK_ERROR;
			response.error = "Unknown error";
		}

		// ----------------------------------------------------------------
		// Cleanup
		// ----------------------------------------------------------------
		// The handles are owned by the context. Any handle already closed by :cancel()
		// or by the deadline watchdog (from another thread) was removed from its list,
		// so nothing is closed twice.
		deadline.stop();
		ctx->closeHandles();
		hUrl = nullptr;
		hInternet = nullptr;

		// A cancelled request always ends as STATUS_CANCELLED, whatever error the
		// aborted network call produced.
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

		// Single completion log
		auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() - operationStart ).count();

#ifdef __RAINMETERAPI_H__
		if ( ctx->rainValid && ctx->rain && ctx->rain->rm ) {
			std::wstring msg = L"[RainJIT:Fetch] Completed in " + std::to_wstring( totalMs ) + L"ms | status: " + std::to_wstring( response.status ) + L" | driver: wininet";

			if ( !response.error.empty() )
				msg += L" | error: " + utf8_to_wstring( response.error );

			RmLog( ctx->rain->rm, LOG_DEBUG, msg.c_str() );
		}
#endif

		// ----------------------------------------------------------------
		// Store the result and notify Lua via window message
		// ----------------------------------------------------------------
		{
			std::lock_guard<std::mutex> lock( ctx->mutex );
			ctx->response = std::move( response );
			ctx->threadActive.store( false );
			ctx->completed.store( true );
		}

		if ( ctx->hNotifyWindow && IsWindow( ctx->hNotifyWindow ) )
			PostMessage( ctx->hNotifyWindow, WM_FETCH_COMPLETE, static_cast<WPARAM>( ctx->refSelf ), 0 );
	}

} // namespace wininet
