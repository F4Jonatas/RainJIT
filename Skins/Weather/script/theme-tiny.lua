-- https://en.cppreference.com/w/c/chrono/strftime
return {
	theme = function( cook )
		SKIN:Bang(

			'[!setoption icon1 x 2]' ..
			'[!setoption icon1 y 1]' ..
			'[!setoption icon1 w 79]' ..
			'[!setoption icon1 h 79]' ..
			'[!setoption day1 hidden 1]' ..
			'[!setoption phrase1 y 100]' ..
			'[!setoption phrase1 fontsize 11]' ..
			'[!setoption phrase1 w 160]' ..
			'[!setoption phrase1 x 100]' ..
			'[!setoption phrase1 y 40]' ..
			'[!setoption phrase1 stringalign left]' ..
			'[!setoption string01 x 100]' ..
			'[!setoption string01 y 10]' ..
			'[!setoption string01 fontsize 15]' ..
			'[!setoption string01 stringalign left]' ..

			'[!setoption icon2 hidden 1]' ..
			'[!setoption icon3 hidden 1]' ..
			'[!setoption icon3 y 0]' ..
			'[!setoption icon4 hidden 1]' ..
			'[!setoption icon4 y 0]' ..
			'[!setoption day2 x 75]' ..
			'[!setoption day3 x 75]' ..
			'[!setoption day3 y 0]' ..
			'[!setoption day4 y 0]' ..
			'[!setoption phrase2 hidden 1]' ..
			'[!setoption phrase3 hidden 1]' ..
			'[!setoption phrase3 y 0]' ..
			'[!setoption phrase4 hidden 1]' ..
			'[!setoption phrase4 y 0]' ..
			'[!setoption temph2 hidden 1]' ..
			'[!setoption temph3 hidden 1]' ..
			'[!setoption temph3 y 0]' ..
			'[!setoption temph4 hidden 1]' ..
			'[!setoption temph4 y 0]' ..
			'[!setoption templ2 hidden 1]' ..
			'[!setoption templ3 hidden 1]' ..
			'[!setoption templ3 y 0]' ..
			'[!setoption templ4 hidden 1]' ..
			'[!setoption templ4 y 0]' ..
			'[!setoption background h 25]'
		)
	end,



	update = function( json, db, cook, round, i18n )
		local temp
		temp = json.current.temp
		if cook:get( 'units' ) == 'metric' then temp = round( temp ) end

		SKIN:Bang(
			'[!setoption string01 text '.. temp ..'\176]' ..
			'[!updatemeter string01]'
		)

		SKIN:Bang(
			'[!setoption icon1 imagename "img/' .. cook:get( 'icons' ) .. '/' .. json.current.weather[1].icon .. '.png"]' ..
			'[!updatemeter  icon1]'
		)


		local phrase = json.current.weather[1].description:gsub( '^%l', string.upper )

		SKIN:Bang(
			'[!setoption phrase1 text "' .. phrase .. '"]' ..
			'[!updatemeter phrase1]'
			)
	end
}