/**
 * @file wininet.cpp
 * @brief HTTP client implementation using WinINet — InternetOpenUrlW path.
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
 *       with this driver — a warning is logged if a non-GET method is used.
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

// Default User-Agent — matches a real Chrome browser so servers accept the request.
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



	void ExecuteFetchThread( std::shared_ptr<core::FetchContext> ctx ) {
		auto &request  = ctx->request;
		auto &response = ctx->response;

		auto operationStart = std::chrono::steady_clock::now();

		HINTERNET hInternet = nullptr;
		HINTERNET hUrl      = nullptr;

		// ----------------------------------------------------------------
		// Validate: InternetOpenUrlW only supports GET natively.
		// ----------------------------------------------------------------
		if ( request.method != L"GET" && request.method != L"get" ) {
			Log( ctx, LOG_WARNING,
			     L"[RainJIT:Fetch] driver=\"wininet\" — only GET requests are supported "
			     L"via InternetOpenUrlW. Use driver=\"winhttp\" for POST/PUT/DELETE." );
		}

		// Warn about unsupported phase-specific timeouts.
		if ( request.dnsTimeout > 0 || request.connectTimeout > 0 ||
		     request.sendTimeout > 0 || request.receiveTimeout > 0 ) {
			Log( ctx, LOG_WARNING,
			     L"[RainJIT:Fetch] driver=\"wininet\" — phase-specific timeouts "
			     L"(dnsTimeout, connectTimeout, sendTimeout, receiveTimeout) are not "
			     L"supported and will be ignored. Use 'timeout' instead." );
		}

		try {
			if ( ctx->cancelled.load() )
				throw std::runtime_error( "Request cancelled before start" );

			// ----------------------------------------------------------------
			// Open Internet session.
			//
			// INTERNET_OPEN_TYPE_PRECONFIG uses the system proxy settings,
			// exactly as Rainmeter's WebParser does.
			// The User-Agent is passed here and inherited by InternetOpenUrlW.
			// ----------------------------------------------------------------
			const wchar_t *userAgent = WININET_USER_AGENT;

			// If the caller supplied a custom User-Agent header, use it.
			auto uaIt = request.headers.find( L"User-Agent" );
			std::wstring customUA;
			if ( uaIt != request.headers.end() ) {
				customUA  = uaIt->second;
				userAgent = customUA.c_str();
			}

			hInternet = InternetOpenW(
				userAgent,
				INTERNET_OPEN_TYPE_PRECONFIG,
				nullptr,
				nullptr,
				0
			);

			if ( !hInternet )
				throw std::runtime_error( "InternetOpenW failed: " + std::to_string( GetLastError() ) );

			// Apply general timeout to all phases.
			DWORD timeout = static_cast<DWORD>( request.timeout );
			InternetSetOptionW( hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof( timeout ) );
			InternetSetOptionW( hInternet, INTERNET_OPTION_SEND_TIMEOUT,    &timeout, sizeof( timeout ) );
			InternetSetOptionW( hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof( timeout ) );

			if ( ctx->cancelled.load() )
				throw std::runtime_error( "Request cancelled" );

			// ----------------------------------------------------------------
			// Build additional headers string.
			//
			// InternetOpenUrlW accepts a raw header block (key: value\r\n).
			// Skip User-Agent — already passed to InternetOpenW above.
			// ----------------------------------------------------------------
			std::wstring extraHeaders;
			for ( const auto &[key, value] : request.headers ) {
				if ( key == L"User-Agent" )
					continue;
				extraHeaders += key + L": " + value + L"\r\n";
			}

			// ----------------------------------------------------------------
			// Flags — mirror what WebParser uses.
			//
			// INTERNET_FLAG_RELOAD       — bypass cache, always fetch fresh.
			// INTERNET_FLAG_NO_CACHE_WRITE — do not store response in cache.
			// INTERNET_FLAG_SECURE       — enforce HTTPS when scheme is https.
			// INTERNET_FLAG_NO_AUTO_REDIRECT — honour followRedirects setting.
			// ----------------------------------------------------------------
			DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;

			if ( !request.followRedirects )
				flags |= INTERNET_FLAG_NO_AUTO_REDIRECT;

			// InternetOpenUrlW handles HTTPS automatically — no explicit flag needed.

			// ----------------------------------------------------------------
			// Open URL — the single high-level call that does everything.
			// This is what WebParser calls and what Cloudflare accepts.
			// ----------------------------------------------------------------
			hUrl = InternetOpenUrlW(
				hInternet,
				request.url.c_str(),
				extraHeaders.empty() ? nullptr : extraHeaders.c_str(),
				extraHeaders.empty() ? 0       : static_cast<DWORD>( extraHeaders.size() ),
				flags,
				0
			);

			if ( !hUrl ) {
				DWORD err = GetLastError();
				throw std::runtime_error( "InternetOpenUrlW failed: " + std::to_string( err ) );
			}

			if ( ctx->cancelled.load() )
				throw std::runtime_error( "Request cancelled" );

			// ----------------------------------------------------------------
			// Read HTTP status code.
			// ----------------------------------------------------------------
			DWORD statusCode = 0;
			DWORD statusSize = sizeof( statusCode );
			HttpQueryInfoW(
				hUrl,
				HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
				&statusCode,
				&statusSize,
				nullptr
			);
			response.status = static_cast<int>( statusCode );

			// ----------------------------------------------------------------
			// Read response headers.
			// ----------------------------------------------------------------
			DWORD headerSize = 0;
			HttpQueryInfoW( hUrl, HTTP_QUERY_RAW_HEADERS_CRLF, nullptr, &headerSize, nullptr );

			if ( headerSize > 0 ) {
				std::vector<wchar_t> headerBuf( headerSize / sizeof( wchar_t ) + 1 );

				if ( HttpQueryInfoW( hUrl, HTTP_QUERY_RAW_HEADERS_CRLF, headerBuf.data(), &headerSize, nullptr ) ) {
					const wchar_t *p   = headerBuf.data();
					const wchar_t *end = p + wcslen( p );

					while ( p < end ) {
						const wchar_t *lineEnd = wcschr( p, L'\n' );
						if ( !lineEnd ) lineEnd = end;

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
				DWORD cookieIndex   = 0;
				DWORD cookieBufSize = 0;

				while ( true ) {
					cookieBufSize = 0;
					HttpQueryInfoW( hUrl, HTTP_QUERY_SET_COOKIE, nullptr, &cookieBufSize, &cookieIndex );
					if ( cookieBufSize == 0 ) break;

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

			if ( ctx->cancelled.load() )
				throw std::runtime_error( "Request cancelled" );

			// ----------------------------------------------------------------
			// Read body.
			// ----------------------------------------------------------------
			std::vector<BYTE> body;
			body.reserve( 65536 );

			BYTE  chunk[8192];
			DWORD bytesRead = 0;

			while ( InternetReadFile( hUrl, chunk, sizeof( chunk ), &bytesRead ) && bytesRead > 0 ) {
				if ( ctx->cancelled.load() )
					throw std::runtime_error( "Request cancelled during read" );

				body.insert( body.end(), chunk, chunk + bytesRead );
			}

			if ( !body.empty() )
				response.body = std::move( body );

			if ( response.status >= 200 && response.status < 400 )
				response.error.clear();

		} catch ( const std::exception &e ) {
			if ( response.status == 0 )
				response.status = core::FetchResponse::STATUS_NETWORK_ERROR;
			response.error = e.what();
		} catch ( ... ) {
			if ( response.status == 0 )
				response.status = core::FetchResponse::STATUS_NETWORK_ERROR;
			response.error = "Unknown error";
		}

		// ----------------------------------------------------------------
		// Cleanup
		// ----------------------------------------------------------------
		if ( hUrl )      InternetCloseHandle( hUrl );
		if ( hInternet ) InternetCloseHandle( hInternet );

		// Single completion log
		auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - operationStart
		).count();

#ifdef __RAINMETERAPI_H__
		if ( ctx->rainValid && ctx->rain && ctx->rain->rm ) {
			std::wstring msg =
				L"[RainJIT:Fetch] Completed in " + std::to_wstring( totalMs ) +
				L"ms | status: "                 + std::to_wstring( response.status ) +
				L" | driver: wininet";

			if ( !response.error.empty() )
				msg += L" | error: " + utf8_to_wstring( response.error );

			RmLog( ctx->rain->rm, LOG_DEBUG, msg.c_str() );
		}
#endif

		// ----------------------------------------------------------------
		// Notify Lua via window message
		// ----------------------------------------------------------------
		ctx->completed.store( true );
		ctx->threadActive.store( false );

		if ( ctx->hNotifyWindow && IsWindow( ctx->hNotifyWindow ) )
			PostMessage( ctx->hNotifyWindow, WM_FETCH_COMPLETE, static_cast<WPARAM>( ctx->refSelf ), 0 );
	}

} // namespace wininet
