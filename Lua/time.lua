-- https://github.com/stepelu/lua-time


local M = {}
M.__index = M


M.toDate = function( content )
	local f, l, year, month, day, h, m, s, ms = content:find(
		'(%d+)-(%d+)-(%d+)T?(%d*):?(%d*):?(%d*).?(%d*)'
	)

	if year == nil or ms == nil or l ~= #content then
		error(
			'"'.. content .. '" is not a string representation of a date\n' ..
			'Expected string: 2001-12-30T11:10:00.000000'
		)
	end

	return os.time({ year = year, month = month, day = day })
end


return M