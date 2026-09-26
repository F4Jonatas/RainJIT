






function Initialize()

	tAuthors = {}
	tDates = {}
	tLinks = {}
	tTopics = {}
	tTitles = {}
	tWatch = {}

	iNumberToDisplay = SKIN:GetVariable('NumberToDisplay')
	iTooltipOption = tonumber(SKIN:GetVariable('TooltipOption'))
	iDoOddEven = tonumber(SKIN:GetVariable('DoOddEven'))
	sEvenColor = SKIN:GetVariable('EvenColor')
	sOddColor = SKIN:GetVariable('OddColor')
	sDeadColor = SKIN:GetVariable('DeadColor')
	iNewMinutes = tonumber(SKIN:GetVariable('NewMinutes'))
	sNewColor = SKIN:GetVariable('NewColor')
	iDoMyName = tonumber(SKIN:GetVariable('DoMyName'))
	sMyName = SKIN:GetVariable('MyName')
	sMyColor = SKIN:GetVariable('MyColor')

	sForumsToWatch = SKIN:GetVariable('ForumsToWatch')
	if not sForumsToWatch then
		sForumsToWatch = ''
	else
		for sWatch in string.gmatch(sForumsToWatch, '[^|]+') do table.insert(tWatch, sWatch) end
		sWatchColor = SKIN:GetVariable('WatchColor')
	end

	msMeasureRainmeterRSS = SKIN:GetMeasure('MeasureRainmeterRSS')

	sMainTitlePat = '<title>(.-)</title>'
	sMainLinkPat = '.-<link href=\"(.-)\".-<entry>'
	sAuthorPat = '.-<author><name>.-CDATA%[(.-)%]%]></name></author>'
	sDatePat = '.-<updated>(.-)</updated>'
	sLinkPat = '.-<link href=\"(.-)\"'
	sTitlePat = '.-<title type=\"html\">.-CDATA%[(.-)%]%]></title>'

end

