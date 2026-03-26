
-- Aggregates multiple asynchronous fetch requests and invokes a single callback
-- once all of them have completed.
--
-- This module collects the result of every registered fetch request, regardless
-- of success or failure, and delivers all responses to the final callback as a
-- single ordered table.
--
-- The execution order of the responses is deterministic and matches the order
-- in which the fetch requests were registered, not the order in which they
-- completed.
--
-- Unlike JavaScript's Promise.all, this implementation does not short-circuit
-- on failures. All requests are always allowed to complete, making the behavior
-- semantically closer to Promise.allSettled, while still exposing the raw fetch
-- responses without normalization or interpretation.
--
-- Error handling is intentionally transparent: failures in individual fetch
-- operations are not intercepted, and errors raised by the final callback are
-- reported back to the caller without suppressing internal state.
--
-- @submodule fetch.promiseAll


local fetch = require( 'fetch' )


-- @table promiseClass
--
-- Internal class that holds shared state for a promiseAll execution.
-- Each fetch request references this object through `f.super`.
local promiseClass = {

	-- @field fired
	-- @number
	-- Number of callbacks that have already been executed.
	fired = 0,

	-- @field length
	-- @number
	-- Total number of fetch requests registered in this promiseAll.
	length = 0,

	-- @field response_list
	-- @table
	-- Table containing all responses, indexed by fetch invocation order.
	response_list = {},

	-- @field response_dict
	-- @table
	-- Reserved for internal ordering or future extensions.
	-- Currently cleared after each execution cycle.
	response_dict = {},


	-- @field listener
	-- @function
	-- Common callback attached to every fetch request.
	-- Stores the response and triggers the final callback once all
	-- requests have completed.
	--
	-- @param f table
	-- The fetch object that triggered the callback.
	--
	-- @param resp any
	-- The response object returned by the fetch request.
	--
	listener = function( f, resp )
		f.super.fired = f.super.fired + 1
		f.super.response_list[ f.id ] = resp

		if f.super.fired == f.super.length then
			f.super.fired = 0
			f.super.length = 0

			local ok, msg = pcall( f.super.call, f.super.response_list )
			if not ok then
				msg = 'fetch.promiseAll an error occurred: '.. msg
				f.super.call({{ ok = false, msg = msg }})
			end

			f.super.response_list = {}
			f.super.response_dict = {}
		end
	end
}


-- @table meta
--
-- Metatable that makes the promiseAll object callable.
-- Each call registers a new asynchronous fetch request.
local meta = {
	__index  = promiseClass,

	-- @field autoSend
	-- @boolean
	-- Reserved for compatibility or future behavior changes.
	autoSend = true,


	-- Registers a new asynchronous fetch request in the current promiseAll.
	-- Calls can be chained.
	--
	-- @param (table) self - The promiseAll instance.
	-- @param (string) url - The request URL.
	-- @param (table|nil) options - Optional fetch options.
	--
	-- @treturn Returns the same promiseAll instance to allow chaining.
	--
	__call = function( self, url, options )
		self.length = self.length + 1

		local f = fetch.async( url, options )
		f.super = self
		f.id = self.length
		f:callback( self.listener )
		f:send()

		return self
	end
}



-- @function fetch.promiseAll
--
-- Creates a new promiseAll aggregator.
--
-- The provided callback will be executed once all registered fetch
-- requests have completed.
--
-- @param (function) callback - Function invoked with a table containing all fetch responses.
-- @param (boolean|nil) autoSend - Reserved parameter for future use.
--
-- @treturn A callable promiseAll instance.
--
fetch.promiseAll = function( callback, autoSend )
	return setmetatable({ call = callback }, meta )
end



-- @return table
-- The extended fetch module.
--
return fetch
