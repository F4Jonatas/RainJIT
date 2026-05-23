--- dotenv.lua
-- Simple dotenv parser and manager for Lua / LuaJIT.
-- Provides an instance-based API for loading, accessing,
-- modifying and saving environment key-value pairs.
--
-- @module dotenv

local Dotenv = {}
Dotenv.__index = Dotenv


--- Get a value by key.
-- Returns nil if the key does not exist.
--
-- @param key string Environment variable name
-- @return string|nil Value associated with the key
--
function Dotenv:get(key, default )
	local v = self._vars[key]
	if v ~= nil then
		return v
	end
	return default
end



--- Set a value for a key.
-- Value is always stored as a string.
--
-- @param key string Environment variable name
-- @param value string Value to set
--
function Dotenv:set(key, value)
	self._vars[key] = value or ""
end



--- Check if a key exists.
--
-- @param key string Environment variable name
-- @return boolean True if key exists
--
function Dotenv:hasKey(key)
	return self._vars[key] ~= nil
end



--- Get all keys.
--
-- @return table Array of keys
--
function Dotenv:keys()
	local list = {}
	for k, _ in pairs(self._vars) do
		list[#list + 1] = k
	end
	return list
end



--- Save current variables back to the original file.
-- Overwrites the file completely.
--
-- @return boolean True on success, false on failure
--
function Dotenv:save()
	if not self._file_path then
		return false
	end

	local file = io.open(self._file_path, "w")
	if not file then
		return false
	end

	for key, value in pairs(self._vars) do
		file:write(key, "=", value, "\n")
	end

	file:close()
	return true
end



--- Callable module entry point.
-- Allows usage: local env = require("dotenv")(path)
--
-- @param file_path string Path to the .env file
-- @return table Dotenv instance
--
return setmetatable({}, {

	--- Create a new dotenv instance and load a file.
	-- This function parses the file immediately.
	--
	-- @param file_path string Path to the .env file
	-- @return table Dotenv instance
	__call = function( self, file_path )
		assert(
			type( file_path ) == 'string',
			'Argument file_path must be a string'
		)

		local self = setmetatable({}, Dotenv)

		self._file_path = file_path
		self._vars = {}

		if file_path then
			local file = io.open(file_path, "r")
			assert( file, ' Error opening file: "'.. file_path ..'"' )

			if file then
				for line in file:lines() do
					local key, value = line:match("^%s*([%w_]+)%s*=%s*(.-)%s*$")
					if key then
						-- Strip surrounding quotes only
						value = value:gsub("^[\"'](.-)[\"']$", "%1")
						-- Preserve empty string instead of nil
						self._vars[key] = value or ""
					end
				end
				file:close()
			end
		end

		return self
	end
})