function Update()

	sAllText = msMeasureRainmeterRSS:GetStringValue()

	sMainTitle,	sMainLink, tAuthors[1], tDates[1], tLinks[1], tTitles[1], tAuthors[2], tDates[2], tLinks[2], tTitles[2], tAuthors[3], tDates[3], tLinks[3], tTitles[3], tAuthors[4], tDates[4], tLinks[4], tTitles[4], tAuthors[5], tDates[5], tLinks[5], tTitles[5], tAuthors[6], tDates[6], tLinks[6], tTitles[6], tAuthors[7], tDates[7], tLinks[7], tTitles[7], tAuthors[8], tDates[8], tLinks[8], tTitles[8], tAuthors[9], tDates[9], tLinks[9], tTitles[9], tAuthors[10], tDates[10], tLinks[10], tTitles[10] = string.match(sAllText, sMainTitlePat..sMainLinkPat..sAuthorPat..sDatePat..sLinkPat..sTitlePat..sAuthorPat..sDatePat..sLinkPat..sTitlePat..sAuthorPat..sDatePat..sLinkPat..sTitlePat..sAuthorPat..sDatePat..sLinkPat..sTitlePat..sAuthorPat..sDatePat..sLinkPat..sTitlePat..sAuthorPat..sDatePat..sLinkPat..sTitlePat..sAuthorPat..sDatePat..sLinkPat..sTitlePat..sAuthorPat..sDatePat..sLinkPat..sTitlePat..sAuthorPat..sDatePat..sLinkPat..sTitlePat..sAuthorPat..sDatePat..sLinkPat..sTitlePat)

	for i = 1, iNumberToDisplay do

		tTitles[i] = string.gsub(tTitles[i], '&amp;', '&')
		tTitles[i] = string.gsub(tTitles[i], '&quot;', '\"')
		tTitles[i] = string.gsub(tTitles[i], '&apos;', '\'')
		tTitles[i] = string.gsub(tTitles[i], '&lt;', '<')
		tTitles[i] = string.gsub(tTitles[i], '&gt;', '>')
		tTitles[i] = string.gsub(tTitles[i], '\"', '\'')
		tTitles[i] = string.gsub(tTitles[i], '#(.-)#', '#**%1**#')
		tTitles[i] = string.gsub(tTitles[i], '%%(.-)%%' , '%%%1')

		-- These settings are used to "shorten" the titles of topics. Use these in any "ForumsToWatch" option in Settings.inc.
		tTitles[i] = string.gsub(tTitles[i], '^Help: Rainmeter Skins', 'Help: Skins')
		tTitles[i] = string.gsub(tTitles[i], '^Rainmeter News', 'News')
		tTitles[i] = string.gsub(tTitles[i], '^Change Announcements', 'Changes')
		tTitles[i] = string.gsub(tTitles[i], '^Site Discussion', 'Site')
		tTitles[i] = string.gsub(tTitles[i], '^Bugs & Feature Suggestions', 'Bugs & Suggestions')
		tTitles[i] = string.gsub(tTitles[i], '^Help: Rainmeter Application', 'Help: Application')
		tTitles[i] = string.gsub(tTitles[i], '^General Discussion', 'General')
		tTitles[i] = string.gsub(tTitles[i], '^Share Your Creations', 'Share')
		tTitles[i] = string.gsub(tTitles[i], '^Members\' Lounge', 'Lounge')
		tTitles[i] = string.gsub(tTitles[i], '^Other Software & Customization', 'Other')
		tTitles[i] = string.gsub(tTitles[i], '^Developer\'s Forum', 'Developers')

		tLinks[i] = string.gsub(tLinks[i], '&amp;', '&')

		if iDoOddEven == 1 then
			if i % 2 == 0 then --Number is even
				SKIN:Bang('!SetOption', 'MeterTitle'..i, 'FontColor', sEvenColor)
			else
				SKIN:Bang('!SetOption', 'MeterTitle'..i, 'FontColor', sOddColor)
			end
		end

		local timeNow = os.time(os.date('!*t'))
		local timeItem = TimeStamp(tDates[i])
		local timeDiff = os.difftime(timeNow, timeItem)
		local timeDifferenceFormat = ConvertSeconds(timeDiff)

		if timeDifferenceFormat['mins'] == 1 then minText = "minute" else minText="minutes" end
		if timeDifferenceFormat['hours'] == 1 then	hourText = "hour" else hourText="hours" end
		if timeDifferenceFormat['days'] == 1 then dayText = "day" else dayText="days" end

		if timeDiff <= 86400 then
			if timeDiff < 3600 then
				ageDisplay = timeDifferenceFormat['mins']..' '..minText..' ago'
			else
				ageDisplay = timeDifferenceFormat['hours']..' '..hourText..' '..timeDifferenceFormat['mins']..' '..minText..' ago'
			end
		else
			ageDisplay = timeDifferenceFormat['days']..' '..dayText..' '..timeDifferenceFormat['hours']..' '..hourText..' '..timeDifferenceFormat['mins']..' '..minText..' ago'
		end

		if timeDiff / 60 <= iNewMinutes then
			SKIN:Bang('!SetOption', 'MeterNew'..i, 'SolidColor', sNewColor)
		else
			SKIN:Bang('!SetOption', 'MeterNew'..i, 'SolidColor', sDeadColor)
		end

		SKIN:Bang('!SetOption', 'MeterWatch'..i, 'SolidColor', sDeadColor)
		for _, sWatch in ipairs(tWatch) do
			if string.match(string.lower(tTitles[i]), string.lower(sWatch)) then
				SKIN:Bang('!SetOption', 'MeterWatch'..i, 'SolidColor', sWatchColor)
			end
		end

		SKIN:Bang('!SetOption', 'MeterMy'..i, 'SolidColor', sDeadColor)
		if iDoMyName == 1 then
			if string.upper(tAuthors[i]) == string.upper(sMyName) then
				SKIN:Bang('!SetOption', 'MeterMy'..i, 'SolidColor', sMyColor)
			end
		end

		SKIN:Bang('!SetOption', 'MeterTitle'..i, 'Text', tTitles[i])
		SKIN:Bang('!SetOption', 'MeterTitle'..i, 'LeftMouseUpAction', '['..tLinks[i]..']')

		if iTooltipOption == 3 then
			SKIN:Bang('!SetOption', 'MeterTitle'..i, 'ToolTipTitle', tAuthors[i])
			SKIN:Bang('!SetOption', 'MeterTitle'..i, 'ToolTipText', tTitles[i]..'#CRLF#'..ageDisplay)
		elseif iTooltipOption == 2 then
			SKIN:Bang('!SetOption', 'MeterTitle'..i, 'ToolTipTitle', tAuthors[i])
			SKIN:Bang('!SetOption', 'MeterTitle'..i, 'ToolTipText', ageDisplay)
		elseif iTooltipOption == 1 then
			SKIN:Bang('!SetOption', 'MeterTitle'..i, 'ToolTipTitle', tAuthors[i])
			SKIN:Bang('!SetOption', 'MeterTitle'..i, 'ToolTipText', tTitles[i])
		else
			SKIN:Bang('!SetOption', 'MeterTitle'..i, 'ToolTipText', '')
		end

	end

	SKIN:Bang('!UpdateMeter', '*')
	SKIN:Bang('!Redraw')

	SELF:Disable()
	return(os.date())

end

function TimeStamp(itemDate)

   -- feed date format: 2015-03-14T15:06:40-05:00

   local year, month, day, hour, min, sec =
   string.match(itemDate, "(%d%d%d%d)-(%d%d)-(%d%d)T(%d%d):(%d%d):(%d%d)")

   local zhours, zminutes = string.match(itemDate, "%d+-%d+-%d+T%d+:%d+:%d+(.-):(%d%d)")

   if not zhours then zhours = 0 end
   if not zminutes then zminutes = 0 end

   local localTime = os.time({year=year, month=month, day=day, hour=hour, min=min, sec=sec, isdst=false})
   local utcTime = localTime - ((tonumber(zhours)*3600) + (tonumber(zminutes)*60))

   return utcTime

end

function ConvertSeconds(secondsDiff)

	local daysDiff = (secondsDiff / 86400)
	local daysRemainder = (secondsDiff % 86400)
	local hoursDiff = (daysRemainder / 3600)
	local hoursRemainder = ((secondsDiff - 86400) % 3600)
	local minsDiff = (hoursRemainder / 60)

	local convertedSeconds = {days = math.floor(daysDiff); hours = math.floor(hoursDiff); mins = math.floor(minsDiff)}

	return convertedSeconds

end
