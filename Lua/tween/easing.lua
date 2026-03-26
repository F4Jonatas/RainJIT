-- if not debug.getinfo(3) then
-- 	print("This is a module to load with `require('turtle')`.")
-- end


--- Collection of easing functions.
-- Each easing function follows the signature:
--   easing(time, begin, change, duration [, extra parameters])
--
-- DOCS:
--   https://joshondesign.com/2013/03/01/improvedEasingEquations
--   https://spicyyoghurt.com/tools/easing-functions
--   https://www.gizma.com/easing/
--
local easing
easing = {
	calculatePAS = function( p, a, c, d )
		p = p or d * 0.3
		a = a or 0

		-- p, a, s
		if a < math.abs( c ) then
			return p, c, p / 4
		end

		-- p, a, s
		return p, a, p / ( 2 * math.pi ) * math.asin( c / a )
	end,



	-- The interpolation is done at a constant rate from beginning to end.
	linear = function( time, begin, change, duration )
		return change * time / duration + begin
	end,



	inquad = function( time, begin, change, duration )
		return change * math.pow( time / duration, 2 ) + begin
	end,



	outquad = function( time, begin, change, duration )
		time = time / duration

		return -change * time * ( time - 2 ) + begin
	end,



	-- https://stackoverflow.com/questions/38497765/pure-javascript-animation-easing
	inoutquad = function( time, begin, change, duration )
		time = time / duration * 2

		if time < 1 then
			return change / 2 * math.pow( time, 2 ) + begin
		end

		return -change / 2 * (( time - 1 ) * ( time - 3 ) - 1 ) + begin
	end,



	outinquad = function( time, begin, change, duration )
		if time < duration / 2 then
			return easing.outquad( time * 2, begin, change / 2, duration )
		end

		return easing.inquad(( time * 2) - duration, begin + change / 2, change / 2, duration )
	end,



	incubic = function( time, begin, change, duration )
		return change * math.pow( time / duration, 3 ) + begin
	end,



	outcubic = function( time, begin, change, duration )
		return change * ( math.pow(( time / duration ) - 1, 3 ) + 1 ) + begin
	end,



	inoutcubic = function( time, begin, change, duration )
		time = time / duration * 2

		if time < 1 then
			return change / 2 * time * time * time + begin
		end

		time = time - 2
		return change / 2 * ( time * time * time + 2 ) + begin
	end,



	outincubic = function( time, begin, change, duration )
		if time < duration / 2 then
			return easing.outcubic( time * 2, begin, change / 2, duration )
		end

		return easing.incubic(( time * 2 ) - duration, begin + change / 2, change / 2, duration )
	end,



	inquart = function( time, begin, change, duration )
		return change * math.pow( time / duration, 4 ) + begin
	end,



	outquart = function( time, begin, change, duration )
		return -change * ( math.pow( time / duration - 1, 4 ) - 1 ) + begin
	end,



	inoutquart = function( time, begin, change, duration )
		time = time / duration * 2

		if time < 1 then
			return change / 2 * math.pow( time, 4 ) + begin
		end

		return -change / 2 * ( math.pow( time - 2, 4 ) - 2 ) + begin
	end,



	outinquart = function( time, begin, change, duration )
		if time < duration / 2 then
			return easing.outquart( time * 2, begin, change / 2, duration )
		end

		return easing.inquart(( time * 2 ) - duration, begin + change / 2, change / 2, duration )
	end,



	inquint = function( time, begin, change, duration )
		return change * math.pow( time / duration, 5 ) + begin
	end,



	outquint = function( time, begin, change, duration )
		return change * ( math.pow( time / duration - 1, 5 ) + 1 ) + begin
	end,



	inoutquint = function( time, begin, change, duration )
		time = time / duration * 2

		if time < 1 then
			return change / 2 * math.pow( time, 5 ) + begin
		end

		return change / 2 * ( math.pow( time - 2, 5 ) + 2 ) + begin
	end,



	outinquint = function( t, b, c, d )
		if t < d / 2 then
			return easing.outquint( t * 2, b, c / 2, d )
		end

		return easing.inquint(( t * 2 ) - d, b + c / 2, c / 2, d )
	end,



	insine = function( t, b, c, d )
		return -c * math.cos( t / d * ( math.pi / 2 )) + c + b
	end,



	outsine = function( t, b, c, d )
		return c * math.sin( t / d * ( math.pi / 2 )) + b
	end,



	inoutsine = function( t, b, c, d )
		return -c / 2 * ( math.cos( math.pi * t / d ) - 1 ) + b
	end,



	outinsine = function( t, b, c, d )
		if t < d / 2 then
			return easing.outsine( t * 2, b, c / 2, d )
		end

		return easing.insine(( t * 2 ) -d, b + c / 2, c / 2, d )
	end,



	inexpo = function( t, b, c, d )
		if t == 0 then
			return b
		end

		return c * math.pow( 2, 10 * ( t / d - 1 )) + b - c * 0.001
	end,



	outexpo = function( t, b, c, d )
		if t == d then
			return b + c
		end

		return c * 1.001 * ( -math.pow( 2, -10 * t / d ) + 1 ) + b
	end,



	inoutexpo = function( t, b, c, d )
		if t == 0 then
			return b
		end

		if t == d then
			return b + c
		end

		t = t / d * 2

		if t < 1 then
			return c / 2 * math.pow( 2, 10 * ( t - 1 )) + b - c * 0.0005
		end

		return c / 2 * 1.0005 * ( -math.pow( 2, -10 * ( t - 1 )) + 2 ) + b
	end,



	outinexpo = function( t, b, c, d )
		if t < d / 2 then
			return easing.outexpo( t * 2, b, c / 2, d )
		end

		return easing.inexpo(( t * 2 ) - d, b + c / 2, c / 2, d )
	end,



	incirc = function( time, begin, change, duration )
		return ( -change * ( math.sqrt( 1 - math.pow( time / duration, 2 )) - 1 ) + begin )
	end,



	-- https://easings.net/#easeOutCirc
	outcirc = function( t, b, c, d )
		return ( c * math.sqrt( 1 - math.pow(( t / d ) - 1, 2 )) + b )
	end,



	inoutcirc = function( t, b, c, d )
		t = t / d * 2

		if t < 1 then
			return -c / 2 * ( math.sqrt( 1 - t * t ) - 1 ) + b
		end

		t = t - 2

		return c / 2 * ( math.sqrt( 1 - t * t ) + 1 ) + b
	end,



	outincirc = function( t, b, c, d )
		if t < d / 2 then
			return easing.outcirc( t * 2, b, c / 2, d )
		end

		return easing.incirc(( t * 2 ) - d, b + c / 2, c / 2, d )
	end,



	inelastic = function( t, b, c, d, a, p )
		local s

		if t == 0 then
			return b
		end

		t = t / d

		if t == 1 then
			return b + c
		end

		p, a, s = easing.calculatePAS( p, a, c, d )
		t = t - 1

		return -( a * math.pow( 2, 10 * t ) * math.sin(( t * d - s ) * ( 2 * math.pi ) / p )) + b
	end,



	outelastic = function( t, b, c, d, a, p )
		local s

		if t == 0 then
			return b
		end

		t = t / d
		if t == 1 then
			return b + c
		end

		p, a, s = easing.calculatePAS( p, a, c, d )

		return a * math.pow( 2, -10 * t ) * math.sin(( t * d - s ) * ( 2 * math.pi ) / p) + c + b
	end,



	inoutelastic = function( t, b, c, d, a, p )
		local s

		if t == 0 then
			return b
		end

		t = t / d * 2

		if t == 2 then
			return b + c
		end

		p, a, s = easing.calculatePAS( p, a, c, d )
		t = t - 1

		if t < 0 then
			return -0.5 * ( a * math.pow( 2, 10 * t ) * math.sin(( t * d - s ) * ( 2 * math.pi ) / p )) + b
		end

		return a * math.pow( 2, -10 * t ) * math.sin(( t * d - s ) * ( 2 * math.pi ) / p ) * 0.5 + c + b
	end,



	outinelastic = function( t, b, c, d, a, p )
		if t < d / 2 then
			return easing.outelastic( t * 2, b, c / 2, d, a, p )
		end

		return easing.inelastic(( t * 2 ) - d, b + c / 2, c / 2, d, a, p )
	end,



	inback = function( t, b, c, d, s )
		s = s or 1.70158
		t = t / d
		return c * t * t * (( s + 1 ) * t - s ) + b
	end,



	outback = function( t, b, c, d, s )
		s = s or 1.70158
		t = t / d - 1
		return c * ( t * t * (( s + 1 ) * t + s ) + 1 ) + b
	end,



	inoutback = function( t, b, c, d, s )
		s = ( s or 1.70158 ) * 1.525
		t = t / d * 2
		if t < 1 then return c / 2 * ( t * t * (( s + 1 ) * t - s )) + b end
		t = t - 2
		return c / 2 * ( t * t * (( s + 1 ) * t + s ) + 2 ) + b
	end,



	outinback = function( t, b, c, d, s )
		if t < d / 2 then return easing.outback( t * 2, b, c / 2, d, s ) end
		return easing.inback(( t * 2 ) - d, b + c / 2, c / 2, d, s )
	end,



	outbounce = function( t, b, c, d )
		t = t / d

		if t < 1 / 2.75 then
			return c * ( 7.5625 * t * t ) + b
		end


		if t < 2 / 2.75 then
			t = t - ( 1.5 / 2.75 )
			return c * ( 7.5625 * t * t + 0.75 ) + b

		elseif t < 2.5 / 2.75 then
			t = t - ( 2.25 / 2.75 )
			return c * ( 7.5625 * t * t + 0.9375 ) + b
		end

		t = t - ( 2.625 / 2.75 )
		return c * ( 7.5625 * t * t + 0.984375 ) + b
	end,



	inbounce = function( t, b, c, d )
		return c - easing.outbounce( d - t, 0, c, d ) + b
	end,



	inoutbounce = function( t, b, c, d )
		if t < d / 2 then
			return easing.inbounce( t * 2, 0, c, d ) * 0.5 + b
		end

		return easing.outbounce( t * 2 - d, 0, c, d ) * 0.5 + c * .5 + b
	end,



	outinbounce = function( t, b, c, d )
		if t < d / 2 then
			return easing.outbounce( t * 2, b, c / 2, d )
		end

		return easing.inbounce(( t * 2 ) - d, b + c / 2, c / 2, d )
	end
}



return easing