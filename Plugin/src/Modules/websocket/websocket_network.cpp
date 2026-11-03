/**
 * @file websocket_network.cpp
 * @brief WebSocket server and ws:// client (RFC 6455) on plain Winsock - no external dependencies
 * @license GPL v2.0 License
 *
 * One thread per server (or per client) runs a WSAPoll loop. Every socket and every Peer lives on
 * that thread and is only touched there. Anything that comes from Lua arrives as a
 * Command (queued, then the loop is woken through a loopback UDP socket); anything
 * that must reach Lua leaves as an Event (queued, then announced with
 * WM_WEBSOCKET_EVENT).
 *
 * A client is the same engine in client mode: one peer that connects out instead of
 * accepting, masks its frames and expects unmasked ones.
 *
 * Not supported: TLS (ws only) and permessage-deflate.
 */

// Keep the Windows headers lean and the min / max macros out of the way.
// This must come before ANY include.
#ifndef WIN32_LEAN_AND_MEAN
	#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
	#define NOMINMAX
#endif

#include <WinSock2.h>
#include <WS2tcpip.h>

#include "websocket_network.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef _MSC_VER
	#pragma comment( lib, "ws2_32.lib" )
#endif






namespace websocket::network {

	/// Events kept in the queue while the Rainmeter thread is busy. Beyond this,
	/// message / ping / pong / drain events are dropped (and counted).
	static constexpr size_t MAX_QUEUED_EVENTS = 100000;

	/// Largest HTTP request head accepted during the handshake.
	static constexpr size_t MAX_HEADER_BYTES = 8192;

	/// Sockets (handshaking + open) accepted at the same time.
	static constexpr size_t MAX_PEERS = 2048;

	static constexpr size_t READ_CHUNK = 65536;
	static constexpr ULONGLONG HANDSHAKE_TIMEOUT_MS = 10000;
	static constexpr ULONGLONG CLOSE_TIMEOUT_MS = 3000;
	static constexpr ULONGLONG STOP_TIMEOUT_MS = 1500;
	static constexpr int POLL_TIMEOUT_MS = 500;
	/// Poll interval while a client TCP connect is in progress.
	static constexpr int CONNECT_POLL_MS = 20;

	// Result of parsing one frame
	static constexpr int PARSE_NEED_MORE = 0;
	static constexpr int PARSE_CONTINUE = 1;
	static constexpr int PARSE_STOP = 2;



	/// Wake-up socket: any thread sends one byte to it to interrupt WSAPoll.
	struct Waker {
		SOCKET sock = INVALID_SOCKET;
		sockaddr_in addr = {};
	};



	/// One TCP connection, from accept() until it is dropped.
	struct Peer {
		SOCKET sock = INVALID_SOCKET;
		std::string ip;

		ULONGLONG connectedAt = 0;
		ULONGLONG lastActivity = 0;
		ULONGLONG closeDeadline = 0;

		std::string in; // received bytes not consumed yet
		std::string out; // bytes waiting to be sent
		size_t outPos = 0; // already sent part of `out`

		// Client side only
		bool isClient = false; // we opened this connection (frames are masked)
		bool connecting = false; // TCP connect still in progress
		ULONGLONG connectDeadline = 0; // connect + handshake must be done by then
		std::string expectedAccept; // Sec-WebSocket-Accept the server must answer with

		std::shared_ptr<core::Connection> conn; // set once the connection is upgraded
		bool upgraded = false;
		bool closing = false; // flush what is queued, then wait for the peer to go away
		bool closeSent = false; // our close frame is queued
		bool shutdownSent = false;
		bool pingSent = false;
		bool needDrain = false;
		bool dead = false;

		int closeCode = 1006;
		std::string closeReason;

		// A message split in several frames
		bool inFragment = false;
		bool fragmentBinary = false;
		std::string fragment;

		std::unordered_set<std::string> topics;
	};



	/// Everything the server thread owns.
	struct Server {
		core::Context &ctx;
		const core::Config &cfg;

		SOCKET listenSock = INVALID_SOCKET;
		Waker waker;

		std::vector<std::unique_ptr<Peer>> peers;
		std::unordered_map<uint64_t, Peer *> byConn;
		std::unordered_map<std::string, std::unordered_set<Peer *>> topics;
		std::string readBuffer;

		bool stopping = false;
		ULONGLONG stopDeadline = 0;

		std::string clientError; // client: why the connection could not be established

		// Client reconnection
		bool attemptActive = false; // an attempt (connect + session) is running or was just started
		bool userClosed = false; // close() was called: never reconnect
		bool reconnectPending = false; // waiting for reconnectAt
		ULONGLONG reconnectAt = 0;
		unsigned int attempt = 0; // consecutive attempts that did not get to open

		explicit Server( core::Context &c ) :
			ctx( c ),
			cfg( c.config ) {
			readBuffer.resize( READ_CHUNK );
		}
	};



	/// A parsed HTTP request head.
	struct Request {
		std::string method;
		std::string path;
		std::string query;
		std::vector<std::pair<std::string, std::string>> headers; // names lowercase
	};



	/// Result of the credential check.
	struct AuthResult {
		bool allowed = false;
		std::string protocol; // subprotocol to answer with ("" = none)
	};



	static ULONGLONG Now() {
		return GetTickCount64();
	}



	// ------------------------------------------------------------------
	// Server thread -> Lua
	// ------------------------------------------------------------------

	// Answers the start-up handshake. Only the first answer counts.
	static void SignalStart( core::Context &ctx, bool ok, const std::string &error ) {
		{
			std::lock_guard<std::mutex> lock( ctx.startMutex );

			if ( ctx.startState != core::Context::START_PENDING )
				return;

			ctx.startState = ok ? core::Context::START_OK : core::Context::START_FAILED;
			ctx.startError = error;
		}

		ctx.startCv.notify_all();
	}



	// Announces queued events to the Rainmeter thread (one message at a time).
	static void NotifyMain( core::Context &ctx ) {
		if ( ctx.notifyPending.exchange( true ) )
			return;

		HWND hWnd = ctx.hNotifyWindow;
		if ( !hWnd || !PostMessage( hWnd, WM_WEBSOCKET_EVENT, static_cast<WPARAM>( ctx.refSelf ), 0 ) )
			ctx.notifyPending.store( false );
	}



	// Queues an event. Never throws.
	static void PostEvent( core::Context &ctx, int type, uint64_t connId, std::string_view data = {}, bool binary = false, int code = 0, const std::shared_ptr<core::Connection> &conn = nullptr, bool retry = false, unsigned int delay = 0 ) {
		try {
			{
				std::lock_guard<std::mutex> lock( ctx.eventMutex );

				// Open / close / error events are never dropped: Lua would lose track of connections.
				const bool droppable = type != core::Event::OPEN && type != core::Event::CLOSE && type != core::Event::SERVER_ERROR;

				if ( droppable && ctx.events.size() >= MAX_QUEUED_EVENTS ) {
					ctx.droppedEvents.fetch_add( 1 );
					return;
				}

				core::Event ev;
				ev.type = type;
				ev.connId = connId;
				ev.conn = conn;
				ev.binary = binary;
				ev.code = code;
				ev.retry = retry;
				ev.delay = delay;

				if ( !data.empty() )
					ev.data.assign( data.data(), data.size() );

				ctx.events.push_back( std::move( ev ) );
			}

			NotifyMain( ctx );
		}

		catch ( ... ) {
		}
	}



	// ------------------------------------------------------------------
	// Small helpers
	// ------------------------------------------------------------------

	static char LowerAscii( char c ) {
		return ( c >= 'A' && c <= 'Z' ) ? static_cast<char>( c - 'A' + 'a' ) : c;
	}



	static std::string ToLowerAscii( std::string_view text ) {
		std::string out( text );
		for ( char &c : out )
			c = LowerAscii( c );

		return out;
	}



	static bool EqualsNoCase( std::string_view a, std::string_view b ) {
		if ( a.size() != b.size() )
			return false;

		for ( size_t i = 0; i < a.size(); i++ ) {
			if ( LowerAscii( a[i] ) != LowerAscii( b[i] ) )
				return false;
		}

		return true;
	}



	static std::string_view Trim( std::string_view text ) {
		while ( !text.empty() && ( text.front() == ' ' || text.front() == '\t' ) )
			text.remove_prefix( 1 );
		while ( !text.empty() && ( text.back() == ' ' || text.back() == '\t' ) )
			text.remove_suffix( 1 );

		return text;
	}



