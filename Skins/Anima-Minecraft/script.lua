
--- Original Skin
-- @see https://forum.rainmeter.net/viewtopic.php?t=45783


local meter  = require( 'meter' )
local anima  = require( 'meter.animate' )
local glass  = require( 'glass' )
local depot  = require( 'depot' )
local sunset = require( 'sunset' )



local dp     = depot()
local HEIGHT = rain:var( 'HEIGHT' )
local WIDTH  = rain:var( 'WIDTH'  )
local tick   = 200


local keyframe = {
	island = {
		from = { translateY = 0 },
		to   = { translateY = 10 }
	},

	clouds = {
		from = { translateX = -100 },
		to   = { translateX = WIDTH - 30 }
	}
}



if dp:get( 'glass', true ) then
	glass( rain.hwnd, { effect = 'acrylic', corners = 'round' })
end



-- Main Background
local colors = sunset.getColors()
local static = meter( 'static' )
	:add():rectangle( 0, 0, WIDTH, HEIGHT )
	:lgradient(('240 | %se6 ; 0.0 | %se6 ; 1.0')
		:format( colors.top, colors.bottom ))


-- Stars
local createdStars = false
if sunset.isNight() then
	createdStars = true

	local stars =
		{{ 20 , 30  },
		{  40 , 70  },
		{  50 , 160 },
		{  90 , 35  },
		{  130, 80  },
		{  160, 120 },
		{  220, 75  },
		{  310, 120 },
		{  380, 20  }}

	for index, area in ipairs( stars ) do
		local shape = static:add()
			:ellipse( area[1], area[2], 2 )
			:fill( 255, 255, 255, 220 )

		stars[ index ] = shape
	end
end


-- Moon and Moon Craters
static:add():ellipse( 90, 70, 30 ):fill( 244, 246, 240 )
	:add():ellipse( 105, 80, 7 ):fill( 224, 224, 224 )
	:add():ellipse(  80, 65, 5 ):fill( 224, 224, 224 )



-- Island Base (Soil) - Pixel Texture
local islandBase = meter( 'island' )
	:add():rectangle( 0,0,300,100  ):fill( 139,69,19 )
	:add():rectangle( 20,10,40,40  ):fill( 115,55,15 )
	:add():rectangle( 80,30,30,30  ):fill( 125,60,17 )
	:add():rectangle( 140,20,50,50 ):fill( 110,50,14 )
	:add():rectangle( 200,40,30,30 ):fill( 120,58,16 )
	:add():rectangle( 250,10,40,40 ):fill( 115,55,15 )
	:add():rectangle( 30,60,30,30  ):fill( 125,60,17 )
	:add():rectangle( 90,70,40,25  ):fill( 110,50,14 )
	:add():rectangle( 150,60,30,30 ):fill( 120,58,16 )
	:add():rectangle( 210,70,40,25 ):fill( 115,55,15 )


--- Island Grass - Pixel Texture
	:add():rectangle( -10,0,320,20 ):fill( 93,156,89 )
	:add():rectangle( 10,0,30,20   ):fill( 80,130,75 )
	:add():rectangle( 60,0,25,20   ):fill( 85,140,80 )
	:add():rectangle( 110,0,40,20  ):fill( 75,125,70 )
	:add():rectangle( 170,0,30,20  ):fill( 85,140,80 )
	:add():rectangle( 220,0,35,20  ):fill( 80,130,75 )
	:add():rectangle( 270,0,30,20  ):fill( 75,125,70 )

--- Mountains
	-- Main Mountain
	:add():rectangle( 70,-100,160,100 ):fill( 127,140,141 )
	:add():rectangle( 70,-100,160,30  ):fill( 255,255,255 )
	-- Left Mountain
	:add():rectangle( 10,-60,100,60  ):fill( 99,110,114 )
	:add():rectangle( 10,-60,100,20  ):fill( 255,255,255 )
	-- Right Mountain
	:add():rectangle( 190,-50,90,50  ):fill( 99,110,114 )
	:add():rectangle( 190,-50,90,15  ):fill( 255,255,255 )

