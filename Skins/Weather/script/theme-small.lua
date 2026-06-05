return {
	theme = function( cook )
		if cook:get( 'iconposition' ) == 'Left' then
			SKIN:Bang(
				'[!setoption icon1 x 2]' ..
				'[!setoption icon2 x 2]' ..
				'[!setoption icon3 x 2]' ..
				'[!setoption icon4 x 2]' ..
				'[!setoption day1 x 75]' ..
				'[!setoption day2 x 75]' ..
				'[!setoption day3 x 75]' ..
				'[!setoption day4 x 75]' ..
				'[!setoption day1 stringalign left]' ..
				'[!setoption day2 stringalign left]' ..
				'[!setoption day3 stringalign left]' ..
				'[!setoption day4 stringalign left]' ..
				'[!setoption phrase1 x 75]' ..
				'[!setoption phrase2 x 75]' ..
				'[!setoption phrase3 x 75]' ..
				'[!setoption phrase4 x 75]' ..
				'[!setoption phrase1 stringalign left]' ..
				'[!setoption phrase2 stringalign left]' ..
				'[!setoption phrase3 stringalign left]' ..
				'[!setoption phrase4 stringalign left]' ..
				'[!setoption string01 x 70]' ..
				'[!setoption temph2 x 66]' ..
				'[!setoption temph3 x 66]' ..
				'[!setoption temph4 x 66]' ..
				'[!setoption templ2 x 66]' ..
				'[!setoption templ3 x 66]' ..
				'[!setoption templ4 x 66]'
			)
		end
	end,



	update = function( json, db, cook, round, i18n, self )
		local count = 0

		if cook:get( 'site' ) == 'OpenWeather' then
			for index, value in pairs( json.daily ) do
				count = count + 1

				if index == 1 then
					local temp
					if db.firsttemp == 'Feels Like' then temp = json.current.feels_like end
					if cook:get( 'units' ) == 'metric' then temp = round( temp ) end

					SKIN:Bang(
						'[!setoption string01 text ' .. temp .. '\176]' ..
						'[!updatemeter string01]'
					)

					SKIN:Bang(
						'[!setoption icon1 imagename "img/' .. cook:get( 'icons' ) .. '/' .. json.current.weather[1].icon .. '.png"]' ..
						'[!updatemeter  icon1]'
					)

					local day = os.date( '%A', value.dt )
					SKIN:Bang(
						'[!setoption day1 text ' .. i18n( day ) .. ']' ..
						'[!updatemeter day1]'
					)

					local phrase = json.current.weather[1].description:gsub( '^%l', string.upper )
					SKIN:Bang(
						'[!setoption phrase1 text "' .. phrase .. '"]' ..
						'[!updatemeter phrase1]'
					)


				-- Continues the following days
				else
					SKIN:Bang(
						'[!setoption icon' .. count .. ' imagename "img/' .. cook:get( 'icons' ) .. '/' .. value.weather[1].icon .. '.png"]' ..
						'[!updatemeter  icon' .. count .. ']'
					)

					if cook:get( 'units' ) == 'metric' then value.temp.max = round( value.temp.max ) end
					SKIN:Bang(
						'[!setoption temph' .. count .. ' text ' .. value.temp.max .. '\176]' ..
						'[!updatemeter temph' .. count .. ']'
					)

					if cook:get( 'units' ) == 'metric' then value.temp.min = round( value.temp.min ) end
					SKIN:Bang(
						'[!setoption templ' .. count .. ' text ' .. value.temp.min .. '\176]' ..
						'[!updatemeter templ' .. count .. ']'
					)

					local day = os.date( '%A', value.dt )
					SKIN:Bang(
						'[!setoption day' .. count .. ' text ' .. i18n( day ) .. ']' ..
						'[!updatemeter day' .. count .. ']'
					)

					local phrase = value.weather[1].description:gsub( '^%l', string.upper )
					SKIN:Bang(
						'[!setoption phrase' .. count .. ' text "' .. phrase .. '"]' ..
						'[!updatemeter phrase' .. count .. ']'
					)
				end

				if count == db.maxdays then break end
			end


		elseif cook:get( 'site' ) == 'Foreca.com' then
			print( self )
		end
	end
}