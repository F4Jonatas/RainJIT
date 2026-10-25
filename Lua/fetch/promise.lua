--- fetch.promiseAll
--
-- Aggregates multiple asynchronous fetch requests and invokes a single callback
-- once all of them have completed.
--
-- Every registered request is always allowed to finish, whether it succeeds or
-- fails (the same idea as JavaScript's Promise.allSettled). The final callback
-- receives the raw response objects of fetch, without any normalization, in a
-- plain list that can be walked with ipairs().
--
-- The order of the list is deterministic: it follows the order in which the
-- requests were registered, not the order in which they completed.
--
-- Errors are not intercepted. An error raised by the final callback is raised
-- exactly once and reaches Rainmeter's log as a fetch callback error; the
-- callback is never called a second time.
--
-- Every promiseAll keeps its own state, so several of them can run at the same
-- time without interfering with each other.
--
-- @usage
--   local promise = fetch.promiseAll( function( list )
--     for index, response in ipairs( list ) do
--       print( index, response.status )
--     end
--   end )
--
--   promise( 'https://a.example.com' )
--   promise( 'https://b.example.com', { timeout = 5000 } )
--
-- @submodule fetch.promiseAll
-- @author F4Jonatas
-- @license GPL v2.0 License
-- @release 1.2.0

local fetch = require( 'fetch' )


-- Private state of every promiseAll, indexed by the promiseAll object itself.
-- The fetch request / response objects are userdata and cannot carry extra
-- fields, and keeping the state here also keeps the object clean for the user.
-- Weak keys: the state disappears together with the promiseAll object.
local states = setmetatable( {}, { __mode = 'k' } )


-- Returns the private state of a promiseAll object.
local function stateOf( self )
	local state = states[ self ]
	if not state then
		error( 'fetch.promiseAll: invalid object (call it with : or ())', 3 )
	end

	return state
end


-- Clears everything that belongs to the current run. The generation counter is
-- increased so that a late callback from an old run is ignored.
local function reset( state )
	state.entries = {}
	state.results = {}
	state.fired = 0
	state.generation = state.generation + 1
end


-- Creates the callback of one request. It captures the promiseAll state, the
-- position of the request in the result list and the generation of the run, so
-- no field has to be stored in the request object.
local function makeListener( state, index, generation )
	return function( _, response )
		-- The run was cancelled (or already finished): ignore it.
		if state.generation ~= generation then
			return
		end

		state.results[ index ] = response
		state.fired = state.fired + 1

		if state.fired < #state.entries then
			return
		end

		-- Everything has completed. The state is cleared BEFORE the final callback
		-- runs, so the callback can reuse this promiseAll (register new requests)
		-- and an error in it cannot leave stale data behind.
		local results  = state.results
		local callback = state.callback
		reset( state )

		callback( results )
	end
end


-- Starts the entry that is at `index`, if it was not started yet.
local function start( state, index )
	local entry = state.entries[ index ]
	if entry and not entry.sent then
		entry.sent = true
		entry.request:send( makeListener( state, index, state.generation ) )
	end
end


-- Public methods of a promiseAll object
local methods = {}


--- promise:send()
--
-- Starts every registered request that was not started yet. It is only needed
-- when the promiseAll was created with autoSend = false; otherwise every request
-- is already started when it is registered, and this method does nothing.
--
-- @return The same promiseAll object (chainable).
function methods.send( self )
	local state = stateOf( self )

	for index = 1, #state.entries do
		start( state, index )
	end

	return self
end


--- promise:cancel()
--
-- Cancels every request that is still pending and discards the results. The
-- final callback is not called. The promiseAll can be used again afterwards.
--
-- @return The same promiseAll object (chainable).
function methods.cancel( self )
	local state = stateOf( self )

	for _, entry in ipairs( state.entries ) do
		entry.request:cancel()
	end

	reset( state )
	return self
end


-- Metatable that makes the promiseAll object callable.
local meta = {
	__index = methods,

	--- promise( url [, options] )
	--
	-- Registers a new asynchronous fetch request. Calls can be chained.
	--
	-- @param (table) self - The promiseAll object.
	-- @param (string) url - The request URL.
	-- @param (table|nil) options - Optional fetch options.
	--
	-- @return The same promiseAll object (chainable).
	__call = function( self, url, options )
		local state = stateOf( self )

		-- Created first: if the URL or the options are invalid, the error is raised
		-- here and nothing was registered, so the final callback still fires.
		local request = fetch.async( url, options )

		local index = #state.entries + 1
		state.entries[ index ] = { request = request, sent = false }

		if state.autoSend then
			start( state, index )
		end

		return self
	end
}


--- fetch.promiseAll( callback [, autoSend] )
--
-- Creates a new promiseAll aggregator.
--
-- The callback runs once, after every registered request has completed, and
-- receives a list with all the responses in registration order. Register all the
-- requests in the same piece of code: a request registered after the others have
-- already finished starts a new run.
--
-- @param (function) callback - Function called with the list of all responses.
-- @param (boolean|nil) autoSend - true (default): each request starts as soon as it
--   is registered. false: requests only start when promise:send() is called.
--
-- @return A callable promiseAll object.
fetch.promiseAll = function( callback, autoSend )
	if type( callback ) ~= 'function' then
		error( "fetch.promiseAll: 'callback' must be a function", 2 )
	end

	local self = setmetatable( {}, meta )

	states[ self ] = {
		callback   = callback,
		autoSend   = autoSend ~= false,
		entries    = {},
		results    = {},
		fired      = 0,
		generation = 0
	}

	return self
end


return fetch
