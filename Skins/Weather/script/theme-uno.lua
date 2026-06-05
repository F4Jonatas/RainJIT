-- https://en.cppreference.com/w/c/chrono/strftime
return {
	theme = function( cook )
		SKIN:Bang(
			'[!setoption icon1 x 2]' ..
			'[!setoption icon1 y 20]' ..
			'[!setoption icon1 w 79]' ..
			'[!setoption icon1 h 79]' ..
			'[!setoption icon2 hidden 1]' ..
			'[!setoption icon3 hidden 1]' ..
			'[!setoption icon4 hidden 1]' ..
			'[!setoption day1 x 180]' ..
			'[!setoption day1 fontsize 11]' ..
			'[!setoption day2 x 75]' ..
			'[!setoption day3 x 75]' ..
			'[!setoption day4 x 75]' ..
			'[!setoption phrase1 y 100]' ..
			'[!setoption phrase1 fontsize 11]' ..
			'[!setoption phrase1 x 180]' ..
			'[!setoption phrase2 hidden 1]' ..
			'[!setoption phrase3 hidden 1]' ..
			'[!setoption phrase4 hidden 1]' ..
			'[!setoption string01 x 200]' ..
			'[!setoption string01 y 20]' ..
			'[!setoption string01 fontsize 50]' ..
			'[!setoption temph2 hidden 1]' ..
			'[!setoption temph3 hidden 1]' ..
			'[!setoption temph4 hidden 1]' ..
			'[!setoption templ2 hidden 1]' ..
			'[!setoption templ3 hidden 1]' ..
			'[!setoption templ4 hidden 1]' ..
			'[!setoption background h 125]'
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


		local day = i18n( os.date( '%a', json.current.dt )) ..
			os.date( ' %d ', json.current.dt ) ..
			i18n( os.date( '%b', json.current.dt )) ..
			os.date( ' %I:%M %p', json.current.dt )

		SKIN:Bang(
			'[!setoption day1 text "'.. day ..'"]' ..
			'[!updatemeter day1]'
		)


		local phrase = round( json.daily[1].temp.max ) .. '\176/'
			.. round( json.daily[1].temp.min ) .. '\176 '.. i18n( 'Feels Like' ) ..' '
			.. round( json.current.feels_like ) .. '\176'

		SKIN:Bang(
			'[!setoption phrase1 text "' .. phrase .. '"]' ..
			'[!setoption phrase1 w 160]' ..
			'[!updatemeter phrase1]'
			)
	end
}