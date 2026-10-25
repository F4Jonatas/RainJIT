--- Animate meter extension.
--
-- High-level animation system built on top of the tween module.
-- Applies interpolated values directly to a meter instance.
--
-- ## Concept
--
-- This module acts as a **runtime animation layer**, separating:
--
-- - interpolation (handled by `tween`)
-- - application (handled here via `meter`)
-- - scheduling (handled via a global update loop)
--
-- Unlike simple tween usage, this module manages animations
-- automatically through a centralized scheduler.
--
-- ## Lifecycle
--
-- 1. Configure animation via `:from()` and `:to()`
-- 2. Configure optional delay via `:delay()`
-- 3. Call `:create()` to initialize and register animation
-- 4. Call `updateAll(dt)` once per frame
--
-- ## States (Web Animations API inspired)
--
-- - `idle`     → not started or reset
-- - `running`  → actively updating
-- - `paused`   → temporarily halted
-- - `finished` → completed
--
-- ## Example
--
-- ```lua
-- local animate = require("meter.animate")
--
-- local a = animate(meter, 1.0, "outQuad")
--   :from({ top = -255 })
--   :to({ top = 0 })
--   :delay(0.25)
--   :create()
--
-- function Update()
--   animate.updateAll(1/60)
-- end
-- ```
--
-- @submodule meter.animate
-- @author F4Jonatas
-- @license GPL v2.0 License
-- @release 0.0.3

local okTween, tween = pcall( require, 'tween' )
assert( okTween, 'Missing dependency: tween\n' .. tostring( tween ))




--- List of active animations
local active = {}



--- Animation instance.
-- @table M
-- @field meter (table) Target meter instance
-- @field duration (number) Animation duration
-- @field easing (string|function) Easing function used by the tween
-- @field playState (string) Current state: `"idle"`, `"running"`, `"paused"` or `"finished"`
-- @field clock (number) Elapsed time since the animation started (excludes delay)
-- @field delayTime (number) Configured delay before the animation starts
-- @field delayClock (number) Elapsed time during the delay phase
-- @field fromValues (table) Initial property values, set via `:from()`
-- @field toValues (table) Target property values, set via `:to()`
-- @field manual (boolean) If true, the animation is not updated by the global scheduler
-- @field tween (table) Underlying tween instance created in `:create()` / `:restart()`
local M = {}
M.__index = M



--- Cancel the animation and return to the initial state.
--
-- Stops the animation immediately, removes it from the scheduler,
-- and restores all animated values to their initial state.
--
-- This method fully interrupts the current execution.
--
-- ## Behavior
--
-- - Sets `playState` to `"idle"`
-- - Resets internal clock to `0`
-- - Restores values using the tween's initial state
-- - Removes the animation from the active scheduler list
--
-- Unlike `reset()`, this method guarantees that the animation
-- will no longer be updated automatically.
--
-- ## When to use
--
-- - When you want to completely stop an animation
-- - When you want to discard current progress
-- - Before reconfiguring (`:from()` / `:to()`)
--
-- ## Notes
--
-- - After calling `cancel()`, the animation must be restarted
--   using `:create()` or `:restart()` if needed.
-- - Safe to call multiple times.
--
-- @return (table) Animation instance
--
-- @usage Basic cancel
-- local a = animate(meter, 1.0)
--   :from({ x = 0 })
--   :to({ x = 100 })
--   :create()
--
-- -- later
-- a:cancel()
--
-- @usage Cancel inside update loop
-- function rain:update(au, dt)
--   animate.updateAll(dt)
--
--   if a.toValues.x > 50 then
--     a:cancel()
--   end
-- end
--
-- @usage Recreate after cancel
-- a:cancel()
-- a:create()
--
function M:cancel()
	self.playState = 'idle'
	self.clock = 0
	self.delayClock = 0

	if self.tween then
		local values = self.tween:reset()
		self:_apply( values )
	end

	-- Remove from scheduler
	for index = #active, 1, -1 do
		if active[ index ] == self then
			table.remove( active, index )
			break
		end
	end

	return self
end



--- Define initial values for the animation.
--
-- These values will be used as the starting point when the animation
-- is created or restarted. They are applied immediately after creation.
--
-- @param (table) attributes - Table of property names and their starting values
-- @return (table) Animation instance
--
function M:from( attributes )
	self.fromValues = attributes
	return self
end



