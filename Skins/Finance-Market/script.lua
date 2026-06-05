

local depot   = require( 'depot' )
local meter   = require( 'meter' )
local fetch   = require( 'fetch' )
local glass   = require( 'glass' )


local dp         = depot()
local symbol     = dp:get( 'symbol', 'VALE3.SA' )
local symbolList = dp:get( 'symbol-list' )
local period1    = dp:get( 'period1', 1672531200 )
local period2    = dp:get( 'period2', 1704067200 )
local interval   = dp:get( 'interval', '1d' )


local HEIGHT = rain:var( 'HEIGHT' )
local WIDTH  = rain:var( 'WIDTH'  )
local CRUMB


local chartURL = 'https://query1.finance.yahoo.com/v8/finance/chart/%s?period1=%s&period2=%s&interval=%s'



-- Forward declarations
local upgrade



if dp:get( 'glass', true ) then
	glass( rain.hwnd, { effect = 'acrylic', corners = 'round' })
end



local function getCrumb()
	local resp1 = fetch.async( 'https://yahoo.com/' )
	local cook1 = resp1:headers()['set-cookie']

	if not cook1 then
		error( 'Cookie não encontrado no cabeçalho da resposta 1.' )
	end


	local resp2 = fetch( 'https://fc.yahoo.com/', { headers = { Cookie = 'B=' .. cook1 }})
	local cook2 = resp2:headers()['set-cookie']

	if not cook2 then
		error( 'Cookie não encontrado no cabeçalho da resposta 2.' )
	end


	local crumb = fetch( 'https://query2.finance.yahoo.com/v1/test/getcrumb', {
		headers = {
			['Cookie'] = cook2,
			['User-Agent'] = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36'
		}
	})

	if not crumb.ok then
		error( 'Falha ao obter crumb. Status: '.. crumb.status )
	end

	return crumb:text()
end




function upgrade()
	local response = fetch( chartURL:format(
		symbol,
		period1,
		period2,
		interval
	))

	local data = response:json()
	local title = meter( 'symbolName' )
		:text( data.chart.result[1].meta.longName )
		:update()
end

upgrade()
-- local response = fetch( 'https://query1.finance.yahoo.com/v8/finance/chart/'.. symbol )


