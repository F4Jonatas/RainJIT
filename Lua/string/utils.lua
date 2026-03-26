-- String format
-- https://stackoverflow.com/a/4673436

-- _Encoding_866To1251
-- https://forum.rainmeter.net/viewtopic.php?f=118&t=16540


-- https://stackoverflow.com/a/62901633







-- Split
-- https://stackoverflow.com/questions/1426954/split-string-in-lua
string.split = function( text, sep )
	sep = sep or '%s'
	local list = {}

	for str in string.gmatch( text, '([^'.. sep ..']+)' ) do
		table.insert( list, str )
	end

	return list
end



--- Converts a string to Title Case.
--
-- Transforms the first character of each word to uppercase and
-- converts the remaining characters of each word to lowercase.
-- Words are detected using Lua pattern matching and consist of
-- alphabetic characters followed by alphanumeric, underscore,
-- or apostrophe characters.
--
-- @function titlecase
-- @param (string) str The input string to be converted.
-- @return (string) A new string formatted in Title Case.
--
-- @usage
-- local result = string.titlecase("example of title in lua")
-- -- result: "Example Of Title In Lua"
--
-- @see string.gsub
string.titleCase = function( text )
	return ( text:gsub( "([%aÀ-ÖØ-öø-ÿ])([%wÀ-ÖØ-öø-ÿ']*)", function( first, rest )
		return first:upper() .. rest:lower()
	end))
end