--- Define target values for the animation.
--
-- These values are the end state that the animation will interpolate to.
--
-- @param (table) attributes - Table of property names and their target values
-- @return (table) Animation instance
--
function M:to( attributes )
	self.toValues = attributes
	return self
end



--- Define delay before animation starts.
--
-- The animation will wait for the specified amount of time before
-- beginning interpolation.
--
-- Delay time is affected by `pause()` and reset by `cancel()`,
-- `reset()`, and `restart()`.
--
-- @param (number) tick - Delay duration
-- @return (table) Animation instance
--
function M:delay( tick )
	self.delayTime = tick or 0
	return self
end



--- Update this animation manually.
--
-- Advances the animation independently of the global scheduler.
-- The animation must have been created with `manual = true` in `:create()`.
--
-- @param (number) dt - Delta time (time since last update)
-- @return (table) Animation instance
--
function M:update( dt )
	if not self.manual then
		error( '[animate] cannot manually update a non-manual animation' )
	end

	self:_update( dt )
	return self
end



--- Create and start the animation.
--
-- Initializes the tween and registers the animation into the global scheduler.
-- If `from` or `to` are missing, the animation is ignored.
--
-- @param (table) opts - Optional settings
-- @field (boolean) manual - If true, the animation will not be
--   added to the global scheduler and must be updated manually via
--   `:update()`. Defaults to false.
--
-- @return (table) Animation instance
--
function M:create( opts )
	opts = opts or {}
	self.manual = opts.manual or false

	if not self.fromValues or not self.toValues then
		print( '[animate] ignored: missing :from() or :to()' )
		return self
	end

	self.tween = tween(
		self.duration,
		self.fromValues,
		self.toValues,
		self.easing
	)

	self.clock = 0
	self.delayClock = 0
	self.playState = 'running'

	if self.manual == false then
		table.insert( active, self )
	end

	return self
end



--- Pause the animation.
--
-- Stops updating the animation while preserving current progress.
-- The animation can be resumed later with `:play()`.
--
-- @return (table) Animation instance
--
function M:pause()
	if self.playState == 'running' then
		self.playState = 'paused'
	end

	return self
end



--- Resume a paused animation.
--
-- Continues updating the animation from where it was paused.
-- Has no effect if the animation is already running or finished.
--
-- @return (table) Animation instance
--
function M:play()
	if self.playState == 'paused' then
		self.playState = 'running'
	end

	return self
end






--- Internal update for a single animation.
--
-- This method is called automatically by `updateAll`.
-- It advances time, updates tween values, and applies them.
--
-- @param (number) dt - Delta time
-- @local
--
function M:_update( dt )
	if self.playState ~= 'running' then
		return
	end

	if not self.tween then
		error( '[animate] animation not created. Call :create() first.' )
	end

	-- Delay phase
	if self.delayClock < self.delayTime then
		self.delayClock = self.delayClock + dt

		-- Still waiting
		if self.delayClock < self.delayTime then
			return
		end

		-- Consume overflow time
		dt = self.delayClock - self.delayTime
	end

	self.clock = self.clock + dt

	local values = self.tween:update( dt )

	self:_apply( values )

	if self.clock >= self.duration then
		self.playState = 'finished'
	end
end



--- Format number to avoid exponent notation.
-- The tween library returns exponent numbers because they are too large,
-- and Rainmeter does not accept exponents.
-- @local
--
local function formatNumber( value, decimal )
	decimal = decimal or 7
	return ('%.' ..decimal.. 'f'):format( value )
end



--- Check if an animation is currently active in the scheduler.
-- @param (table) Animation instance
-- @return boolean
--
local function isActive( self )
	for _, a in ipairs( active ) do
		if a == self then
			return true
		end
	end

	return false
end



--- Apply interpolated values to the meter.
--
-- Prioritizes executing available methods of the method and, if none exist, sets the property value.
-- Preference is given to methods because they are generally more complete than simply setting values.
--
-- @param (table) values - Interpolated values
--
function M:_apply( values )
	for key, value in pairs( values ) do
		key = key:lower()
		local val = formatNumber( value )


		if type( self.meter[ key ]) == 'function' then
			self.meter[ key ]( self.meter, val )

		else
			self.meter:option( key, val )
		end

	end

	self.meter:update()
end



