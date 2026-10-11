/**
 * @file core.cpp
 * @brief Core implementation for fetch module
 * @license GPL v2.0 License
 */

#include "core.hpp"
#include <Includes/rain.hpp>
#include <RainmeterAPI.hpp>
#include <utils/strings.hpp>

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

			if ( !cancelled.load() ) {
				NetHandle h{ handle, closer };
				netHandles.push_back( h );
				return true;
			}
		}

		// Cancelled before the handle could be tracked: close it right away so the
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
	}



	size_t ContextRegistry::count() {
		std::lock_guard<std::mutex> lock( mutex_ );
		return contexts_.size();
	}



	void CleanupContexts( Rain *rain ) {
		ContextRegistry::instance().removeAllByRain( rain );
	}
} // namespace core
