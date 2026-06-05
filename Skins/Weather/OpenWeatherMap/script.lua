-- cSpell:ignoreRegExp ROOTCONFIGPATH|apikey


require( 'math.utils' )

local glass = require( 'glass' )
local meter = require( 'meter' )
local depot = require( 'depot' )
local fetch = require( 'fetch' )
local i18n  = require( 'i18n' )
local json  = require( 'json' ).decode

-- load and set current system language
i18n.language()


local dp        = depot()
local latitude  = dp:get( 'latitude' )
local longitude = dp:get( 'longitude' )


-- Forward declarations
local gatherResults



-- If this is enabled for Windows, it will be more accurate.
-- Otherwise, it will use an online API with less precision.
local geolocation = true -- dp:get( 'geolocation' )


local baseURL = 'https://api.openweathermap.org/data/2.5'
local query =
	'&exclude='.. dp:get( 'exclude', 'minutely,hourly' ) ..
	'&units='  .. dp:get( 'units'  , '' ) ..
	'&lang='   .. dp:get( 'locale' , 'en' ) ..
	'&lat='    .. latitude ..
	'&lon='    .. longitude ..
	'&appid='  .. dp:get( 'apikey' , '' )



-- Apply a blur effect
if dp:get( 'glass' ) then
	glass( rain.hwnd, { effect = 'acrylic', corners = 'round' })
end



-- city name
-- state code
-- country code

if geolocation then
	-- local owmgeo = fetch( 'https://nominatim.openstreetmap.org/search?' ..
	-- 	'&q=São João de Meriti' ..
	-- 	'&format=jsonv2'
	-- )
	-- print( owmgeo.ok, owmgeo.text )
	-- return


	-- local ip = fetch( 'https://api.ipify.org' )
	-- if not ip.ok then
	-- 	return print( 'Não consegui encontrar seu IP externo.' )
	-- end

	-- local geo = fetch( 'https://ipinfo.io/widget/demo/'.. ip.text )
	-- if not geo.ok then
	-- 	return print( 'Não consegui sua Geo Localização aproximada.' )
	-- end

	-- local geo = json( geo.text ).data
	-- geo = string.split( geo.loc, ',' )
	-- latitude = geo[1]
	-- longitude = geo[2]

	-- dp:set( 'latitude' , latitude )
	-- dp:set( 'longitude', longitude )
end




function rain:init()
	local imgPath = rain:var( '#ROOTCONFIGPATH#Weather\\img\\'.. dp:get( 'icons', 'OpenWeather' ) ..'\\' )

	for index = 1, 10 do
		local icon = meter( 'icon1' )
		if not icon then break end

		icon:path( imgPath )
	end

	upgrade()
end




upgrade = function()
	data = gatherResults()
	local maxDay = 4

	for index, item in ipairs( data ) do
		if index == 1 then -- today
			meter( 'string01' ):text( math.round( item.feelsLike ) .. '°' ):update()
			meter( 'icon1' ):image( item.icon ):update()
			meter( 'week-day1' ):text( i18n( item.weekDayName ..'-' )):update()
			meter( 'phrase1' ):text( item.phrase:gsub( '^%l', string.upper )):update()

		else
			meter( 'icon'.. index ):image( item.icon ):update()
			meter( 'tempH'.. index ):text( math.round( item.max ) ..'°' ):update()
			meter( 'tempL'.. index ):text( math.round( item.min ) ..'°' ):update()
			meter( 'week-day'.. index ):text( i18n( item.weekDayName ..'-' )):update()
			meter( 'phrase'.. index ):text( item.phrase:gsub( '^%l', string.upper )):update()

			if index == maxDay then break end
		end

	end
end



function gatherResults()
	local result = {}

	-- https://openweathermap.org/current
	local current = fetch( baseURL ..'/weather?'.. query )
	if not current.ok then
		error( 'Failed to fetch current weather data.' )

	else
		local db = json( current.text )
		 table.insert( result, {
			feelsLike   = db.main.feels_like,
			icon        = db.weather[1].icon,
			timestamp   = db.dt,
			weekDayName = os.date( '%A', db.dt ),
			phrase      = db.weather[1].description:gsub( '^%l', string.upper )
		})
	end


	-- https://openweathermap.org/forecast5
	local days = fetch( baseURL ..'/forecast?'.. query )
	if not days.ok then
		error( 'Failed to fetch days weather data.' )

	else
		local db = json( days.text ).list
		local today = os.date( '%d' )
		local curdDay = today -- ignore current day

		for index, item in ipairs( db ) do
			local day = os.date( '%d', item.dt )
			if day ~= curdDay then
				curdDay = day
				table.insert( result, {
					icon = item.weather[1].icon,
					phrase = item.weather[1].description:gsub( '^%l', string.upper ),
					weekDayName = os.date( '%A', item.dt )
				})
			end

			if day ~= today and day == curdDay then
				local dict = result[ #result ]
				local time = tonumber( os.date( '%H', item.dt ))

				dict.icon = time <= 15 and item.weather[1].icon or dict.icon
				dict.timestamp = dict.timestamp or item.dt
				dict.max = math.max( item.main.temp_max, dict.max or 0 )
				dict.min = math.min( item.main.temp_min, dict.min or 99999 )
			end
		end

	end

	return result
end