	// "a, b ,c" -> { "a", "b", "c" }
	static std::vector<std::string_view> SplitList( std::string_view list ) {
		std::vector<std::string_view> out;
		size_t pos = 0;

		while ( pos <= list.size() ) {
			size_t comma = list.find( ',', pos );
			if ( comma == std::string_view::npos )
				comma = list.size();

			std::string_view token = Trim( list.substr( pos, comma - pos ) );
			if ( !token.empty() )
				out.push_back( token );

			pos = comma + 1;
		}

		return out;
	}



	static bool ListHasToken( std::string_view list, std::string_view token ) {
		for ( std::string_view entry : SplitList( list ) ) {
			if ( EqualsNoCase( entry, token ) )
				return true;
		}

		return false;
	}



	static int HexValue( char c ) {
		if ( c >= '0' && c <= '9' )
			return c - '0';
		if ( c >= 'a' && c <= 'f' )
			return c - 'a' + 10;
		if ( c >= 'A' && c <= 'F' )
			return c - 'A' + 10;

		return -1;
	}



	// Percent-decoding of a query string value. '+' is kept as is (it is NOT a space
	// here), so base64 tokens sent without encoding still match.
	static std::string UrlDecode( std::string_view input ) {
		std::string out;
		out.reserve( input.size() );

		for ( size_t i = 0; i < input.size(); i++ ) {
			if ( input[i] == '%' && i + 2 < input.size() ) {
				const int hi = HexValue( input[i + 1] );
				const int lo = HexValue( input[i + 2] );

				if ( hi >= 0 && lo >= 0 ) {
					out += static_cast<char>( hi * 16 + lo );
					i += 2;
					continue;
				}
			}

			out += input[i];
		}

		return out;
	}



	// Value of the query parameter `name` (compared case-insensitively), "" when absent.
	static std::string QueryValue( std::string_view query, const std::string &name ) {
		size_t pos = 0;

		while ( pos <= query.size() ) {
			size_t amp = query.find( '&', pos );
			if ( amp == std::string_view::npos )
				amp = query.size();

			std::string_view pair = query.substr( pos, amp - pos );
			const size_t eq = pair.find( '=' );
			const std::string_view key = eq == std::string_view::npos ? pair : pair.substr( 0, eq );

			if ( EqualsNoCase( UrlDecode( key ), name ) )
				return eq == std::string_view::npos ? std::string() : UrlDecode( pair.substr( eq + 1 ) );

			pos = amp + 1;
		}

		return std::string();
	}



	// Strict UTF-8 check (no overlong forms, no surrogates, nothing above U+10FFFF).
	static bool IsValidUtf8( std::string_view text ) {
		const unsigned char *s = reinterpret_cast<const unsigned char *>( text.data() );
		const size_t n = text.size();
		size_t i = 0;

		while ( i < n ) {
			const unsigned char c = s[i];

			if ( c < 0x80 ) {
				i++;
				continue;
			}

			size_t extra = 0;
			uint32_t minimum = 0;
			uint32_t code = 0;

			if ( c >= 0xC2 && c <= 0xDF ) {
				extra = 1;
				minimum = 0x80;
				code = c & 0x1F;
			}

			else if ( c >= 0xE0 && c <= 0xEF ) {
				extra = 2;
				minimum = 0x800;
				code = c & 0x0F;
			}

			else if ( c >= 0xF0 && c <= 0xF4 ) {
				extra = 3;
				minimum = 0x10000;
				code = c & 0x07;
			}

			else
				return false;

			if ( i + extra >= n )
				return false;

			for ( size_t k = 1; k <= extra; k++ ) {
				if ( ( s[i + k] & 0xC0 ) != 0x80 )
					return false;

				code = ( code << 6 ) | ( s[i + k] & 0x3F );
			}

			if ( code < minimum || code > 0x10FFFF || ( code >= 0xD800 && code <= 0xDFFF ) )
				return false;

			i += extra + 1;
		}

		return true;
	}



	// ------------------------------------------------------------------
	// SHA-1 and base64 (for Sec-WebSocket-Accept)
	// ------------------------------------------------------------------

	static uint32_t Rotl( uint32_t value, int bits ) {
		return ( value << bits ) | ( value >> ( 32 - bits ) );
	}



	static void Sha1( std::string_view data, unsigned char out[20] ) {
		uint32_t h[5] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u };

		std::string message( data );
		message.push_back( static_cast<char>( 0x80 ) );
		while ( message.size() % 64 != 56 )
			message.push_back( 0 );

		const uint64_t bits = static_cast<uint64_t>( data.size() ) * 8;
		for ( int i = 7; i >= 0; i-- )
			message.push_back( static_cast<char>( ( bits >> ( i * 8 ) ) & 0xFF ) );

		for ( size_t chunk = 0; chunk < message.size(); chunk += 64 ) {
			uint32_t w[80];
			const unsigned char *p = reinterpret_cast<const unsigned char *>( message.data() ) + chunk;

			for ( int i = 0; i < 16; i++ )
				w[i] = ( static_cast<uint32_t>( p[i * 4] ) << 24 ) | ( static_cast<uint32_t>( p[i * 4 + 1] ) << 16 ) | ( static_cast<uint32_t>( p[i * 4 + 2] ) << 8 ) | static_cast<uint32_t>( p[i * 4 + 3] );

			for ( int i = 16; i < 80; i++ )
				w[i] = Rotl( w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1 );

			uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];

			for ( int i = 0; i < 80; i++ ) {
				uint32_t f, k;

				if ( i < 20 ) {
					f = ( b & c ) | ( ~b & d );
					k = 0x5A827999u;
				}

				else if ( i < 40 ) {
					f = b ^ c ^ d;
					k = 0x6ED9EBA1u;
				}

				else if ( i < 60 ) {
					f = ( b & c ) | ( b & d ) | ( c & d );
					k = 0x8F1BBCDCu;
				}

				else {
					f = b ^ c ^ d;
					k = 0xCA62C1D6u;
				}

				const uint32_t temp = Rotl( a, 5 ) + f + e + k + w[i];
				e = d;
				d = c;
				c = Rotl( b, 30 );
				b = a;
				a = temp;
			}

