/**
 * @file core.cpp
 * @brief Core implementation for fetch module
 * @license GPL v2.0 License
 */

#include "core.hpp"
#include <Includes/rain.hpp>
#include <RainmeterAPI.hpp>
#include <utils/strings.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <memory>






namespace core {

	// FetchContext implementation
	FetchContext::FetchContext( Rain *r ) :
		rain( r ) {
		if ( request.timeout < 1000 )
			request.timeout = 1000;
	}



	FetchContext::~FetchContext() {
		releaseLuaRefs();
	}



	void FetchContext::releaseLuaRefs() {
		// Nothing to release: do not touch `rain` at all. It may already be gone
		// when the last reference to this context is dropped.
		if ( refSelfLua == LUA_NOREF && refCallback == LUA_NOREF )
			return;

		if ( rain && rain->L ) {
			lua_State *L = rain->L;

			if ( refSelfLua != LUA_NOREF ) {
				luaL_unref( L, LUA_REGISTRYINDEX, refSelfLua );
				refSelfLua = LUA_NOREF;
			}

			if ( refCallback != LUA_NOREF ) {
				luaL_unref( L, LUA_REGISTRYINDEX, refCallback );
				refCallback = LUA_NOREF;
			}
		} else {
			refSelfLua = LUA_NOREF;
			refCallback = LUA_NOREF;
		}
	}



	// Closes a list of handles, children first (request -> connect -> session).
	static void CloseHandleList( std::vector<NetHandle> &handles ) {
		for ( auto it = handles.rbegin(); it != handles.rend(); ++it ) {
			if ( it->handle && it->closer )
				it->closer( it->handle );
		}

		handles.clear();
	}



	bool FetchContext::trackHandle( void *handle, void ( *closer )( void * ) ) {
		if ( !handle || !closer )
			return false;

		{
			std::lock_guard<std::mutex> lock( handleMutex );

			if ( !cancelled.load() && !timedOut.load() ) {
				NetHandle h{ handle, closer };
				netHandles.push_back( h );
				return true;
			}
		}

		// Cancelled (or timed out) before the handle could be tracked: close it right away so the
		// worker can never block on a handle that nobody is able to abort.
		closer( handle );
		return false;
	}



	void FetchContext::closeHandles() {
		std::vector<NetHandle> toClose;
		{
			std::lock_guard<std::mutex> lock( handleMutex );
			toClose.swap( netHandles );
		}

		// Closed outside the lock, so a slow close never blocks the thread that
		// is registering handles. Each handle belongs to exactly one list, so it
		// is never closed twice (cancel thread vs. worker thread).
		CloseHandleList( toClose );
	}



	void FetchContext::requestCancel() {
		std::vector<NetHandle> toClose;
		{
			std::lock_guard<std::mutex> lock( handleMutex );
			cancelled.store( true );
			toClose.swap( netHandles );
		}

		CloseHandleList( toClose );
	}



	void FetchContext::expireDeadline() {
		std::vector<NetHandle> toClose;
		{
			std::lock_guard<std::mutex> lock( handleMutex );
			timedOut.store( true );
			toClose.swap( netHandles );
		}

		CloseHandleList( toClose );
	}



	// DeadlineGuard implementation
	static VOID CALLBACK DeadlineTimerProc( PVOID param, BOOLEAN ) {
		static_cast<FetchContext *>( param )->expireDeadline();
	}



	DeadlineGuard::DeadlineGuard( FetchContext *ctx, DWORD milliseconds ) {
		// If the timer cannot be created the request simply runs without a watchdog
		// (the checkpoints between the steps still enforce the deadline).
		if ( !CreateTimerQueueTimer( &timer_, nullptr, DeadlineTimerProc, ctx, milliseconds, 0, WT_EXECUTEONLYONCE ) )
			timer_ = nullptr;
	}



	DeadlineGuard::~DeadlineGuard() {
		stop();
	}



	void DeadlineGuard::stop() {
		if ( timer_ ) {
			// INVALID_HANDLE_VALUE: block until a callback that is already running
			// has finished, so expireDeadline() can never run after this returns.
			DeleteTimerQueueTimer( nullptr, timer_, INVALID_HANDLE_VALUE );
			timer_ = nullptr;
		}
	}