--- Steve - Main Character
	:add():rectangle( 170,-115,40,40 ):fill( 241,218,193 )
	-- Hair
	:add():polygon({ 170,-116, 210,-116, 210,-97, 205,-97, 205,-106, 175,-106, 175,-97, 170,-97 }):fill( 139,69,19 )
	-- Eyes
	:add():rectangle( 180,-100,5,5 ):fill( 105,105,255 )
	:add():rectangle( 195,-100,5,5 ):fill( 105,105,255 )
	-- Mouth
	:add():rectangle( 185,-90,10,5 ):fill( 139,69,19 )
	-- Body
	:add():rectangle( 175,-75,30,40 ):fill( 0,112,192 )
	-- Arms
	:add():rectangle( 160,-75,15,30 ):fill( 241,218,193 )
	:add():rectangle( 205,-75,15,30 ):fill( 241,218,193 )
	-- Legs
	:add():rectangle( 175,-35,15,30 ):fill( 106,60,181 )
	:add():rectangle( 190,-35,15,30 ):fill( 106,60,181 )
	-- Feet
	:add():rectangle( 175,-5,15,5 ):fill( 139,69,19 )
	:add():rectangle( 190,-5,15,5 ):fill( 139,69,19 )

--- Cake in hand
	-- Cake base
	:add():rectangle( 215,-65,25,20 ):fill( 139,69,19 )
	-- Cake frosting
	:add():rectangle( 215,-65,25,5 ):fill( 255,255,255 )
	-- Cake decoration
	:add():rectangle( 220,-65,3,3 ):fill( 255,0,0 )
	:add():rectangle( 228,-65,3,3 ):fill( 255,0,0 )
	:add():rectangle( 236,-65,3,3 ):fill( 255,0,0 )
	:add():rectangle( 224,-63,3,3 ):fill( 255,0,0 )
	:add():rectangle( 232,-63,3,3 ):fill( 255,0,0 )


local clouds1 = meter( 'clouds1' )
	:add():polygon({
		0,65,10,65,10,60,30,60,30,65,50,65,50,60,70,60,70,65,90,65,90,60,110,60,110,65,120,65,
		120,85,110,85,110,90,90,90,90,85,70,85,70,90,50,90,50,85,30,85,30,90,10,90,10,85,0,85
	})
	:fill( 255,255,255,200 )

local clouds2 = meter( 'clouds2' )
	:add():rectangle( 0,105,100,15 ):fill( 255,255,255,170 )
	:add():rectangle( 10,100,20,25 ):fill( 255,255,255,170 )
	:add():rectangle( 40,100,20,25 ):fill( 255,255,255,170 )
	:add():rectangle( 70,100,20,25 ):fill( 255,255,255,170 )

local clouds3 = meter( 'clouds3' )
	:add():rectangle( 0,45,140,25 ):fill( 255,255,255,190 )
	:add():rectangle( 15,40,20,35 ):fill( 255,255,255,190 )
	:add():rectangle( 60,40,20,35 ):fill( 255,255,255,190 )
	:add():rectangle( 105,40,20,35 ):fill( 255,255,255,190 )



islandBase.anima = anima( islandBase, 500, 'OutQuad' )
	:from( keyframe.island.from )
	:to( keyframe.island.to )
	:create()

clouds1.anima = anima( clouds1, 2500, 'linear' )
	:from( keyframe.clouds.from )
	:to( keyframe.clouds.to )
	:create()

clouds2.anima = anima( clouds2, 5000, 'OutCubic' )
	:from( keyframe.clouds.from )
	:to( keyframe.clouds.to )
	:create()

clouds3.anima = anima( clouds3, 3000, 'linear' )
	:from( keyframe.clouds.from )
	:to( keyframe.clouds.to )
	:create()



-- @param (int)   au number accumulated Updates
-- @param (float) dt number deltaTime
function rain:update( au, dt )
	if islandBase.anima.playState == 'finished' then
		islandBase.anima:reverse()
	end

	if clouds1.anima.playState == 'finished' then
		clouds1.anima:reverse()
	end

	if clouds2.anima.playState == 'finished' then
		clouds2.anima:reverse()
	end

	if clouds3.anima.playState == 'finished' then
		clouds3.anima:reverse()
	end


	-- make animation
	anima.updateAll( 250 * dt )



	-- Activated whenever a certain number of "tick" seconds is accumulated.
	if math.fmod( au, tick ) == 0 then
		local colors = sunset.getColors()
		static:lgradient(('240 | %se6 ; 0.0 | %se6 ; 1.0'):format( colors.top, colors.bottom ))
			:update()


			if createdStars and not sunset.isNight() then
				createdStars = false
				for index, shape in ipairs( stars ) do
					shape:fill( 'transparent' )
				end
			end

		-- print(sunset.getPeriod(), sunset.getSunPositionX(800) ,sunset.getSunPositionY(300))
		-- local sunX = sunset.getSunPositionX(800)
		-- local sunY = sunset.getSunPositionY(300)
	end
end

