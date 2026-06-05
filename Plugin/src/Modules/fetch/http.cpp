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
#define FETCH_USER_AGENT_A  "RainJIT/1.0"




// Defina um identificador único para a mensagem
#define WM_FETCH_COMPLETE ( WM_APP + 3 )



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

			default: {
				char buffer[256];
				snprintf( buffer, sizeof( buffer ), "WinHTTP error %lu. See Windows error documentation.", errorCode );
				return std::string( buffer );
			}
		}
		// clang-format on
	}



	void ExecuteFetchThread( std::shared_ptr<core::FetchContext> ctx ) {
		core::FetchResponse response;
		response.status = -1;
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
		// Se timeouts específicos não forem definidos, usa timeout geral
		int dnsTimeout = request.dnsTimeout > 0 ? request.dnsTimeout : request.timeout;
		int connectTimeout = request.connectTimeout > 0 ? request.connectTimeout : request.timeout;
		int sendTimeout = request.sendTimeout > 0 ? request.sendTimeout : request.timeout;
		int receiveTimeout = request.receiveTimeout > 0 ? request.receiveTimeout : request.timeout;

		// Garante mínimos razoáveis
		if ( dnsTimeout < 1000 )
			dnsTimeout = 5000; // DNS: mínimo 5s
		if ( connectTimeout < 1000 )
			connectTimeout = 10000; // Connect: mínimo 10s
		if ( sendTimeout < 1000 )
			sendTimeout = 30000; // Send: mínimo 30s
		if ( receiveTimeout < 1000 )
			receiveTimeout = 60000; // Receive: mínimo 60s

		// TIMEOUT TOTAL (backup safety)
		auto totalTimeout = std::chrono::milliseconds( request.timeout );
		if ( totalTimeout.count() < 1000 )
			totalTimeout = std::chrono::milliseconds( 120000 ); // Fallback: 120s

		auto operationStart = std::chrono::steady_clock::now();
		auto totalDeadline = operationStart + totalTimeout;

		try {
			if ( ctx->cancelled.load() )
				throw std::runtime_error( "Request cancelled before start" );

			// Parse URL using WinHttpCrackUrl — handles IPv6, credentials,
			// fragments, and custom ports correctly without manual string parsing.
			std::wstring url = request.url;

			// Normalize schemeless URLs
			if ( url.find( L"://" ) == std::wstring::npos ) {
				if ( url.size() >= 2 && url[0] == L'/' && url[1] == L'/' )
					url = L"http:" + url;
				else
					url = L"http://" + url;
			}

			URL_COMPONENTS uc = {};
			uc.dwStructSize = sizeof( uc );

			// Set non-zero lengths to request allocation of each component.
			uc.dwSchemeLength    = (DWORD)-1;
			uc.dwHostNameLength  = (DWORD)-1;
			uc.dwUrlPathLength   = (DWORD)-1;
			uc.dwExtraInfoLength = (DWORD)-1;
			uc.dwUserNameLength  = (DWORD)-1;
			uc.dwPasswordLength  = (DWORD)-1;

			if ( !WinHttpCrackUrl( url.c_str(), 0, 0, &uc ) ) {
				DWORD err = GetLastError();
				throw std::runtime_error( "Invalid URL: " + WinHttpErrorToString( err ) );
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
			// WinHTTP does not send credentials from the URL automatically —
			// WinHttpSetCredentials must be called explicitly after WinHttpOpenRequest.
			std::wstring urlUser;
			std::wstring urlPassword;
			if ( uc.dwUserNameLength > 0 )
				urlUser.assign( uc.lpszUserName, uc.dwUserNameLength );
			if ( uc.dwPasswordLength > 0 )
				urlPassword.assign( uc.lpszPassword, uc.dwPasswordLength );

			INTERNET_PORT port = uc.nPort;
			DWORD flags = ( uc.nScheme == INTERNET_SCHEME_HTTPS ) ? WINHTTP_FLAG_SECURE : 0;

			// Debug log
			std::wstring debugMsg = L"[RainJIT:Fetch] Starting request to: \n" + url;
#ifdef __RAINMETERAPI_H__
			if ( ctx->rainValid && ctx->rain && ctx->rain->rm )
				RmLog( ctx->rain->rm, LOG_DEBUG, debugMsg.c_str() );
#endif

			// Open session
			// clang-format off
			hSession = WinHttpOpen(
				FETCH_USER_AGENT,
				WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
				WINHTTP_NO_PROXY_NAME,
				WINHTTP_NO_PROXY_BYPASS,
				0
			);
			// clang-format on

			if ( !hSession ) {
				DWORD err = GetLastError();
				throw std::runtime_error( "WinHttpOpen failed: " + std::to_string( err ) );
			}

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

			if ( ctx->cancelled.load() )
				throw std::runtime_error( "Request cancelled during setup" );


			// Helper function to check TOTAL timeout (safety net)
			auto checkTotalTimeout = [&]() {
				if ( std::chrono::steady_clock::now() > totalDeadline )
					throw std::runtime_error( "Total operation timeout exceeded" );
				if ( ctx->cancelled.load() )
					throw std::runtime_error( "Request cancelled" );
			};


			// Connect
			hConnect = WinHttpConnect( hSession, host.c_str(), port, 0 );
			if ( !hConnect ) {
				DWORD err = GetLastError();
				if ( err == ERROR_WINHTTP_CANNOT_CONNECT || err == ERROR_WINHTTP_CONNECTION_ERROR || err == ERROR_WINHTTP_NAME_NOT_RESOLVED ) {
					throw std::runtime_error( WinHttpErrorToString( err ) );
				}
				throw std::runtime_error( "WinHttpConnect failed: " + WinHttpErrorToString( err ) );
			}

			checkTotalTimeout();

			// Open request
			hRequest = WinHttpOpenRequest( hConnect, request.method.c_str(), path.c_str(), HTTPVersion, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags );

			if ( !hRequest ) {
				DWORD err = GetLastError();
				throw std::runtime_error( "WinHttpOpenRequest failed: " + WinHttpErrorToString( err ) );
			}

			// Apply Basic Auth credentials extracted from the URL, if present.
			if ( !urlUser.empty() ) {
				WinHttpSetCredentials(
					hRequest,
					WINHTTP_AUTH_TARGET_SERVER,
					WINHTTP_AUTH_SCHEME_BASIC,
					urlUser.c_str(),
					urlPassword.c_str(),
					nullptr
				);
			}

			// Configure request options
			// Disable automatic decompression — WinHTTP's built-in decompressor
			// has known issues with chunked responses, causing WinHttpReadData to
			// return 0 bytes immediately. Request identity encoding so the server
			// sends uncompressed data, which WinHttpReadData handles correctly.
			if ( request.headers.find( L"Accept-Encoding" ) == request.headers.end() ) {
				WinHttpAddRequestHeaders( hRequest, L"Accept-Encoding: identity", -1L, WINHTTP_ADDREQ_FLAG_ADD );
			}

			DWORD redirectPolicy = request.followRedirects ? WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS : WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
			WinHttpSetOption( hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof( redirectPolicy ) );

			// Add headers
			for ( const auto &header : request.headers ) {
				std::wstring hdr = header.first + L": " + header.second;
				WinHttpAddRequestHeaders( hRequest, hdr.c_str(), -1L, WINHTTP_ADDREQ_FLAG_ADD );
			}

			// Default headers
			if ( request.headers.find( L"User-Agent" ) == request.headers.end() ) {
				WinHttpAddRequestHeaders( hRequest, L"User-Agent: " FETCH_USER_AGENT, -1L, WINHTTP_ADDREQ_FLAG_ADD );
			}

			// Always accept all content types
			WinHttpAddRequestHeaders( hRequest, L"Accept: */*", -1L, WINHTTP_ADDREQ_FLAG_ADD );
			checkTotalTimeout();

			// Send request
			BOOL bResults;
			if ( request.body.empty() ) {
				bResults = WinHttpSendRequest( hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0 );
			} else {
				std::vector<BYTE> bodyCopy = request.body;
				bResults = WinHttpSendRequest( hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, bodyCopy.data(), static_cast<DWORD>( bodyCopy.size() ), static_cast<DWORD>( bodyCopy.size() ), 0 );
			}

			if ( !bResults ) {
				DWORD err = GetLastError();
				throw std::runtime_error( "Send failed: " + WinHttpErrorToString( err ) );
			}

			checkTotalTimeout();

			// Receive response
			bResults = WinHttpReceiveResponse( hRequest, NULL );
			if ( !bResults ) {
				DWORD err = GetLastError();
				throw std::runtime_error( "Receive response failed: " + WinHttpErrorToString( err ) );
			}

			checkTotalTimeout();

			// Get status code
			DWORD dwStatusCode = 0;
			DWORD dwSize = sizeof( dwStatusCode );
			if ( WinHttpQueryHeaders( hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX ) ) {
				response.status = static_cast<int>( dwStatusCode );
			} else {
				DWORD err = GetLastError();
				throw std::runtime_error( "Failed to get HTTP status code: " + std::to_string( err ) );
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

			std::vector<BYTE> buffer;
			DWORD totalBytes = 0;
			bool readSuccess = false;

			// WinHttpQueryDataAvailable is intentionally avoided — it fails with
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
					DWORD err = GetLastError();
					buffer.resize( offset );

					if ( err == E_ABORT || err == ERROR_OPERATION_ABORTED ) {
						if ( totalBytes > 0 ) readSuccess = true;
					} else if ( err == ERROR_WINHTTP_CONNECTION_ERROR && totalBytes > 0 ) {
						readSuccess = true;
					} else if ( totalBytes > 0 ) {
						readSuccess = true;
					} else {
						response.error = "WinHttpReadData failed: " + WinHttpErrorToString( err );
					}
					break;
				}

				buffer.resize( offset + bytesRead );
				totalBytes += bytesRead;

				if ( bytesRead > 0 )
					readSuccess = true;

			} while ( bytesRead > 0 && !ctx->cancelled.load() );

			if ( readSuccess && totalBytes > 0 ) {
				response.body = std::move( buffer );
				response.text = std::string( reinterpret_cast<const char *>( response.body.data() ), response.body.size() );
			}

			else if ( !readSuccess ) {
#ifdef __RAINMETERAPI_H__
				if ( ctx->rainValid && ctx->rain && ctx->rain->rm )
					RmLog( ctx->rain->rm, LOG_ERROR, L"[RainJIT:Fetch] FAILED to read any data" );
#endif

				response.body.clear();

				DWORD lastErr = GetLastError();
				if ( lastErr == E_ABORT || lastErr == ERROR_OPERATION_ABORTED ) {
					response.status = core::FetchResponse::STATUS_ABORTED;
					response.error = "Request aborted - no data received";
				}
			}


			// Clear error only on 2xx/3xx — preserve error message on server-side failures.
			if ( response.status >= 200 && response.status < 400 )
				response.error.clear();

			// Log successful completion time
			auto operationEnd = std::chrono::steady_clock::now();
			auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>( operationEnd - operationStart ).count();

			std::wstring timeMsg = L"[RainJIT:Fetch] Request completed in " + std::to_wstring( totalMs ) + L"ms";
#ifdef __RAINMETERAPI_H__
			if ( ctx->rainValid && ctx->rain && ctx->rain->rm )
				RmLog( ctx->rain->rm, LOG_DEBUG, timeMsg.c_str() );
#endif

		} catch ( const std::exception &e ) {
			if ( response.body.empty() ) {
				response.status = -1;
				response.error = e.what();
				// response.body.clear();
				// response.text.clear();

				// Try to identify specific error by message
				std::string errMsg = e.what();
				if ( errMsg.find( "abort" ) != std::string::npos || errMsg.find( "cancelled" ) != std::string::npos ) {
					response.status = core::FetchResponse::STATUS_ABORTED;
				} else if ( errMsg.find( "SSL" ) != std::string::npos || errMsg.find( "TLS" ) != std::string::npos || errMsg.find( "secure" ) != std::string::npos ) {
					response.status = core::FetchResponse::STATUS_SSL_ERROR;
				} else if ( errMsg.find( "proxy" ) != std::string::npos ) {
					response.status = core::FetchResponse::STATUS_PROXY_ERROR;
				} else if ( errMsg.find( "DNS" ) != std::string::npos || errMsg.find( "resolve" ) != std::string::npos ) {
					response.status = core::FetchResponse::STATUS_TIMEOUT_DNS;
				} else if ( errMsg.find( "chunk" ) != std::string::npos ) {
					response.status = core::FetchResponse::STATUS_CHUNKED_ERROR;
				}
			}

#ifdef __RAINMETERAPI_H__
			if ( ctx->rainValid && ctx->rain && ctx->rain->rm )
				RmLog( ctx->rain->rm, LOG_DEBUG, ( L"[RainJIT:Fetch] Exception: " + utf8_to_wstring( e.what() ) ).c_str() );
#endif

		} catch ( ... ) {
			// Catch any other errors (including E_ABORT)
			// response.body.clear();
			// response.text.clear();

			if ( response.body.empty() ) {
				DWORD lastError = GetLastError();
				response.status = core::FetchResponse::STATUS_NETWORK_ERROR; // fallback

				// Map Windows error codes to our codes
				switch ( lastError ) {
				case E_ABORT:
				case ERROR_OPERATION_ABORTED:
					response.status = core::FetchResponse::STATUS_ABORTED;
					response.error = "Operation aborted (E_ABORT)";
					break;

				case ERROR_WINHTTP_CONNECTION_ERROR:
					response.status = core::FetchResponse::STATUS_CONNECTION_LOST;
					response.error = "Connection lost during transfer";
					break;

				case ERROR_WINHTTP_TIMEOUT:
					response.status = core::FetchResponse::STATUS_TIMEOUT_RECEIVE;
					response.error = "Operation timeout";
					break;

				case ERROR_WINHTTP_NAME_NOT_RESOLVED:
					response.status = core::FetchResponse::STATUS_TIMEOUT_DNS;
					response.error = "DNS name resolution failed";
					break;

				case ERROR_WINHTTP_CANNOT_CONNECT:
					response.status = core::FetchResponse::STATUS_TIMEOUT_CONNECT;
					response.error = "Cannot connect to server";
					break;

				case ERROR_WINHTTP_SECURE_FAILURE:
					response.status = core::FetchResponse::STATUS_SSL_ERROR;
					response.error = "SSL/TLS security failure";
					break;

				case ERROR_WINHTTP_AUTO_PROXY_SERVICE_ERROR:
					response.status = core::FetchResponse::STATUS_PROXY_ERROR;
					response.error = "Proxy configuration error";
					break;

				default: {
					// Try to get system error message
					LPVOID errorMsg = nullptr;
					DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;

					if ( FormatMessageA( flags, NULL, lastError, MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ), (LPSTR)&errorMsg, 0, NULL ) ) {
						response.error = (char *)errorMsg;
						LocalFree( errorMsg );
					} else {
						char buffer[256];
						snprintf( buffer, sizeof( buffer ), "System error code: %lu (0x%08lX)", lastError, lastError );
						response.error = buffer;
					}
					break;
				}
				}

#ifdef __RAINMETERAPI_H__
				if ( ctx->rainValid && ctx->rain && ctx->rain->rm )
					RmLog( ctx->rain->rm, LOG_DEBUG, ( L"[RainJIT:Fetch] System error: " + std::to_wstring( lastError ) + L" - " + utf8_to_wstring( response.error ) ).c_str() );
#endif
			}
		}

		// Cleanup
		if ( hRequest )
			WinHttpCloseHandle( hRequest );
		if ( hConnect )
			WinHttpCloseHandle( hConnect );
		if ( hSession )
			WinHttpCloseHandle( hSession );

		// Log completion
		if ( ctx && ctx->rain && ctx->rain->rm ) {
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
