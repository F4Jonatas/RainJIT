[variables]
	;GREYSCALE = 0
	;ICONPOSITION = Left

	; Sync Color With Wallpaper
	;SYNCCOLOR = On

	; Themes: Small / Tiny
	ICONS = OpenWeather

	;FIRSTTEMP = Feels Like
	; For Celcius change the Type to M and the Postfix to C
	; For Fahrenheit change the Type to F and the Postfix to F
	;TYPE = m
	; For location, go to
	; Weather.com and search for location then copy the code in the URL.
	; Location = USMO0895
	;LOCATION = BRXX0161:1:BR
	;Foreca.com
	;SITE = Weather.com
	; pt_BR en_US
	;LOCALE = pt_BR
	;POSTFIX = C
	;WxDataRegExp = (?siU)<tmp>(\d+)<\/tmp>\s*<flik>\d+<\/flik>\s*<t>([\x{00E0}\x{00E3}\w\s\-\.\(\)\/]+)<\/t>\s*<icon>(\d+)<\/icon>[^\x{0000}]+<day d="0" t="\w+" dt="[\w\d ]+">\s*<hi>(\d+)<\/hi>\s*<low>(\d+)<\/low>[^\x{0000}]+<day d="1" t="([\w\/]+)" dt="[\w\d ]+">\s*<hi>(\d+)<\/hi>\s*<low>(\d+)<\/low>\s*<sunr>[\w\:\s]+<\/sunr>\s*<suns>[\w\:\s]+<\/suns>\s*<part p="d">\s*<icon>(\d+)<\/icon>\s*<t>([\x{00E0}\x{00E3}\w\s\-\.\(\)\/]+)<\/t>[^\x{0000}]+<day d="2" t="([\w\/]+)" dt="[\w\d ]+">\s*<hi>(\d+)<\/hi>\s*<low>(\d+)<\/low>\s*<sunr>[\w\:\s]+<\/sunr>\s*<suns>[\w\:\s]+<\/suns>\s*<part p="d">\s*<icon>(\d+)<\/icon>\s*<t>([\x{00E0}\x{00E3}\w\s\-\.\(\)\/]+)<\/t>[^\x{0000}]+<day d="3" t="([\w\/]+)" dt="[\w\d ]+">\s*<hi>(\d+)<\/hi>\s*<low>(\d+)<\/low>\s*<sunr>[\w\:\s]+<\/sunr>\s*<suns>[\w\:\s]+<\/suns>\s*<part p="d">\s*<icon>(\d+)<\/icon>\s*<t>([\x{00E0}\x{00E3}\w\s\-\.\(\)\/]+)<\/t>

[style1]
	FontColor = ffffff
	StringEffect = Shadow
	FontEffectColor = 0,0,0,120
	StringStyle = Normal
	StringAlign = Right
	FontFace = Calibri
	Antialias = 1

[ClipString]
	ClipString = 1

[Bold]
	StringStyle = Bold

[FontSize-9]
	FontSize = 9

[FontSize-12]
	FontSize = 12