			h[0] += a;
			h[1] += b;
			h[2] += c;
			h[3] += d;
			h[4] += e;
		}

		for ( int i = 0; i < 5; i++ ) {
			out[i * 4] = static_cast<unsigned char>( h[i] >> 24 );
			out[i * 4 + 1] = static_cast<unsigned char>( h[i] >> 16 );
			out[i * 4 + 2] = static_cast<unsigned char>( h[i] >> 8 );
			out[i * 4 + 3] = static_cast<unsigned char>( h[i] );
		}
	}



	static std::string Base64( const unsigned char *data, size_t length ) {
		static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

		std::string out;
		out.reserve( ( length + 2 ) / 3 * 4 );

		for ( size_t i = 0; i < length; i += 3 ) {
			const uint32_t b0 = data[i];
			const uint32_t b1 = i + 1 < length ? data[i + 1] : 0;
			const uint32_t b2 = i + 2 < length ? data[i + 2] : 0;
			const uint32_t triple = ( b0 << 16 ) | ( b1 << 8 ) | b2;

			out += alphabet[( triple >> 18 ) & 0x3F];
			out += alphabet[( triple >> 12 ) & 0x3F];
			out += i + 1 < length ? alphabet[( triple >> 6 ) & 0x3F] : '=';
			out += i + 2 < length ? alphabet[triple & 0x3F] : '=';
		}

		return out;
	}



	// Sec-WebSocket-Accept for a client key
	static std::string AcceptKey( std::string_view key ) {
		std::string input( key );
		input += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

		unsigned char digest[20];
		Sha1( input, digest );

		return Base64( digest, sizeof( digest ) );
	}



	// ------------------------------------------------------------------
	// Credentials
	// ------------------------------------------------------------------

	// Constant-time comparison: the time taken does not reveal how much of the secret matched.
	static bool SecretEquals( std::string_view given, const std::string &secret ) {
		const size_t length = given.size() > secret.size() ? given.size() : secret.size();
		unsigned char diff = given.size() != secret.size() ? 1 : 0;

		for ( size_t i = 0; i < length; i++ ) {
			const unsigned char a = i < given.size() ? static_cast<unsigned char>( given[i] ) : 0;
			const unsigned char b = i < secret.size() ? static_cast<unsigned char>( secret[i] ) : 0;
			diff |= static_cast<unsigned char>( a ^ b );
		}

		return diff == 0;
	}



	static bool MatchesAnySecret( const core::AuthConfig &auth, std::string_view given ) {
		if ( given.empty() )
			return false;

		// No early exit: the time does not depend on which secret matched.
		bool ok = false;
		for ( const std::string &secret : auth.secrets )
			ok = SecretEquals( given, secret ) || ok;

		return ok;
	}



	static std::string_view HeaderValue( const Request &req, std::string_view name ) {
		for ( const auto &header : req.headers ) {
			if ( header.first == name )
				return header.second;
		}

		return std::string_view();
	}



	// Checks the credential of an upgrade request and picks the subprotocol to answer with.
	// A browser requires the answer to be ONE of the offered subprotocols, never the whole list.
	static AuthResult Authorize( const core::Config &cfg, const Request &req ) {
		AuthResult result;

		const std::vector<std::string_view> protocols = SplitList( HeaderValue( req, "sec-websocket-protocol" ) );

		if ( cfg.auth.type == core::AuthConfig::AUTH_NONE )
			result.allowed = true;

		else if ( cfg.auth.type == core::AuthConfig::AUTH_HEADER )
			result.allowed = MatchesAnySecret( cfg.auth, HeaderValue( req, cfg.auth.name ) );

		else if ( cfg.auth.type == core::AuthConfig::AUTH_QUERY )
			result.allowed = MatchesAnySecret( cfg.auth, QueryValue( req.query, cfg.auth.name ) );

		else if ( cfg.auth.type == core::AuthConfig::AUTH_PROTOCOL ) {
			// The secret is one of the offered subprotocols. The answer is another offered
			// entry, so the secret is not echoed back; only when it was the sole entry
			// (browsers insist on an answer) it has to be.
			bool found = false;
			std::string_view matched;
			std::string_view other;

			for ( std::string_view token : protocols ) {
				if ( MatchesAnySecret( cfg.auth, token ) ) {
					found = true;
					matched = token;
				}

				else if ( other.empty() )
					other = token;
			}

			result.allowed = found;
			result.protocol = std::string( other.empty() ? matched : other );
			return result;
		}

		if ( !protocols.empty() )
			result.protocol = std::string( protocols.front() );

		return result;
	}



	// "/*" matches everything, "/prefix/*" a prefix, anything else the exact path.
	static bool PathMatches( const std::string &pattern, const std::string &path ) {
		if ( pattern == "/*" )
			return true;

		if ( pattern.size() >= 2 && pattern.compare( pattern.size() - 2, 2, "/*" ) == 0 ) {
			const std::string prefix = pattern.substr( 0, pattern.size() - 1 );
			return path.compare( 0, prefix.size(), prefix ) == 0;
		}

		return path == pattern;
	}



	// ------------------------------------------------------------------
	// Peers: output, close, drop
	// ------------------------------------------------------------------

	static size_t Pending( const Peer &p ) {
		return p.out.size() - p.outPos;
	}



	// Fills `buf` with random bytes (client masks and handshake keys).
	static void RandomBytes( unsigned char *buf, size_t n ) {
		static std::mutex mutex;
		static std::mt19937_64 engine( [] {
			std::random_device rd;
			return ( static_cast<uint64_t>( rd() ) << 32 ) ^ rd() ^ static_cast<uint64_t>( GetTickCount64() );
		}() );

		std::lock_guard<std::mutex> lock( mutex );

		for ( size_t i = 0; i < n; i++ )
			buf[i] = static_cast<unsigned char>( engine() >> 24 );
	}



	// Appends one frame. Servers send it unmasked, clients must mask it (RFC 6455 5.3).
	static void AppendFrame( std::string &out, int opcode, std::string_view payload, bool mask = false ) {
		const size_t n = payload.size();
		const char maskBit = mask ? static_cast<char>( 0x80 ) : 0;

		out.push_back( static_cast<char>( 0x80 | opcode ) );

		if ( n <= 125 )
			out.push_back( static_cast<char>( maskBit | static_cast<char>( n ) ) );

		else if ( n <= 65535 ) {
			out.push_back( static_cast<char>( maskBit | 126 ) );
			out.push_back( static_cast<char>( ( n >> 8 ) & 0xFF ) );
			out.push_back( static_cast<char>( n & 0xFF ) );
		}

		else {
			out.push_back( static_cast<char>( maskBit | 127 ) );
			for ( int i = 7; i >= 0; i-- )
				out.push_back( static_cast<char>( ( static_cast<uint64_t>( n ) >> ( i * 8 ) ) & 0xFF ) );
		}

		if ( !mask ) {
			out.append( payload );
			return;
		}

		unsigned char key[4];
		RandomBytes( key, sizeof( key ) );
		out.append( reinterpret_cast<const char *>( key ), 4 );

		const size_t start = out.size();
		out.append( payload );

		for ( size_t i = 0; i < n; i++ )
			out[start + i] = static_cast<char>( static_cast<unsigned char>( out[start + i] ) ^ key[i & 3] );
	}



	// Client: will the next end of a connection / failed attempt be followed by a reconnection?
	static bool CanRetry( const Server &s ) {
		const core::ClientConfig::Reconnect &rc = s.cfg.client.reconnect;

		if ( !s.cfg.isClient || !rc.enabled || s.stopping || s.userClosed )
			return false;

		return rc.maxTries == 0 || s.attempt + 1 <= rc.maxTries;
	}



	// Closes the socket and forgets the peer. An upgraded peer reports a CLOSE event.
	static void DropPeer( Server &s, Peer &p, int code, std::string_view reason ) {
		if ( p.dead )
			return;

		p.dead = true;

		if ( p.isClient && !p.upgraded && !s.stopping && s.clientError.empty() )
			s.clientError = "The connection was closed before the handshake finished";

		if ( p.sock != INVALID_SOCKET ) {
			closesocket( p.sock );
			p.sock = INVALID_SOCKET;
		}

		for ( const std::string &topic : p.topics ) {
			auto it = s.topics.find( topic );

			if ( it != s.topics.end() ) {
				it->second.erase( &p );

				if ( it->second.empty() )
					s.topics.erase( it );
			}
		}
		p.topics.clear();

		if ( p.upgraded ) {
			p.conn->open.store( false );
			s.byConn.erase( p.conn->id );
			s.ctx.connectionCount.fetch_sub( 1 );

			PostEvent( s.ctx, core::Event::CLOSE, p.conn->id, reason, false, code, nullptr, s.cfg.isClient && CanRetry( s ) );
		}
	}



	// Queues a data / control frame. false when it was not queued (closing, or the
	// peer is too slow: it is told with a DRAIN event when it has caught up).
	static bool QueueFrame( Server &s, Peer &p, int opcode, std::string_view payload ) {
		if ( p.dead || p.closeSent )
			return false;

		// maxBackpressure 0 = unlimited. A message is always accepted when nothing is waiting.
		const size_t limit = s.cfg.maxBackpressure;
		const size_t pending = Pending( p );

		if ( limit > 0 && pending > 0 && pending + payload.size() + 10 > limit ) {
			p.needDrain = true;
			return false;
		}

		AppendFrame( p.out, opcode, payload, p.isClient );
		return true;
	}



	// Queues the close frame and starts the close: the rest is flushed, then the peer
	// has a few seconds to hang up. `code` 0 = empty close frame.
	static void QueueClose( Peer &p, int code, std::string_view reason ) {
		if ( p.dead || p.closeSent )
			return;

		std::string payload;
		if ( code > 0 ) {
			payload.push_back( static_cast<char>( ( code >> 8 ) & 0xFF ) );
			payload.push_back( static_cast<char>( code & 0xFF ) );
			payload.append( reason.substr( 0, 123 ) );
		}

		AppendFrame( p.out, 8, payload, p.isClient );

		p.closeSent = true;
		p.closing = true;
		p.closeCode = code > 0 ? code : 1005;
		p.closeReason = std::string( reason.substr( 0, 123 ) );
		p.closeDeadline = Now() + CLOSE_TIMEOUT_MS;

		if ( p.conn )
			p.conn->open.store( false );
	}



	// Sends what the socket accepts. Finishes the close once everything went out.
	static void Flush( Server &s, Peer &p ) {
		while ( !p.dead && Pending( p ) > 0 ) {
			const size_t left = Pending( p );
			const int chunk = left > 1048576 ? 1048576 : static_cast<int>( left );

			const int sent = send( p.sock, p.out.data() + p.outPos, chunk, 0 );

			if ( sent > 0 ) {
				p.outPos += static_cast<size_t>( sent );
				continue;
			}

			if ( sent == 0 || WSAGetLastError() == WSAEWOULDBLOCK )
				break;

			DropPeer( s, p, 1006, std::string_view() );
			return;
		}

		if ( p.dead )
			return;

		if ( Pending( p ) == 0 ) {
			p.out.clear();
			p.outPos = 0;

			if ( p.out.capacity() > 1048576 )
				std::string().swap( p.out );

			if ( p.needDrain ) {
				p.needDrain = false;

				if ( p.upgraded && s.ctx.wanted[core::Event::DRAIN].load() )
					PostEvent( s.ctx, core::Event::DRAIN, p.conn->id );
			}

			// Everything is out: half-close, so the peer reads the close frame (or the HTTP
			// error) before the connection goes away. The peer then closes its side.
			if ( p.closing && !p.shutdownSent ) {
				shutdown( p.sock, SD_SEND );
				p.shutdownSent = true;
			}
		}

		else if ( p.outPos > 65536 ) {
			p.out.erase( 0, p.outPos );
			p.outPos = 0;
		}
	}



	// ------------------------------------------------------------------
	// Frames
	// ------------------------------------------------------------------

	static int Fail( Peer &p, int code ) {
		QueueClose( p, code, std::string_view() );
		return PARSE_STOP;
	}



	static bool ValidReceivedCloseCode( int code ) {
		return ( code >= 1000 && code <= 1003 ) || ( code >= 1007 && code <= 1014 ) || ( code >= 3000 && code <= 4999 );
	}



	static int Deliver( Server &s, Peer &p, bool binary, std::string_view data ) {
		if ( !binary && !IsValidUtf8( data ) )
			return Fail( p, 1007 );

		if ( s.ctx.wanted[core::Event::MESSAGE].load() )
			PostEvent( s.ctx, core::Event::MESSAGE, p.conn->id, data, binary );

		return PARSE_CONTINUE;
	}



	// Parses one client frame starting at p.in[off].
	static int ParseOne( Server &s, Peer &p, size_t &off ) {
		const size_t avail = p.in.size() - off;
		if ( avail < 2 )
			return PARSE_NEED_MORE;

		const unsigned char *d = reinterpret_cast<const unsigned char *>( p.in.data() ) + off;

		const bool fin = ( d[0] & 0x80 ) != 0;
		const int rsv = d[0] & 0x70;
		const int opcode = d[0] & 0x0F;
		const bool masked = ( d[1] & 0x80 ) != 0;
		uint64_t length = d[1] & 0x7F;

		// No extension is negotiated: RSV bits must be 0. Frames from a client must be
		// masked, frames from a server must not be.
		if ( rsv != 0 || masked == p.isClient )
			return Fail( p, 1002 );

		const size_t maskLength = masked ? 4 : 0;

		if ( ( opcode > 2 && opcode < 8 ) || opcode > 10 )
			return Fail( p, 1002 );

		const bool control = opcode >= 8;
		size_t pos = 2;

		if ( length == 126 ) {
			if ( avail < 4 )
				return PARSE_NEED_MORE;

			length = ( static_cast<uint64_t>( d[2] ) << 8 ) | d[3];
			pos = 4;
		}

		else if ( length == 127 ) {
			if ( avail < 10 )
				return PARSE_NEED_MORE;

			length = 0;
			for ( int i = 0; i < 8; i++ )
				length = ( length << 8 ) | d[2 + i];

			pos = 10;

			if ( ( length >> 63 ) != 0 )
				return Fail( p, 1002 );
		}

		if ( control && ( !fin || length > 125 ) )
			return Fail( p, 1002 );

		if ( length > s.cfg.maxPayload )
			return Fail( p, 1009 );

		if ( avail < pos + maskLength + static_cast<size_t>( length ) )
			return PARSE_NEED_MORE;

		const unsigned char *mask = d + pos;
		const unsigned char *body = d + pos + maskLength;

		std::string payload( reinterpret_cast<const char *>( body ), static_cast<size_t>( length ) );
		if ( masked ) {
			for ( size_t i = 0; i < payload.size(); i++ )
				payload[i] = static_cast<char>( static_cast<unsigned char>( payload[i] ) ^ mask[i & 3] );
		}

		off += pos + maskLength + payload.size();

		switch ( opcode ) {
			case 0: { // continuation
				if ( !p.inFragment )
					return Fail( p, 1002 );

				if ( p.fragment.size() + payload.size() > s.cfg.maxPayload )
					return Fail( p, 1009 );

				p.fragment += payload;

				if ( fin ) {
					p.inFragment = false;
					std::string message;
					message.swap( p.fragment );
					return Deliver( s, p, p.fragmentBinary, message );
				}

				return PARSE_CONTINUE;
			}

			case 1:
			case 2: { // text / binary
				if ( p.inFragment )
					return Fail( p, 1002 );

				if ( fin )
					return Deliver( s, p, opcode == 2, payload );

				p.inFragment = true;
				p.fragmentBinary = opcode == 2;
				p.fragment.swap( payload );
				return PARSE_CONTINUE;
			}

			case 8: { // close
				int code = 1005;
				std::string_view reason;

				if ( payload.size() == 1 )
					return Fail( p, 1002 );

				if ( payload.size() >= 2 ) {
					code = ( static_cast<unsigned char>( payload[0] ) << 8 ) | static_cast<unsigned char>( payload[1] );
					reason = std::string_view( payload ).substr( 2 );

					if ( !ValidReceivedCloseCode( code ) )
						return Fail( p, 1002 );

					if ( !IsValidUtf8( reason ) )
						return Fail( p, 1007 );
				}

				// Answer with the same code, then let the connection go.
				const std::string reasonText( reason );
				QueueClose( p, code == 1005 ? 0 : code, std::string_view() );
				p.closeCode = code;
				p.closeReason = reasonText;
				return PARSE_STOP;
			}

			case 9: { // ping
				QueueFrame( s, p, 10, payload );

				if ( s.ctx.wanted[core::Event::PING].load() )
					PostEvent( s.ctx, core::Event::PING, p.conn->id, payload );

				return PARSE_CONTINUE;
			}

			default: { // 10: pong
				p.pingSent = false;

				if ( s.ctx.wanted[core::Event::PONG].load() )
					PostEvent( s.ctx, core::Event::PONG, p.conn->id, payload );

				return PARSE_CONTINUE;
			}
		}
	}



	static void ParseFrames( Server &s, Peer &p ) {
		size_t off = 0;
		int result;

		do {
			result = ParseOne( s, p, off );
		} while ( result == PARSE_CONTINUE && !p.dead );

		if ( !p.dead )
			p.in.erase( 0, off );
	}



	// ------------------------------------------------------------------
	// Handshake
	// ------------------------------------------------------------------

	// Answers with an HTTP error and starts closing the connection.
	static void Reject( Peer &p, const char *status, const char *body, const char *extraHeaders = "" ) {
		std::string response = "HTTP/1.1 ";
		response += status;
		response += "\r\nContent-Type: text/plain\r\nConnection: close\r\n";
		response += extraHeaders;
		response += "Content-Length: " + std::to_string( strlen( body ) ) + "\r\n\r\n";
		response += body;

		p.out += response;
		p.in.clear();
		p.closing = true;
		p.closeDeadline = Now() + CLOSE_TIMEOUT_MS;
	}



	// Parses the header lines of `head` starting at `pos` (names lowercased, repeated
	// headers merged with ", "). false when a line is malformed.
	static bool ParseHeaderLines( std::string_view head, size_t pos, std::vector<std::pair<std::string, std::string>> &headers ) {
		while ( pos < head.size() ) {
			size_t end = head.find( "\r\n", pos );
			if ( end == std::string_view::npos )
				end = head.size();

			const std::string_view header = head.substr( pos, end - pos );
			pos = end + 2;

			if ( header.empty() )
				continue;

			const size_t colon = header.find( ':' );
			if ( colon == std::string_view::npos || colon == 0 )
				return false;

			const std::string name = ToLowerAscii( header.substr( 0, colon ) );
			if ( name.find_first_of( " \t" ) != std::string::npos )
				return false;

			const std::string_view value = Trim( header.substr( colon + 1 ) );

			bool merged = false;
			for ( auto &entry : headers ) {
				if ( entry.first == name ) {
					entry.second += ", ";
					entry.second.append( value );
					merged = true;
					break;
				}
			}

			if ( !merged )
				headers.emplace_back( name, std::string( value ) );
		}

		return true;
	}



	// head = the request without the final blank line
	static bool ParseRequest( std::string_view head, Request &req ) {
		const size_t lineEnd = head.find( "\r\n" );
		const std::string_view line = head.substr( 0, lineEnd );

		const size_t sp1 = line.find( ' ' );
		const size_t sp2 = line.rfind( ' ' );
		if ( sp1 == std::string_view::npos || sp2 == sp1 )
			return false;

		req.method = std::string( line.substr( 0, sp1 ) );

		const std::string_view target = line.substr( sp1 + 1, sp2 - sp1 - 1 );
		if ( line.substr( sp2 + 1 ) != "HTTP/1.1" || target.empty() || target[0] != '/' )
			return false;

		const size_t question = target.find( '?' );
		req.path = std::string( target.substr( 0, question ) );
		req.query = question == std::string_view::npos ? std::string() : std::string( target.substr( question + 1 ) );

		return ParseHeaderLines( head, lineEnd == std::string_view::npos ? head.size() : lineEnd + 2, req.headers );
	}



	// The HTTP handshake is done: the peer becomes a WebSocket connection (server and client).
	// Bytes already received after the handshake belong to the first frames.
	static void Upgrade( Server &s, Peer &p, std::string_view protocol = {} ) {
		p.conn = std::make_shared<core::Connection>();
		p.conn->id = s.ctx.nextConnId.fetch_add( 1 );
		p.conn->ip = p.ip;
		p.upgraded = true;
		p.lastActivity = Now();

		if ( p.isClient )
			s.attempt = 0; // connected: the next failure starts the backoff again

		s.byConn[p.conn->id] = &p;
		s.ctx.connectionCount.fetch_add( 1 );

		PostEvent( s.ctx, core::Event::OPEN, p.conn->id, protocol, false, 0, p.conn );

		if ( !p.in.empty() )
			ParseFrames( s, p );
	}



	static void HandleHandshake( Server &s, Peer &p ) {
		const size_t end = p.in.find( "\r\n\r\n" );

		if ( end == std::string::npos ) {
			if ( p.in.size() > MAX_HEADER_BYTES )
				Reject( p, "431 Request Header Fields Too Large", "Request too large" );

			return;
		}

		if ( end > MAX_HEADER_BYTES ) {
			Reject( p, "431 Request Header Fields Too Large", "Request too large" );
			return;
		}

		Request req;
		if ( !ParseRequest( std::string_view( p.in ).substr( 0, end ), req ) || req.method != "GET" ) {
			Reject( p, "400 Bad Request", "Bad Request" );
			return;
		}

		if ( !PathMatches( s.cfg.path, req.path ) ) {
			Reject( p, "404 Not Found", "Not Found" );
			return;
		}

		if ( !EqualsNoCase( HeaderValue( req, "upgrade" ), "websocket" ) || !ListHasToken( HeaderValue( req, "connection" ), "upgrade" ) ) {
			Reject( p, "426 Upgrade Required", "WebSocket upgrade required", "Upgrade: websocket\r\n" );
			return;
		}

		if ( HeaderValue( req, "sec-websocket-version" ) != "13" ) {
			Reject( p, "426 Upgrade Required", "Unsupported WebSocket version", "Sec-WebSocket-Version: 13\r\n" );
			return;
		}

		const std::string_view key = HeaderValue( req, "sec-websocket-key" );
		if ( key.size() != 24 ) {
			Reject( p, "400 Bad Request", "Bad Request" );
			return;
		}

		// The credential is checked HERE, before the connection becomes a WebSocket:
		// a wrong or missing credential gets a 401 and the socket never opens.
		const AuthResult auth = Authorize( s.cfg, req );
		if ( !auth.allowed ) {
			Reject( p, "401 Unauthorized", "Unauthorized" );
			return;
		}

		if ( s.cfg.maxConnections > 0 && s.ctx.connectionCount.load() >= s.cfg.maxConnections ) {
			Reject( p, "503 Service Unavailable", "Too many connections" );
			return;
		}

		std::string response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ";
		response += AcceptKey( key );
		response += "\r\n";

		if ( !auth.protocol.empty() )
			response += "Sec-WebSocket-Protocol: " + auth.protocol + "\r\n";

		response += "\r\n";

		p.out += response;

		// Bytes after the request already belong to the first frames.
		p.in.erase( 0, end + 4 );

		Upgrade( s, p );
	}



	// ------------------------------------------------------------------
	// Client handshake
	// ------------------------------------------------------------------

	// The connection failed: remember why (reported with an error event) and drop it.
	static void ClientFail( Server &s, Peer &p, const std::string &message ) {
		if ( s.clientError.empty() )
			s.clientError = message;

		DropPeer( s, p, 1006, std::string_view() );
	}



	// Queues the HTTP upgrade request.
	static void SendClientHandshake( Server &s, Peer &p ) {
		const core::ClientConfig &client = s.cfg.client;

		unsigned char raw[16];
		RandomBytes( raw, sizeof( raw ) );

		const std::string key = Base64( raw, sizeof( raw ) );
		p.expectedAccept = AcceptKey( key );

		std::string request = "GET " + client.target + " HTTP/1.1\r\n";
		request += "Host: " + client.hostHeader + "\r\n";
		request += "Upgrade: websocket\r\nConnection: Upgrade\r\n";
		request += "Sec-WebSocket-Key: " + key + "\r\n";
		request += "Sec-WebSocket-Version: 13\r\n";

		if ( !client.protocols.empty() ) {
			request += "Sec-WebSocket-Protocol: ";

			for ( size_t i = 0; i < client.protocols.size(); i++ ) {
				if ( i > 0 )
					request += ", ";

				request += client.protocols[i];
			}

			request += "\r\n";
		}

		for ( const auto &header : client.headers )
			request += header.first + ": " + header.second + "\r\n";

		request += "\r\n";

		p.out += request;
	}



	// Reads the server answer. 101 + a valid Sec-WebSocket-Accept makes the peer a connection.
	static void HandleClientHandshake( Server &s, Peer &p ) {
		const size_t end = p.in.find( "\r\n\r\n" );

		if ( end == std::string::npos ) {
			if ( p.in.size() > MAX_HEADER_BYTES )
				ClientFail( s, p, "The server answer is too large" );

			return;
		}

		const std::string_view head = std::string_view( p.in ).substr( 0, end );
		const size_t lineEnd = head.find( "\r\n" );
		const std::string_view status = head.substr( 0, lineEnd );

		// "HTTP/1.1 101 Switching Protocols"
		const size_t sp = status.find( ' ' );
		const bool is101 = status.substr( 0, 5 ) == "HTTP/" && sp != std::string_view::npos && status.substr( sp + 1, 3 ) == "101";

		if ( !is101 ) {
			ClientFail( s, p, "The server rejected the connection: " + std::string( status.substr( 0, 120 ) ) );
			return;
		}

		std::vector<std::pair<std::string, std::string>> headers;
		if ( !ParseHeaderLines( head, lineEnd == std::string_view::npos ? head.size() : lineEnd + 2, headers ) ) {
			ClientFail( s, p, "The server answer is malformed" );
			return;
		}

		auto value = [&headers]( std::string_view name ) -> std::string_view {
			for ( const auto &entry : headers ) {
				if ( entry.first == name )
					return entry.second;
			}

			return std::string_view();
		};

		if ( !EqualsNoCase( value( "upgrade" ), "websocket" ) || !ListHasToken( value( "connection" ), "upgrade" ) ) {
			ClientFail( s, p, "The server answer is not a WebSocket upgrade" );
			return;
		}

		if ( value( "sec-websocket-accept" ) != p.expectedAccept ) {
			ClientFail( s, p, "The server sent an invalid Sec-WebSocket-Accept" );
			return;
		}

		// No extension was offered, so none may be selected.
		if ( !value( "sec-websocket-extensions" ).empty() ) {
			ClientFail( s, p, "The server selected a WebSocket extension that was not offered" );
			return;
		}

		const std::string_view protocol = Trim( value( "sec-websocket-protocol" ) );

		if ( !protocol.empty() ) {
			bool offered = false;

			for ( const std::string &candidate : s.cfg.client.protocols ) {
				if ( candidate == protocol )
					offered = true;
			}

			if ( !offered ) {
				ClientFail( s, p, "The server selected a subprotocol that was not offered" );
				return;
			}
		}

		const std::string chosen( protocol );
		p.in.erase( 0, end + 4 );

		Upgrade( s, p, chosen );
	}



	// ------------------------------------------------------------------
	// Sockets
	// ------------------------------------------------------------------

	static void SetNonBlocking( SOCKET sock ) {
		u_long enable = 1;
		ioctlsocket( sock, FIONBIO, &enable );
	}



	// Listening socket on host:port. INVALID_SOCKET and `errorCode` on failure.
	static SOCKET OpenListener( const std::string &host, int port, int &errorCode ) {
		addrinfo hints = {};
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
		hints.ai_flags = AI_PASSIVE;

		addrinfo *result = nullptr;
		const std::string service = std::to_string( port );

		const int rc = getaddrinfo( host.c_str(), service.c_str(), &hints, &result );
		if ( rc != 0 || !result ) {
			errorCode = rc != 0 ? rc : WSAEADDRNOTAVAIL;
			return INVALID_SOCKET;
		}

		SOCKET sock = socket( result->ai_family, result->ai_socktype, result->ai_protocol );
		if ( sock == INVALID_SOCKET ) {
			errorCode = WSAGetLastError();
			freeaddrinfo( result );
			return INVALID_SOCKET;
		}

		// No other process can share the port (no port hijacking).
		int on = 1;
		setsockopt( sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>( &on ), sizeof( on ) );

		if ( bind( sock, result->ai_addr, static_cast<int>( result->ai_addrlen ) ) == SOCKET_ERROR || listen( sock, SOMAXCONN ) == SOCKET_ERROR ) {
			errorCode = WSAGetLastError();
			closesocket( sock );
			sock = INVALID_SOCKET;
		}

		freeaddrinfo( result );

		if ( sock != INVALID_SOCKET )
			SetNonBlocking( sock );

		return sock;
	}



	// Loopback UDP socket that interrupts WSAPoll when another thread has work for the loop.
	static bool OpenWaker( Waker &waker ) {
		waker.sock = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
		if ( waker.sock == INVALID_SOCKET )
			return false;

		sockaddr_in addr = {};
		addr.sin_family = AF_INET;
		addr.sin_port = 0;
		inet_pton( AF_INET, "127.0.0.1", &addr.sin_addr );

		socklen_t length = sizeof( addr );

		if ( bind( waker.sock, reinterpret_cast<sockaddr *>( &addr ), sizeof( addr ) ) == SOCKET_ERROR || getsockname( waker.sock, reinterpret_cast<sockaddr *>( &addr ), &length ) == SOCKET_ERROR ) {
			closesocket( waker.sock );
			waker.sock = INVALID_SOCKET;
			return false;
		}

		waker.addr = addr;
		SetNonBlocking( waker.sock );
		return true;
	}



	// "host:port" for messages (IPv6 literals in brackets).
	static std::string HostPort( const core::Config &cfg ) {
		const bool v6 = cfg.host.find( ':' ) != std::string::npos;
		return ( v6 ? "[" + cfg.host + "]" : cfg.host ) + ":" + std::to_string( cfg.port );
	}



	// Starts the non-blocking TCP connect of a client. On an immediate failure the
	// reason goes to Server::clientError and no peer is created.
	static void StartConnect( Server &s ) {
		const core::Config &cfg = s.cfg;

		addrinfo hints = {};
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;

		addrinfo *result = nullptr;
		const std::string service = std::to_string( cfg.port );

		const int rc = getaddrinfo( cfg.host.c_str(), service.c_str(), &hints, &result );
		if ( rc != 0 || !result ) {
			s.clientError = "Could not resolve the host \"" + cfg.host + "\" (error " + std::to_string( rc != 0 ? rc : WSAHOST_NOT_FOUND ) + ")";
			return;
		}

		// IPv4 first: embedded devices (TVs, bridges...) rarely listen on IPv6.
		const addrinfo *chosen = result;
		for ( const addrinfo *it = result; it; it = it->ai_next ) {
			if ( it->ai_family == AF_INET ) {
				chosen = it;
				break;
			}
		}

		SOCKET sock = socket( chosen->ai_family, chosen->ai_socktype, chosen->ai_protocol );
		if ( sock == INVALID_SOCKET ) {
			s.clientError = "Could not create the socket (error " + std::to_string( WSAGetLastError() ) + ")";
			freeaddrinfo( result );
			return;
		}

		SetNonBlocking( sock );

		int one = 1;
		setsockopt( sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>( &one ), sizeof( one ) );

		char text[64] = {};
		if ( chosen->ai_family == AF_INET )
			inet_ntop( AF_INET, &reinterpret_cast<const sockaddr_in *>( chosen->ai_addr )->sin_addr, text, sizeof( text ) );
		else if ( chosen->ai_family == AF_INET6 )
			inet_ntop( AF_INET6, &reinterpret_cast<const sockaddr_in6 *>( chosen->ai_addr )->sin6_addr, text, sizeof( text ) );

		bool connecting = false;

		if ( connect( sock, chosen->ai_addr, static_cast<int>( chosen->ai_addrlen ) ) == SOCKET_ERROR ) {
			const int error = WSAGetLastError();

			if ( error == WSAEWOULDBLOCK || error == WSAEINPROGRESS )
				connecting = true;

			else {
				s.clientError = "Could not connect to " + HostPort( cfg ) + " (error " + std::to_string( error ) + ")";
				closesocket( sock );
				freeaddrinfo( result );
				return;
			}
		}

		freeaddrinfo( result );

		auto peer = std::make_unique<Peer>();
		peer->sock = sock;
		peer->ip = text;
		peer->isClient = true;
		peer->connecting = connecting;
		peer->connectedAt = Now();
		peer->lastActivity = peer->connectedAt;
		peer->connectDeadline = peer->connectedAt + cfg.client.connectTimeout;

		if ( !connecting )
			SendClientHandshake( s, *peer );

		s.peers.push_back( std::move( peer ) );
	}



	// Finishes the TCP connect of the peers that are still connecting.
	// (select instead of WSAPoll: WSAPoll does not report a failed connect on older Windows.)
	static void CheckConnecting( Server &s ) {
		for ( auto &peer : s.peers ) {
			Peer &p = *peer;
			if ( p.dead || !p.connecting )
				continue;

			fd_set writable;
			fd_set failed;
			FD_ZERO( &writable );
			FD_ZERO( &failed );
			FD_SET( p.sock, &writable );
			FD_SET( p.sock, &failed );

			timeval none = { 0, 0 };
			const int ready = select( static_cast<int>( p.sock ) + 1, nullptr, &writable, &failed, &none );

			if ( ready <= 0 )
				continue;

			int error = 0;
			socklen_t length = sizeof( error );
			if ( getsockopt( p.sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>( &error ), &length ) == SOCKET_ERROR )
				error = WSAGetLastError();

			if ( error != 0 || FD_ISSET( p.sock, &failed ) ) {
				ClientFail( s, p, "Could not connect to " + HostPort( s.cfg ) + " (error " + std::to_string( error != 0 ? error : WSAECONNREFUSED ) + ")" );
				continue;
			}

			p.connecting = false;
			p.lastActivity = Now();
			SendClientHandshake( s, p );
		}
	}



	// Starts one connection attempt of a client.
	static void TryConnect( Server &s ) {
		s.clientError.clear();
		s.attemptActive = true;

		StartConnect( s );
	}



	// The attempt (connect, handshake and the session that followed) is over and nothing
	// is left: reports the failure and either schedules the next attempt or gives up.
	static void ClientAttemptEnded( Server &s ) {
		if ( !s.cfg.isClient || !s.attemptActive || !s.peers.empty() )
			return;

		s.attemptActive = false;

		const bool retry = CanRetry( s );
		const std::string error = std::move( s.clientError );
		s.clientError.clear();

		// A close the script asked for (or a stop) is not an error.
		if ( !error.empty() && !s.stopping )
			PostEvent( s.ctx, core::Event::SERVER_ERROR, 0, error, false, 0, nullptr, retry );

		if ( !retry )
			return;

		s.attempt++;

		// Exponential backoff: delay, 2 * delay, 4 * delay ... up to maxDelay.
		const core::ClientConfig::Reconnect &rc = s.cfg.client.reconnect;
		ULONGLONG delay = rc.delay;

		for ( unsigned int i = 1; i < s.attempt && delay < rc.maxDelay; i++ )
			delay *= 2;

		if ( delay > rc.maxDelay )
			delay = rc.maxDelay;

		s.reconnectAt = Now() + delay;
		s.reconnectPending = true;

		if ( s.ctx.wanted[core::Event::RECONNECTING].load() )
			PostEvent( s.ctx, core::Event::RECONNECTING, 0, {}, false, static_cast<int>( s.attempt ), nullptr, false, static_cast<unsigned int>( delay ) );
	}



	static void AcceptPeers( Server &s ) {
		for ( int i = 0; i < 64; i++ ) {
			sockaddr_storage addr = {};
			socklen_t length = sizeof( addr );

			SOCKET client = accept( s.listenSock, reinterpret_cast<sockaddr *>( &addr ), &length );
			if ( client == INVALID_SOCKET )
				return;

			if ( s.stopping || s.peers.size() >= MAX_PEERS ) {
				closesocket( client );
				continue;
			}

			SetNonBlocking( client );

			int one = 1;
			setsockopt( client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>( &one ), sizeof( one ) );

			auto peer = std::make_unique<Peer>();
			peer->sock = client;
			peer->connectedAt = Now();
			peer->lastActivity = peer->connectedAt;

			char text[64] = {};
			if ( addr.ss_family == AF_INET )
				inet_ntop( AF_INET, &reinterpret_cast<sockaddr_in *>( &addr )->sin_addr, text, sizeof( text ) );
			else if ( addr.ss_family == AF_INET6 )
				inet_ntop( AF_INET6, &reinterpret_cast<sockaddr_in6 *>( &addr )->sin6_addr, text, sizeof( text ) );

			peer->ip = text;
			s.peers.push_back( std::move( peer ) );
		}
	}



	static void ReadPeer( Server &s, Peer &p ) {
		const int n = recv( p.sock, s.readBuffer.data(), static_cast<int>( s.readBuffer.size() ), 0 );

		if ( n > 0 ) {
			p.lastActivity = Now();
			p.pingSent = false;

			// Closing: whatever the peer still sends is discarded.
			if ( p.closing )
				return;

			p.in.append( s.readBuffer.data(), static_cast<size_t>( n ) );

			if ( p.upgraded )
				ParseFrames( s, p );
			else if ( p.isClient )
				HandleClientHandshake( s, p );
			else
				HandleHandshake( s, p );

			return;
		}

		if ( n < 0 && WSAGetLastError() == WSAEWOULDBLOCK )
			return;

		// The peer is gone (n == 0: it hung up; n < 0: the connection broke).
		if ( n == 0 && p.closeSent )
			DropPeer( s, p, p.closeCode, p.closeReason );
		else
			DropPeer( s, p, 1006, std::string_view() );
	}



	// ------------------------------------------------------------------
	// Commands from Lua
	// ------------------------------------------------------------------

	static void BeginStop( Server &s ) {
		if ( s.stopping )
			return;

		s.stopping = true;
		s.reconnectPending = false;
		s.stopDeadline = Now() + STOP_TIMEOUT_MS;

		if ( s.listenSock != INVALID_SOCKET ) {
			closesocket( s.listenSock );
			s.listenSock = INVALID_SOCKET;
		}

		for ( auto &peer : s.peers ) {
			if ( peer->upgraded )
				QueueClose( *peer, 1001, std::string_view() );
			else
				DropPeer( s, *peer, 1001, std::string_view() );
		}
	}



	static void ApplyCommands( Server &s ) {
		std::deque<core::Command> batch;
		{
			std::lock_guard<std::mutex> lock( s.ctx.commandMutex );
			batch.swap( s.ctx.commands );
		}

		for ( core::Command &cmd : batch ) {
			if ( cmd.type == core::Command::STOP ) {
				BeginStop( s );
				continue;
			}

			if ( s.stopping )
				continue;

			const int opcode = cmd.binary ? 2 : 1;

			if ( cmd.type == core::Command::BROADCAST ) {
				for ( auto &peer : s.peers ) {
					if ( peer->upgraded && !peer->closing )
						QueueFrame( s, *peer, opcode, cmd.data );
				}

				continue;
			}

			if ( cmd.type == core::Command::PUBLISH ) {
				auto topic = s.topics.find( cmd.topic );
				if ( topic == s.topics.end() )
					continue;

				// A connection never receives what it publishes itself.
				Peer *sender = nullptr;
				if ( cmd.connId != 0 ) {
					auto it = s.byConn.find( cmd.connId );
					if ( it != s.byConn.end() )
						sender = it->second;
				}

				for ( Peer *subscriber : topic->second ) {
					if ( subscriber != sender && !subscriber->closing )
						QueueFrame( s, *subscriber, opcode, cmd.data );
				}

				continue;
			}

			// A client has one connection: Lua does not know its id (it always says 0).
			if ( s.cfg.isClient ) {
				if ( s.byConn.empty() ) {
					if ( cmd.type == core::Command::CLOSE )
						BeginStop( s ); // still connecting: give up

					continue;
				}

				cmd.connId = s.byConn.begin()->first;
			}

			auto it = s.byConn.find( cmd.connId );
			if ( it == s.byConn.end() )
				continue; // closed in the meantime

			Peer &p = *it->second;
			if ( p.closing )
				continue;

			switch ( cmd.type ) {
				case core::Command::SEND:
					QueueFrame( s, p, opcode, cmd.data );
					break;

				case core::Command::CLOSE:
					s.userClosed = true; // a requested close is never followed by a reconnection
					QueueClose( p, cmd.code, cmd.data );
					break;

				case core::Command::SUBSCRIBE:
					s.topics[cmd.topic].insert( &p );
					p.topics.insert( cmd.topic );
					break;

				case core::Command::UNSUBSCRIBE: {
					auto topic = s.topics.find( cmd.topic );

					if ( topic != s.topics.end() ) {
						topic->second.erase( &p );

						if ( topic->second.empty() )
							s.topics.erase( topic );
					}

					p.topics.erase( cmd.topic );
					break;
				}

				default:
					break;
			}
		}
	}



	// ------------------------------------------------------------------
	// The server
	// ------------------------------------------------------------------

	static std::string ListenError( const core::Config &cfg, int attempts, int code ) {
		std::string error = "Could not listen on " + cfg.host + ":" + std::to_string( cfg.port );

		if ( attempts > 1 )
			error += " (nor on the next " + std::to_string( attempts - 1 ) + " ports)";

		return error + ". Port in use, or the address is not available (error " + std::to_string( code ) + ").";
	}



	// Per-peer timers: handshake timeout, close timeout, idle timeout and pings.
	static void CheckTimers( Server &s ) {
		const ULONGLONG now = Now();

		for ( auto &peer : s.peers ) {
			Peer &p = *peer;
			if ( p.dead )
				continue;

			// Client: connect + handshake share one deadline.
			if ( p.isClient && !p.upgraded && !p.closing && now > p.connectDeadline ) {
				ClientFail( s, p, p.connecting ? "Connecting to " + HostPort( s.cfg ) + " timed out" : "The handshake timed out" );
				continue;
			}

			if ( p.closing ) {
				if ( now > p.closeDeadline )
					DropPeer( s, p, p.closeSent ? p.closeCode : 1006, p.closeSent ? std::string_view( p.closeReason ) : std::string_view() );

				continue;
			}

			if ( !p.upgraded ) {
				if ( !p.isClient && now - p.connectedAt > HANDSHAKE_TIMEOUT_MS )
					DropPeer( s, p, 1006, std::string_view() );

				continue;
			}

			if ( s.cfg.idleTimeout > 0 ) {
				const ULONGLONG idle = now - p.lastActivity;
				const ULONGLONG limit = static_cast<ULONGLONG>( s.cfg.idleTimeout ) * 1000;

				if ( idle > limit )
					DropPeer( s, p, 1006, std::string_view() );

				// Half way to the limit: ask the peer to prove it is alive.
				else if ( s.cfg.sendPings && !p.pingSent && idle > limit / 2 ) {
					if ( QueueFrame( s, p, 9, std::string_view() ) )
						p.pingSent = true;
				}
			}
		}
	}



	// The poll loop shared by servers and clients. Returns when everything is closed.
	static void RunLoop( Server &s ) {
		core::Context &ctx = s.ctx;
		const core::Config &cfg = s.cfg;

		std::vector<WSAPOLLFD> fds;
		std::vector<Peer *> polled;
		char drain[64];

		// A client is done when its only connection is gone.
		while ( !( s.stopping && ( s.peers.empty() || Now() >= s.stopDeadline ) ) && !( cfg.isClient && s.peers.empty() && !s.reconnectPending ) ) {
			// ---- What to wait for ------------------------------------------------
			fds.clear();
			polled.clear();

			WSAPOLLFD entry = {};

			entry.fd = s.waker.sock;
			entry.events = POLLRDNORM;
			fds.push_back( entry );

			if ( s.listenSock != INVALID_SOCKET ) {
				entry.fd = s.listenSock;
				fds.push_back( entry );
			}

			const size_t firstPeer = fds.size();
			bool anyConnecting = false;

			for ( auto &peer : s.peers ) {
				if ( peer->dead )
					continue;

				// A connecting socket is watched by CheckConnecting().
				if ( peer->connecting ) {
					anyConnecting = true;
					continue;
				}

				entry.fd = peer->sock;
				entry.events = POLLRDNORM | ( Pending( *peer ) > 0 ? POLLWRNORM : 0 );
				fds.push_back( entry );
				polled.push_back( peer.get() );
			}

			int timeout = anyConnecting ? CONNECT_POLL_MS : POLL_TIMEOUT_MS;

			// Wake up in time for the next reconnection attempt.
			if ( s.reconnectPending ) {
				const ULONGLONG now = Now();
				const int left = s.reconnectAt > now ? static_cast<int>( std::min<ULONGLONG>( s.reconnectAt - now, POLL_TIMEOUT_MS ) ) : 0;
				timeout = std::min( timeout, left );
			}

			const int ready = WSAPoll( fds.data(), static_cast<ULONG>( fds.size() ), timeout );
			if ( ready == SOCKET_ERROR )
				throw std::runtime_error( "WSAPoll failed (error " + std::to_string( WSAGetLastError() ) + ")" );

			// ---- Commands from Lua (every round, whether or not the wake-up arrived) -
			if ( fds[0].revents & POLLRDNORM ) {
				while ( recv( s.waker.sock, drain, sizeof( drain ), 0 ) > 0 ) {
				}
			}

			ctx.wakePending.store( false );
			ApplyCommands( s );

			if ( s.reconnectPending && !s.stopping && Now() >= s.reconnectAt ) {
				s.reconnectPending = false;
				TryConnect( s );
			}

			CheckConnecting( s );

			// ---- Network ---------------------------------------------------------
			for ( size_t i = 0; i < polled.size(); i++ ) {
				Peer &p = *polled[i];
				const short revents = fds[firstPeer + i].revents;

				if ( p.dead || revents == 0 )
					continue;

				if ( revents & POLLNVAL )
					DropPeer( s, p, 1006, std::string_view() );

				else if ( revents & ( POLLRDNORM | POLLHUP | POLLERR ) )
					ReadPeer( s, p );
			}

			if ( s.listenSock != INVALID_SOCKET && ( fds[1].revents & POLLRDNORM ) )
				AcceptPeers( s );

			CheckTimers( s );

			for ( auto &peer : s.peers ) {
				if ( !peer->dead && !peer->connecting && ( Pending( *peer ) > 0 || peer->closing ) )
					Flush( s, *peer );
			}

			s.peers.erase( std::remove_if( s.peers.begin(), s.peers.end(), []( const std::unique_ptr<Peer> &p ) {
				return p->dead;
			} ), s.peers.end() );

			ClientAttemptEnded( s );
		}

		// Whatever is left is closed now.
		for ( auto &peer : s.peers )
			DropPeer( s, *peer, 1001, std::string_view() );
	}



	static void Serve( core::Context &ctx ) {
		const core::Config &cfg = ctx.config;
		Server s( ctx );

		// ---- Listen: 127.0.0.1 by default, the next ports when one is busy ------
		const int attempts = cfg.port == 0 ? 1 : cfg.portTries;
		int errorCode = 0;
		int port = cfg.port;

		for ( int i = 0; i < attempts && s.listenSock == INVALID_SOCKET; i++ ) {
			port = cfg.port + i;
			if ( port > 65535 )
				break;

			s.listenSock = OpenListener( cfg.host, port, errorCode );

			// Only a busy port is worth another try.
			if ( s.listenSock == INVALID_SOCKET && errorCode != WSAEADDRINUSE && errorCode != WSAEACCES )
				break;
		}

		if ( s.listenSock == INVALID_SOCKET ) {
			SignalStart( ctx, false, ListenError( cfg, attempts, errorCode ) );
			return;
		}

		int boundPort = port;
		if ( cfg.port == 0 ) {
			sockaddr_storage local = {};
			socklen_t length = sizeof( local );

			if ( getsockname( s.listenSock, reinterpret_cast<sockaddr *>( &local ), &length ) == 0 )
				boundPort = ntohs( local.ss_family == AF_INET6 ? reinterpret_cast<sockaddr_in6 *>( &local )->sin6_port : reinterpret_cast<sockaddr_in *>( &local )->sin_port );
		}

		if ( !OpenWaker( s.waker ) ) {
			closesocket( s.listenSock );
			SignalStart( ctx, false, "Could not create the wake-up socket (error " + std::to_string( WSAGetLastError() ) + ")" );
			return;
		}

		{
			std::lock_guard<std::mutex> lock( ctx.loopMutex );
			ctx.waker = &s.waker;
		}

		ctx.boundPort.store( boundPort );
		ctx.running.store( true );
		SignalStart( ctx, true, std::string() );

		RunLoop( s );

		if ( s.listenSock != INVALID_SOCKET )
			closesocket( s.listenSock );

		// The loop is over: nothing may reach the locals above any more.
		ctx.running.store( false );
		{
			std::lock_guard<std::mutex> lock( ctx.loopMutex );
			ctx.waker = nullptr;
		}

		closesocket( s.waker.sock );
	}



	// Thread body of a client: connect, then run the same loop as a server.
	static void ServeClient( core::Context &ctx ) {
		Server s( ctx );

		if ( !OpenWaker( s.waker ) ) {
			SignalStart( ctx, false, "Could not create the wake-up socket (error " + std::to_string( WSAGetLastError() ) + ")" );
			return;
		}

		{
			std::lock_guard<std::mutex> lock( ctx.loopMutex );
			ctx.waker = &s.waker;
		}

		ctx.boundPort.store( ctx.config.port );
		ctx.running.store( true );

		// The connect is asynchronous: connect() returns at once and the result arrives as events.
		SignalStart( ctx, true, std::string() );

		TryConnect( s );
		ClientAttemptEnded( s ); // an immediate failure (host not found...) may already be over
		RunLoop( s );

		ctx.running.store( false );
		{
			std::lock_guard<std::mutex> lock( ctx.loopMutex );
			ctx.waker = nullptr;
		}

		closesocket( s.waker.sock );
	}



	void Run( std::shared_ptr<core::Context> ctx ) {
		std::string failure;

		WSADATA wsa;
		if ( WSAStartup( MAKEWORD( 2, 2 ), &wsa ) != 0 ) {
			SignalStart( *ctx, false, "WSAStartup failed" );
			return;
		}

		try {
			if ( ctx->config.isClient )
				ServeClient( *ctx );
			else
				Serve( *ctx );
		}

		catch ( const std::exception &e ) {
			failure = e.what();
		}

		catch ( ... ) {
			failure = "Unknown error in the WebSocket server thread";
		}

		// Whatever happened, the loop is no longer reachable.
		ctx->running.store( false );
		{
			std::lock_guard<std::mutex> lock( ctx->loopMutex );
			ctx->waker = nullptr;
		}

		// Answers a start-up that is still waiting (no effect once it has been answered).
		SignalStart( *ctx, false, failure.empty() ? std::string( "The server stopped before it was ready" ) : failure );

		if ( !failure.empty() )
			PostEvent( *ctx, core::Event::SERVER_ERROR, 0, failure );

		WSACleanup();
	}



	void Wake( core::Context &ctx ) {
		if ( ctx.wakePending.exchange( true ) )
			return;

		// Held while sending: the server thread takes the same lock before it closes the socket.
		std::lock_guard<std::mutex> lock( ctx.loopMutex );

		const Waker *waker = static_cast<const Waker *>( ctx.waker );
		if ( !waker ) {
			ctx.wakePending.store( false );
			return;
		}

		// A full buffer means a wake-up is already waiting; the loop also runs
		// the commands at least every POLL_TIMEOUT_MS.
		const char byte = 1;
		sendto( waker->sock, &byte, 1, 0, reinterpret_cast<const sockaddr *>( &waker->addr ), sizeof( waker->addr ) );
	}



	void Post( core::Context &ctx, core::Command &&command ) {
		{
			std::lock_guard<std::mutex> lock( ctx.commandMutex );
			ctx.commands.push_back( std::move( command ) );
		}

		Wake( ctx );
	}



	void Stop( core::Context &ctx ) {
		core::Command stop;
		stop.type = core::Command::STOP;

		Post( ctx, std::move( stop ) );
	}

} // namespace websocket::network
