

require( 'math.utils' )

local glass = require( 'glass' )
local meter = require( 'meter' )
local depot = require( 'depot' )
local fetch = require( 'fetch' )
local i18n  = require( 'i18n' )
local json  = require( 'json' ).decode

-- Get and set current system language
i18n.language()

local dp        = depot()
local latitude  = dp:get( 'latitude', -22.8039 )
local longitude = dp:get( 'longitude', -43.3722 )
local reload    = 500

-- Forward declarations
local gatherResults


local baseURL = 'https://api.open-meteo.com/v1/forecast?' ..
	'latitude='.. latitude ..
	'&longitude='.. longitude ..
	'&current=apparent_temperature,is_day,weather_code' ..
	'&daily=temperature_2m_max,temperature_2m_min,weather_code' ..
	'&timezone='.. dp:get( 'timezone', 'auto' )


-- Apply a acrylic effect
if dp:get( 'glass', true ) then
	glass( rain.hwnd, { effect = 'acrylic', corners = 'round' })
end



function rain:init()
	upgrade( gatherResults())
end


function rain:update( cs, dt )
	if math.fmod( math.round( cs ), reload ) == 0 then
		upgrade( gatherResults())
	end
end


upgrade = function( data )
	meter( 'curTemp' ):text( math.round( data[1].feelsLike ) ..'°' ):update()
	meter( 'curText' ):text( i18n( 'Feels like' )):update()

	for index, item in ipairs( data ) do
		meter( 'tempH'.. index ):text( math.round( item.max ) ..'°' ):update()
		meter( 'tempL'.. index ):text( math.round( item.min ) ..'°' ):update()
		meter( 'week-day'.. index ):text( i18n( item.weekDayName )):update()
		meter( 'phrase'.. index ):text( item.text ):update()
		meter( 'icon'.. index ):image( item.icon ):update()
	end
end



function mapWeather( code, isDay, dt )
	local dict = {
		[0]  = { text = 'Clear', phrase = 'Clear sky', icon = isDay and 'clear-day' or 'clear-night' },
		[1]  = { text = 'Mainly clear', phrase = 'Mainly clear', icon = isDay and 'partly-cloudy-day' or 'partly-cloudy-night' },
		[2]  = { text = 'Partly cloudy', phrase = 'Partly cloudy', icon = isDay and 'partly-cloudy-day' or 'partly-cloudy-night' },
		[3]  = { text = 'Overcast', phrase = 'Overcast', icon = 'cloudy' },
		[45] = { text = 'Fog', phrase = 'Fog', icon = isDay and 'fog-day' or 'fog-night' },
		[48] = { text = 'Rime fog', phrase = 'Depositing rime fog', icon = isDay and 'fog-day' or 'fog-night' },
		[51] = { text = 'Drizzle', phrase = 'Light drizzle', icon = isDay and 'rain-day' or 'rain-night' },
		[53] = { text = 'Drizzle', phrase = 'Moderate drizzle', icon = isDay and 'rain-day' or 'rain-night' },
		[55] = { text = 'Drizzle', phrase = 'Dense drizzle', icon = isDay and 'rain-day' or 'rain-night' },
		[56] = { text = 'Freezing drizzle', phrase = 'Light freezing drizzle', icon = isDay and 'sleet-day' or 'sleet-night' },
		[57] = { text = 'Freezing drizzle', phrase = 'Dense freezing drizzle', icon = isDay and 'sleet-day' or 'sleet-night' },
		[61] = { text = 'Rain', phrase = 'Slight rain', icon = isDay and 'rain-day' or 'rain-night' },
		[63] = { text = 'Rain', phrase = 'Moderate rain', icon = isDay and 'rain-day' or 'rain-night' },
		[65] = { text = 'Heavy rain', phrase = 'Heavy rain', icon = isDay and 'rain-day' or 'rain-night' },
		[66] = { text = 'Freezing rain', phrase = 'Light freezing rain', icon = isDay and 'sleet-day' or 'sleet-night' },
		[67] = { text = 'Freezing rain', phrase = 'Heavy freezing rain', icon = isDay and 'sleet-day' or 'sleet-night' },
		[71] = { text = 'Snow', phrase = 'Light snowfall', icon = isDay and 'snow-day' or 'snow-night' },
		[73] = { text = 'Snow', phrase = 'Moderate snowfall', icon = isDay and 'snow-day' or 'snow-night' },
		[75] = { text = 'Heavy snow', phrase = 'Heavy snowfall', icon = isDay and 'snow-day' or 'snow-night' },
		[77] = { text = 'Snow grains', phrase = 'Snow grains', icon = isDay and 'snow-day' or 'snow-night' },
		[80] = { text = 'Rain showers', phrase = 'Slight rain showers', icon = isDay and 'rain-day' or 'rain-night' },
		[81] = { text = 'Rain showers', phrase = 'Moderate rain showers', icon = isDay and 'rain-day' or 'rain-night' },
		[82] = { text = 'Heavy showers', phrase = 'Violent rain showers', icon = isDay and 'rain-day' or 'rain-night' },
		[85] = { text = 'Snow showers', phrase = 'Light snow showers', icon = isDay and 'snow-day' or 'snow-night'},
		[86] = { text = 'Snow showers', phrase = 'Heavy snow showers', icon = isDay and 'snow-day' or 'snow-night' },
		[95] = { text = 'Thunderstorm', phrase = 'Thunderstorm', icon = isDay and 'storm-day' or 'storm-night' },
		[96] = { text = 'Thunderstorm', phrase = 'Thunderstorm with hail', icon = isDay and 'storm-day' or 'storm-night' },
		[99] = { text = 'Severe thunderstorm', phrase = 'Thunderstorm with heavy hail', icon = isDay and 'storm-day' or 'storm-night' }
	}


	if dict[ code ] then
		local _,_, year, month, day = dt:find( '(%d+)-(%d+)-(%d+)' )
		dict[ code ].date = os.time({ year = year, month = month, day = day })
		return dict[ code ]

	else
		return {
			text   = 'Unknown',
			phrase = 'Unknown weather condition (code '.. tostring( code ) ..')',
			icon   = 'unknown'
		}
	end
end




function gatherResults()
	local result = {}
	local current = fetch( baseURL )

	if not current.ok then
		error( 'Failed to fetch current weather data.\nError: ' .. current.error )

	else
		local data = json( current.text )
		local dict = mapWeather(
			data.current.weather_code,
			true,
			data.daily.time[1]
		)

		table.insert( result, {
			feelsLike   = data.current.apparent_temperature,
			max         = data.daily.temperature_2m_max[1],
			min         = data.daily.temperature_2m_min[1],
			icon        = dict.icon,
			timestamp   = data.current.time,
			weekDayName = 'Today',
			text        = dict.text,
			phrase      = dict.phrase:gsub( '^%l', string.upper )
		})


		for index = 2, 4 do
			local dict = mapWeather(
				data.daily.weather_code[ index ],
				index == 2 and data.current.is_day or true,
				data.daily.time[ index ]
			)

			table.insert( result, {
				max         = data.daily.temperature_2m_max[ index ],
				min         = data.daily.temperature_2m_min[ index ],
				icon        = dict.icon,
				weekDayName = os.date( '%A', dict.date ),
				text        = dict.text,
				phrase      = dict.phrase:gsub( '^%l', string.upper )
			})
		end
	end

	return result
end