	// ContextRegistry implementation
	ContextRegistry &ContextRegistry::instance() {
		static ContextRegistry instance;
		return instance;
	}



	int ContextRegistry::registerContext( std::shared_ptr<FetchContext> ctx ) {
		std::lock_guard<std::mutex> lock( mutex_ );
		int id = nextId_++;
		contexts_[id] = ctx;
		return id;
	}



	std::shared_ptr<FetchContext> ContextRegistry::getContext( int id ) {
		std::lock_guard<std::mutex> lock( mutex_ );
		auto it = contexts_.find( id );

		if ( it != contexts_.end() )
			return it->second;

		return nullptr;
	}



	void ContextRegistry::removeContext( int id ) {
		std::lock_guard<std::mutex> lock( mutex_ );
		contexts_.erase( id );
	}



	void ContextRegistry::removeAllByRain( Rain *rain ) {
		std::vector<std::shared_ptr<FetchContext>> removed;

		{
			std::lock_guard<std::mutex> lock( mutex_ );

			for ( auto it = contexts_.begin(); it != contexts_.end(); ) {
				if ( it->second->rain == rain ) {
					removed.push_back( it->second );
					it = contexts_.erase( it );
				}

				else
					++it;
			}
		}

		// Cancelled outside the registry lock: requestCancel() closes network
		// handles, which also aborts in-flight calls so the worker threads wind
		// down quickly instead of lingering after the skin was unloaded.
		for ( auto &ctx : removed ) {
			ctx->rainValid = false;
			ctx->requestCancel();
		}

		// The worker threads are detached and run code that lives in this DLL, so
		// give them a short, bounded time to finish before the DLL can be unloaded.
		// Their blocking calls were just aborted, so this normally takes milliseconds.
		const ULONGLONG waitUntil = GetTickCount64() + 3000;
		for ( auto &ctx : removed ) {
			while ( ctx->workerAlive.load() && GetTickCount64() < waitUntil )
				Sleep( 2 );
		}

		// Release the Lua references now, on the calling thread, while the Lua state
		// is still valid. Afterwards the destructor never has to touch `rain`.
		for ( auto &ctx : removed )
			ctx->releaseLuaRefs();
	}



	size_t ContextRegistry::count() {
		std::lock_guard<std::mutex> lock( mutex_ );
		return contexts_.size();
	}



	void CleanupContexts( Rain *rain ) {
		ContextRegistry::instance().removeAllByRain( rain );
	}



	// ------------------------------------------------------------------
	// Shared helpers
	// ------------------------------------------------------------------

	std::map<std::wstring, std::wstring>::const_iterator FindHeader( const std::map<std::wstring, std::wstring> &headers, const wchar_t *name ) {
		for ( auto it = headers.begin(); it != headers.end(); ++it ) {
			if ( _wcsicmp( it->first.c_str(), name ) == 0 )
				return it;
		}

		return headers.end();
	}



	int StatusFromNetError( DWORD err, int timeoutStatus, int invalidResponseStatus ) {
		// WinHTTP and WinINet share the same 12xxx numbering, so one table serves both.
		// clang-format off
		switch ( err ) {
			case 995:        // ERROR_OPERATION_ABORTED
			case 12017:      // OPERATION_CANCELLED
			case 0x80004004: // E_ABORT
				return FetchResponse::STATUS_ABORTED;

			case 12005:      // INVALID_URL
			case 12006:      // UNRECOGNIZED_SCHEME
				return FetchResponse::STATUS_INVALID_URL;

			case 12163:      // INTERNET_DISCONNECTED (no connection to the internet)
				return FetchResponse::STATUS_NO_INTERNET;

			case 12007:      // NAME_NOT_RESOLVED
				return FetchResponse::STATUS_TIMEOUT_DNS;

			case 12029:      // CANNOT_CONNECT
				return FetchResponse::STATUS_TIMEOUT_CONNECT;

			case 12030:      // CONNECTION_ERROR / CONNECTION_ABORTED
			case 12031:      // CONNECTION_RESET
				return FetchResponse::STATUS_CONNECTION_LOST;

			case 12002:      // TIMEOUT
				return timeoutStatus;

			case 12152:      // INVALID_SERVER_RESPONSE
				return invalidResponseStatus;

			case 12037:      // SECURE_CERT_DATE_INVALID
			case 12038:      // SECURE_CERT_CN_INVALID
			case 12044:      // CLIENT_AUTH_CERT_NEEDED
			case 12045:      // SECURE_INVALID_CA
			case 12055:      // SEC_CERT_ERRORS
			case 12057:      // SECURE_CERT_REV_FAILED
			case 12157:      // SECURE_CHANNEL_ERROR
			case 12169:      // SECURE_INVALID_CERT
			case 12170:      // SECURE_CERT_REVOKED
			case 12175:      // SECURE_FAILURE
			case 12179:      // SECURE_CERT_WRONG_USAGE
				return FetchResponse::STATUS_SSL_ERROR;

			case 12166:      // BAD_AUTO_PROXY_SCRIPT
			case 12167:      // UNABLE_TO_DOWNLOAD_SCRIPT
			case 12178:      // AUTO_PROXY_SERVICE_ERROR
			case 12180:      // AUTODETECTION_FAILED
				return FetchResponse::STATUS_PROXY_ERROR;

			default:
				return FetchResponse::STATUS_NETWORK_ERROR;
		}
		// clang-format on
	}



