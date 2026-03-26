

--- Cubic Bézier easing function.
-- Similar to CSS cubic-bezier() timing function.
-- @param p0 number Control point 1 x (0-1)
-- @param p1 number Control point 1 y
-- @param p2 number Control point 2 x (0-1)
-- @param p3 number Control point 2 y
-- @return function Easing function
--
local cubicBezier = function(p0, p1, p2, p3)
	-- Pre-calc some values for performance
	local cx = 3 * p0
	local bx = 3 * (p2 - p0) - cx
	local ax = 1 - cx - bx

	local cy = 3 * p1
	local by = 3 * (p3 - p1) - cy
	local ay = 1 - cy - by

	-- Helper function to solve cubic equation for x
	local function sampleCurveX(t)
		return ((ax * t + bx) * t + cx) * t
	end

	-- Helper function to solve cubic equation for y
	local function sampleCurveY(t)
		return ((ay * t + by) * t + cy) * t
	end

	-- Newton-Raphson method to find t for given x
	local function solveCurveX(x)
		local t0, t1, t2, x2, d2
		local i = 0

		-- First try a few iterations of bisection method for robustness
		t0 = 0
		t1 = 1
		t2 = x

		if t2 < t0 then return t0 end
		if t2 > t1 then return t1 end

		-- Use Newton-Raphson method
		for i = 1, 8 do
			x2 = sampleCurveX(t2) - x
			if math.abs(x2) < 0.00001 then
				return t2
			end

			-- Derivative: 3ax² + 2bx + c
			d2 = (3 * ax * t2 + 2 * bx) * t2 + cx

			if math.abs(d2) < 0.00001 then
				break
			end

			t2 = t2 - x2 / d2
		end

		-- Fallback to binary subdivision
		t0 = 0
		t1 = 1
		t2 = x

		if t2 < t0 then return t0 end
		if t2 > t1 then return t1 end

		while t0 < t1 do
			x2 = sampleCurveX(t2)
			if math.abs(x2 - x) < 0.00001 then
				return t2
			end

			if x > x2 then
				t0 = t2
			else
				t1 = t2
			end

			t2 = (t1 - t0) * 0.5 + t0
		end

		return t2
	end

	-- The actual easing function
	return function(t, b, c, d)
		if t <= 0 then return b end
		if t >= d then return b + c end

		-- Normalize time to 0-1 range
		local x = t / d

		-- Find t that gives us this x
		local tSolved = solveCurveX(x)

		-- Get y value at this t
		local y = sampleCurveY(tSolved)

		-- Apply to output range
		return b + c * y
	end
end



-- Add to easing table
--- Creates a cubic-bezier easing function.
-- @param x1 number Control point 1 x (0-1)
-- @param y1 number Control point 1 y
-- @param x2 number Control point 2 x (0-1)
-- @param y2 number Control point 2 y
-- @return function Easing function
-- easing.cubicbezier = function(p0, p1, p2, p3)
-- 	return cubicBezier(p0, p1, p2, p3)
-- end


return cubicBezier