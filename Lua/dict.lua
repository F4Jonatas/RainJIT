--
-- @module dict
-- @author F4Jonatas
-- @version 1.3
-- A collection of utility functions for dictionary-like tables
-- (tables with string keys and arbitrary values).

-- https://stackoverflow.com/questions/6075262/lua-table-tostringtablename-and-table-fromstringstringtable-functions


local dict = {}



--- Returns the index (key) of a given value in a table.
-- Works like JavaScript's indexOf for arrays, but can also find values
-- in dictionary tables (returns the first key found).
-- @param that table The table to search.
-- @param item any The value to locate.
-- @return key|nil The first key associated with `item`, or nil if not found.
--
-- like a javascript indexOf:
--   https://developer.mozilla.org/pt-BR/docs/Web/JavaScript/Reference/Global_Objects/Array/indexOf
--
function dict.indexof(that, item)
	for key, value in pairs(that) do
		if value == item then
			return key
		end
	end

	return nil
end



--- Returns a random element from an array-like table.
-- @param that table An array (list) of values.
-- @return any A random value from the table, or nil if the table is empty.
-- ex: table.random({ 19, 28, 37, 46, 50, 64, 73, 82, 91 })
--
function dict.random(that)
		local len = #that
		if len == 0 then return nil end
		return that[math.random(1, len)]
end



--- Returns a random key from the table.
-- @param that table The table.
-- @return any A random key from the table, or nil if the table is empty.
function dict.randomkey(that)
		local keys = dict.keys(that)
		return dict.random(keys)
end



--
--- Returns a list of all keys in the table.
-- @param that table The table.
-- @return table A list containing all keys (order not guaranteed).
function dict.keys(that)
		local result = {}
		for key, _ in pairs(that) do
				table.insert(result, key)
		end
		return result
end



--
-- table.isarray( {} )        -- nil
-- table.isarray( { 4 } )     -- true
-- table.isarray( { a = 4 } ) -- false
-- table.isarray( { 4 ; n = 1 } ) -- false
--
--- Checks whether a table is a pure array (sequence).
-- A pure array has only consecutive numeric keys starting at 1,
-- and no other keys.
-- @param that table The table to test.
-- @return boolean|nil `true` if it is an array, `false` if it is a dictionary
-- or has extra keys, `nil` if `that` is not a table.
function dict.isarray(that)
	if type(that) ~= "table" then return nil end

	local len = #that
	local key_count = 0
	for _ in pairs(that) do key_count = key_count + 1 end

	-- If length equals total key count, and there are no gaps,
	-- it's a pure array.  But # may be wrong if there are gaps.
	-- We can check all keys are numeric and <= len.
	if len == key_count then
		for k, _ in pairs(that) do
			if type(k) ~= "number" or k < 1 or k > len or math.floor(k) ~= k then
				return false
			end
		end
		return true
	else

		-- If key_count > len, there are extra keys or gaps → not an array.
		return false
	end
end



--- Sorts the dictionary by its values and returns an ordered list of key‑value pairs.
-- @param that table The dictionary table (keys can be any type, but typically strings).
-- @param asc boolean (optional) `true` for ascending order (default),
-- `false` for descending order.
-- @return table A list of tables, each with fields `key` and `value`,
-- ordered by the values according to `asc`.
function dict.sortvalue( that, asc )
	if type( that ) ~= 'table' then return {} end
	if asc == nil then asc = true end

	local pairs_list = {}
	for key, value in pairs( that ) do
		table.insert( pairs_list, { key = key, value = value })
	end

	table.sort( pairs_list, function( a, b )
		if asc then
			return a.value < b.value
		else
			return a.value > b.value
		end
	end)

	return pairs_list
end




return dict