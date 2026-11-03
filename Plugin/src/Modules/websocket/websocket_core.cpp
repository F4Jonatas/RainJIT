/**
 * @file websocket_core.cpp
 * @brief Core implementation for websocket module
 * @license GPL v2.0 License
 */

#include "websocket_core.hpp"
#include "websocket_network.hpp"

#include <Includes/rain.hpp>
#include <RainmeterAPI.hpp>






namespace websocket::core {

	// Context implementation
	Context::Context( Rain *r ) :
		rain( r ) {
		for ( int i = 0; i < Event::COUNT; i++ ) {
			refHandlers[i] = LUA_NOREF;
			wanted[i].store( false );
		}
	}



	Context::~Context() {
		releaseLuaRefs();
	}



	void Context::releaseLuaRefs() {
		bool any = refSelfLua != LUA_NOREF || !connRefs.empty();

		for ( int i = 0; i < Event::COUNT && !any; i++ )
			any = refHandlers[i] != LUA_NOREF;

		// Nothing to release: do not touch `rain` at all. It may already be gone
		// when the last reference to this context is dropped.
		if ( !any )
			return;

		if ( rain && rain->L ) {
			lua_State *L = rain->L;

			for ( int i = 0; i < Event::COUNT; i++ ) {
				if ( refHandlers[i] != LUA_NOREF ) {
					luaL_unref( L, LUA_REGISTRYINDEX, refHandlers[i] );
					refHandlers[i] = LUA_NOREF;
				}
			}

			for ( const auto &entry : connRefs )
				luaL_unref( L, LUA_REGISTRYINDEX, entry.second );

			connRefs.clear();

			if ( refSelfLua != LUA_NOREF ) {
				luaL_unref( L, LUA_REGISTRYINDEX, refSelfLua );
				refSelfLua = LUA_NOREF;
			}
		}

		else {
			for ( int i = 0; i < Event::COUNT; i++ )
				refHandlers[i] = LUA_NOREF;

			connRefs.clear();
			refSelfLua = LUA_NOREF;
		}
	}



	// ContextRegistry implementation
	ContextRegistry &ContextRegistry::instance() {
		static ContextRegistry instance;
		return instance;
	}



	int ContextRegistry::registerContext( std::shared_ptr<Context> ctx ) {
		std::lock_guard<std::mutex> lock( mutex_ );
		int id = nextId_++;
		contexts_[id] = ctx;
		return id;
	}



	std::shared_ptr<Context> ContextRegistry::getContext( int id ) {
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
		std::vector<std::shared_ptr<Context>> removed;

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

		// Stopped outside the registry lock: every server thread is asked to close
		// its sockets and leave its loop.
		for ( auto &ctx : removed ) {
			ctx->rainValid = false;
			ctx->stopped = true;

			if ( ctx->running.load() || ctx->workerAlive.load() > 0 )
				network::Stop( *ctx );
		}

		// The server threads are detached and run code that lives in this DLL, so
		// give them a short, bounded time to finish before the DLL can be unloaded.
		const ULONGLONG waitUntil = GetTickCount64() + 3000;
		for ( auto &ctx : removed ) {
			while ( ctx->workerAlive.load() > 0 && GetTickCount64() < waitUntil )
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
} // namespace websocket::core
