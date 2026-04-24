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
		std::lock_guard<std::mutex> lock( mutex_ );

		for ( auto it = contexts_.begin(); it != contexts_.end(); ) {
			if ( it->second->rain == rain ) {
				it->second->cancelled.store( true );
				it->second->rainValid = false;
				it = contexts_.erase( it );
			}

			else
				++it;
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