	std::string FormatNetError( DWORD err, const wchar_t *moduleName ) {
		HMODULE module = moduleName ? GetModuleHandleW( moduleName ) : nullptr;

		DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM;
		if ( module )
			flags |= FORMAT_MESSAGE_FROM_HMODULE;

		// Prefer English (stable, easy to search for); fall back to the user language.
		LPWSTR buffer = nullptr;
		DWORD length = FormatMessageW( flags, module, err, MAKELANGID( LANG_ENGLISH, SUBLANG_ENGLISH_US ), reinterpret_cast<LPWSTR>( &buffer ), 0, nullptr );
		if ( !length || !buffer ) {
			if ( buffer ) {
				LocalFree( buffer );
				buffer = nullptr;
			}

			length = FormatMessageW( flags, module, err, 0, reinterpret_cast<LPWSTR>( &buffer ), 0, nullptr );
		}

		std::wstring text;
		if ( length && buffer )
			text.assign( buffer, length );

		if ( buffer )
			LocalFree( buffer );

		// Trim the trailing CR/LF/space/period that FormatMessage appends.
		while ( !text.empty() && ( text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ' || text.back() == L'.' ) )
			text.pop_back();

		char tail[48];
		snprintf( tail, sizeof( tail ), " (error %lu)", static_cast<unsigned long>( err ) );

		if ( text.empty() )
			return std::string( "Network error" ) + tail;

		return wstring_to_utf8( text ) + tail;
	}



	// Returns the index just after the authority ("scheme://user@host:port") of a
	// URL, i.e. where the path / query / fragment starts. 0 if there is no scheme.
	static size_t AuthorityEnd( const std::wstring &url ) {
		size_t schemeEnd = url.find( L"://" );
		if ( schemeEnd == std::wstring::npos )
			return 0;

		size_t pos = url.find_first_of( L"/?#", schemeEnd + 3 );
		return pos == std::wstring::npos ? url.size() : pos;
	}



	std::wstring EncodeUrlToAscii( const std::wstring &url ) {
		const size_t start = AuthorityEnd( url );

		// Fast path: nothing to encode.
		bool needs = false;
		for ( size_t i = start; i < url.size() && !needs; i++ )
			needs = url[i] <= L' ' || url[i] >= 0x7F;

		if ( !needs )
			return url;

		// Work on UTF-8 bytes so every non-ASCII character becomes its %XX sequence.
		const std::string tail = wstring_to_utf8( url.substr( start ) );

		std::wstring out = url.substr( 0, start );
		out.reserve( url.size() + tail.size() );

		static const wchar_t hex[] = L"0123456789ABCDEF";
		for ( unsigned char c : tail ) {
			if ( c <= 0x20 || c >= 0x7F ) {
				out += L'%';
				out += hex[c >> 4];
				out += hex[c & 0x0F];
			}

			else
				out += static_cast<wchar_t>( c );
		}

		return out;
	}



	std::wstring SanitizeUrlForLog( const std::wstring &url ) {
		const size_t schemeEnd = url.find( L"://" );
		const size_t authStart = schemeEnd == std::wstring::npos ? 0 : schemeEnd + 3;

		size_t authEnd = url.find_first_of( L"/?#", authStart );
		if ( authEnd == std::wstring::npos )
			authEnd = url.size();

		std::wstring authority = url.substr( authStart, authEnd - authStart );

		// Drop "user:password@"
		size_t at = authority.rfind( L'@' );
		if ( at != std::wstring::npos )
			authority.erase( 0, at + 1 );

		// Keep the path, drop the fragment and hide the query string (API keys, tokens).
		std::wstring rest = url.substr( authEnd );

		size_t hash = rest.find( L'#' );
		if ( hash != std::wstring::npos )
			rest.erase( hash );

		bool hadQuery = false;
		size_t query = rest.find( L'?' );
		if ( query != std::wstring::npos ) {
			rest.erase( query );
			hadQuery = true;
		}

		std::wstring out = url.substr( 0, authStart ) + authority + rest;
		if ( hadQuery )
			out += L"?...";

		return out;
	}



	// Case-insensitive lookup in the response headers (keys keep the server's casing).
	static const std::string *FindResponseHeader( const FetchResponse &response, const char *name ) {
		for ( const auto &h : response.headers ) {
			if ( _stricmp( h.first.c_str(), name ) == 0 )
				return &h.second;
		}

		return nullptr;
	}



	bool GetContentLength( const FetchResponse &response, size_t &length ) {
		const std::string *value = FindResponseHeader( response, "Content-Length" );
		if ( !value || value->empty() )
			return false;

		char *end = nullptr;
		unsigned long long n = _strtoui64( value->c_str(), &end, 10 );
		if ( end == value->c_str() )
			return false;

		length = static_cast<size_t>( n );
		return true;
	}



	static bool EndsWith( const std::string &s, const char *suffix ) {
		size_t n = strlen( suffix );
		return s.size() >= n && s.compare( s.size() - n, n, suffix ) == 0;
	}



	// Decides whether a payload is text, from the Content-Type or, if there is none,
	// from the content itself.
	static bool LooksLikeText( const FetchResponse &response, const std::vector<BYTE> &data ) {
		const std::string *contentType = FindResponseHeader( response, "Content-Type" );

		if ( contentType && !contentType->empty() ) {
			// Media type only ("text/html; charset=utf-8" -> "text/html"), lowercase.
			std::string type = contentType->substr( 0, contentType->find( ';' ) );
			type.erase( std::remove_if( type.begin(), type.end(), []( unsigned char c ) { return std::isspace( c ) != 0; } ), type.end() );
			std::transform( type.begin(), type.end(), type.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );

			if ( type.compare( 0, 5, "text/" ) == 0 )
				return true;

			// clang-format off
			if ( EndsWith( type, "/json" )       || EndsWith( type, "+json" )          ||
			     EndsWith( type, "/xml" )        || EndsWith( type, "+xml" )           ||
			     EndsWith( type, "/javascript" ) || EndsWith( type, "/x-javascript" )  ||
			     EndsWith( type, "/ecmascript" ) || EndsWith( type, "/yaml" )          ||
			     EndsWith( type, "/x-yaml" )     || EndsWith( type, "/graphql" )       ||
			     EndsWith( type, "/x-www-form-urlencoded" ) )
				return true;
			// clang-format on

			return false;
		}

		// No Content-Type: inspect the first bytes. Text has no NUL and almost no
		// control characters.
		const size_t n = std::min<size_t>( data.size(), 512 );
		for ( size_t i = 0; i < n; i++ ) {
			BYTE c = data[i];
			if ( c == 0 )
				return false;
			if ( c < 0x20 && c != '\t' && c != '\n' && c != '\r' && c != '\f' )
				return false;
		}

		return true;
	}



	void StoreBody( FetchResponse &response, std::vector<BYTE> &&data ) {
		response.body.clear();
		response.text.clear();

		if ( data.empty() )
			return;

		if ( LooksLikeText( response, data ) ) {
			// One copy into the string, then the byte buffer is released.
			response.text.assign( reinterpret_cast<const char *>( data.data() ), data.size() );
			std::vector<BYTE>().swap( data );
		}

		else
			response.body = std::move( data );
	}
} // namespace core
