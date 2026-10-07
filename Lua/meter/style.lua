--- Style manager (`meter.style`).
-- Manipulates the pipe-separated list of the `MeterStyle` option.
-- Every method returns the meter instance, except `contains`.
--
-- @usage
-- meter.style:add( "A", "B" )
-- meter.style:remove( "A" )
-- meter.style:contains( "A" )
-- meter.style:toggle( "A" )
--
-- @submodule meter.style
-- @release 0.1.1
-- @author F4Jonatas
-- @license GPL v2.0 License



local style = {}
style.__index = style


local function split( str )
	local list = {}

	for item in string.gmatch( str or '', '[^|]+' ) do
		item = item:match( '^%s*(.-)%s*$' )

		if item ~= '' then
			list[ #list + 1 ] = item
		end
	end

	return list
end


local function indexOf( list, name )
	for i, v in ipairs( list ) do
		if v == name then
			return i
		end
	end
end


local function read( self )
	return split( rain:option( self.meter.name, 'meterStyle' ))
end


local function write( self, list )
	rain:bang( '!setOption', self.meter.name, 'meterStyle', table.concat( list, '|' ))
	return self.meter
end


--- Add one or more styles (ignores those already present).
function style:add( ... )
	local list  = read( self )
	local array = type( ... ) == 'table' and ... or { ... }

	for _, name in ipairs( array ) do
		if not indexOf( list, name ) then
			list[ #list + 1 ] = name
		end
	end

	return write( self, list )
end


--- Remove one or more styles.
function style:remove( ... )
	local list = read( self )
	local array = type( ... ) == 'table' and ... or { ... }

	for _, name in ipairs({ ... }) do
		local i = indexOf( list, name )

		if i then
			table.remove( list, i )
		end
	end

	return write( self, list )
end


--- Check whether all given styles are present.
-- @return (boolean)
function style:contains( ... )
	local list = read( self )
	local array = type( ... ) == 'table' and ... or { ... }

	for _, name in ipairs({ ... }) do
		if not indexOf( list, name ) then
			return false
		end
	end

	return true
end


--- Toggle one or more styles.
function style:toggle( ... )
	local list = read( self )

	for _, name in ipairs({ ... }) do
		local i = indexOf( list, name )

		if i then
			table.remove( list, i )
		else
			list[ #list + 1 ] = name
		end
	end

	return write( self, list )
end


return style