--- Reset the animation to its initial state.
--
-- Rewinds the animation without restarting it. The animation will stay
-- in the `"idle"` state and will not be updated until a new call to
-- `:create()` or `:restart()` is made.
--
-- @return (table) Animation instance
--
function M:reset()
	if not self.tween then
		return self
	end

	local values = self.tween:reset()

	self.clock = 0
	self.delayClock = 0
	self.playState = 'idle'

	self:_apply( values )

	return self
end



--- Reverse the animation direction by swapping from/to values.
--
-- Exchanges the `fromValues` and `toValues` tables, so the next
-- execution (via :create() or :restart()) will play in reverse.
--
-- **Note:** If the animation is currently running, this method does
-- not affect the active tween. To reverse during playback, cancel
-- (:cancel()) first, then recreate or restart.
--
-- @return (table) Animation instance
--
-- @usage Setup and later play backwards
-- local a = animate(meter, 1.0)
--   :from({ x = 0 })
--   :to({ x = 100 })
--   :create()
-- -- when finished...
-- a:reverse()  -- now goes from 100 to 0
--
function M:reverse()
	self.fromValues, self.toValues = self.toValues, self.fromValues

	self:restart()
	return self
end



--- Restart the animation from the beginning.
--
-- Reinitializes the internal tween, resets the clock, and
-- re-registers the animation into the scheduler.
--
-- This method is different from `reset()`:
--
-- - `reset()` → only rewinds values and sets state to `idle`
-- - `restart()` → fully restarts execution (`running`)
--
-- If the animation has already finished, this is the correct
-- way to run it again.
--
-- Internally, this method:
--
-- 1. Recreates the tween instance
-- 2. Resets the internal clock
-- 3. Sets `playState` to `"running"`
-- 4. Adds the animation back to the active scheduler list
--
-- @return (table) Animation instance
--
-- @usage
-- local a = animate(meter, 1.0)
--   :from({ x = 0 })
--   :to({ x = 100 })
--   :create()
--
-- function Update()
--   animate.updateAll(dt)
--
--   if a.playState == "finished" then
--     a:restart()
--   end
-- end
--
-- @usage Loop animation manually
-- function Update()
--   animate.updateAll(dt)
--
--   if a.playState == "finished" then
--     a:restart()
--   end
-- end
--
-- @usage Restart after pause
-- a:pause()
-- -- later...
-- a:restart()
--
function M:restart()
	if not self.fromValues or not self.toValues then
		print( '[animate] restart ignored: missing :from() or :to()' )
		return self
	end

	self.tween = tween(
		self.duration,
		self.fromValues,
		self.toValues,
		self.easing
	)

	self.clock = 0
	self.delayClock = 0
	self.playState = 'running'

	-- Set default values
	self:_apply( self.fromValues )


	if not isActive( self ) then
		table.insert( active, self )
	end

	return self
end





local module = {}


--- Update all active animations.
--
-- This function must be called once per frame.
-- It iterates through all active animations and updates them.
--
-- Finished animations are automatically removed.
--
-- @param (number) dt - Delta time (time since last frame)
--
-- @usage
-- function Update()
--   animate.updateAll(1/60)
-- end
--
function module.updateAll( dt )
	for index = #active, 1, -1 do
		local a = active[ index ]

		if not a.manual then
			a:_update( dt )
		end

		if a.playState == 'finished' then
			table.remove( active, index )
		end
	end
end



--- Check whether all active animations have finished.
--
-- Iterates through the active scheduler list and returns `false`
-- as soon as any non-manual animation is not in the `"finished"`
-- state. Manual animations (`manual = true`) are ignored in this
-- check, since they are not advanced by the scheduler.
--
-- @return (boolean) `true` if every non-manual animation has finished, `false` otherwise.
--
-- @usage
-- function Update()
--   animate.updateAll(dt)
--
--   if animate.finished() then
--     print("All animations done")
--   end
-- end
--
function module.finished()
	for index = #active, 1, -1 do
		local a = active[ index ]

		if not a.manual then
			if a.playState ~= 'finished' then
				return false
			end
		end
	end

	return true
end






--- Create a new animation instance.
--
-- @param (table) meter - Target meter instance
-- @param (number) duration - Animation duration
-- @param (string|function) [easing="linear"] - Easing function
--
-- @return (table) Animation instance
--
return setmetatable( module, {
	__call = function( _, meter, duration, easing )
		return setmetatable({
			meter      = meter[ 'meter' ] or meter,
			duration   = duration,
			easing     = easing,
			playState  = 'idle',
			clock      = 0,

			delayTime  = 0,
			delayClock = 0,
		}, M )
	end
})