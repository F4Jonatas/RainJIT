
local M = {}
M.__index = M





local function calcY( value, max, height )
	if max == 0 then return height end

	local y = ( height - PADDING_BOTTOM ) - ( value / max ) * ( height - ( PADDING_TOP + PADDING_BOTTOM ))
	return math.min( height, math.max( 0, y ))
end




function M:addLine( shape )
	if type( shape ) == 'table' then
		-- table.insert( self.line, shape:add()
	end
end



return setmetatable( {}, {
	__call = function( _, opts )
		local meta = {
			lines   = opts.lines or {},
			options = {
				maxPoints     = opts.maxPoints     and opts.maxPoints                     or 0,
				maxDownload   = opts.maxDownload   and opts.maxDownload                   or 0,
				maxUpload     = opts.maxUpload     and opts.maxUpload                     or 0,
				paddingTop    = opts.paddingTop    and opts.paddingTop                    or 0,
				paddingBottom = opts.paddingBottom and opts.paddingBottom                 or 0,
				paddingLeft   = opts.paddingLeft   and opts.paddingLeft                   or 0,
				paddingRight  = opts.paddingRight  and opts.paddingRight                  or 0,
				height        = opts.height        and opts.height                        or 0,
				width         = opts.width         and ( opts.width - opts.paddingRight ) or 0,
				spacing       = opts.spacing       and opts.spacing                       or (( opts.width - opts.paddingLeft ) / opts.maxPoints )
			}
		}


		for index = 1, meta.lines do
			print( index )
			-- table.insert( DOWNLOAD_ARRAY, 0 )
			-- table.insert( UPLOAD_ARRAY  , 0 )
		end


		return setmetatable( meta, M )
	end
})



-- local graph = chart({
-- 	meter         = meter( 'graphic' ),
-- 	maxPoints     = 40,
-- 	paddingTop    = 15,
-- 	paddingBottom = 5,
-- 	paddingLeft   = 55,
-- 	paddingRight  = 15,
-- 	height        = rain:var( 'HEIGHT' ) - rain:var( '#MARGIN_TOP#' ) - 3,
-- 	width         = rain:var( 'WIDTH' )
-- })

-- graph:addLine() -- shape 1 - 2
-- graph:addLine() -- shape 3 - 4



	-- sDown:addPoint( netIn:value())
	-- sUp:addPoint( netout:value())
	-- graph:redraw()
