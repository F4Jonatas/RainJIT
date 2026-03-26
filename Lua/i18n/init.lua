-- cSpell:ignoreRegExp cdef|Jonatas|Tchau|myproject|Metatable


--- Lightweight internationalization helper.
--
-- This module detects the user's UI language on Windows and attempts to
-- load a corresponding locale dictionary. It provides a callable interface
-- to translate strings using the loaded dictionary.
--
-- The module expects locale files to follow this pattern:
--   locales.<locale>.lua
--
-- Example:
--   locales.en-us
--   locales.pt-br
--
-- Each locale file must return a table where keys are source strings and
-- values are translated strings.
--
-- Example dictionary file:
--   return {
--     ["Hello"] = "Olá",
--     ["Goodbye"] = "Tchau"
--   }
--
-- The language detection relies on a dictionary mapping Windows language
-- identifiers to locale codes.
--
-- Example structure of `i18n.dictionary`:
--
--   {
--     ['4'] = {
--       code = 0x0004,
--       country = 'zh-CHS',
--       meaning = 'Chinese - Simplified'
--     }
--   }
--
-- @module i18n
-- @author F4Jonatas
-- @version 1.3.7
--
local ffi = require( 'ffi' )



--- FFI bindings to required Win32 and CRT functions.
--
-- `GetUserDefaultUILanguage` retrieves the current user's UI language ID.
ffi.cdef[[

	// Returns the user's default UI language identifier.
	unsigned short __stdcall GetUserDefaultUILanguage(void);
]]



--- Module table.
-- Acts as both a namespace and metatable for instances.
-- @table M
local M = {}



--- Metatable index.
-- Enables method lookup on the module instance.
M.__index = M



--- Current locale code.
--
-- Default fallback locale used when detection fails.
--
-- @field locale
-- @string
-- @default "en-us"
M.locale = 'en-us'



--- Base path for locale modules.
--
-- Example:
--   i18n.path = "myproject."
--
-- Resulting require:
--
--   require("myproject.locales.en-us")
--
-- @field path
-- @string
M.path = ''



--- Callable interface for translation.
--
-- Allows the module to be used as a function:
--   local i18n = require("i18n")
--   print(i18n("Hello"))
--
-- If the key is not present in the dictionary,
-- the original text is returned.
--
-- @tparam string text Source string
-- @treturn string translated string or original text
M.__call = function( self, text )
	return self.dict and self.dict[ text ] or text
end



--- Detects the system language and loads the corresponding dictionary.
--
-- The process:
--
-- 1. Query Windows UI language via `GetUserDefaultUILanguage`.
-- 2. Lookup language metadata in `i18n.dictionary`.
-- 3. Convert the country code to lowercase.
-- 4. Attempt to require `locales.<locale>`.
-- 5. If loading fails, fallback to an empty dictionary.
--
-- The loaded dictionary is stored in `M.dict`.
--
-- @usage
--   local i18n = require("i18n")
--   i18n.language()
--
--   print(i18n("Hello"))
--
-- @return nil
M.language = function()
	local dict   = require( 'i18n.dictionary' )
	local code   = ffi.C.GetUserDefaultUILanguage()
	local osLang = dict[ tostring( code )]

	if osLang then
		M.locale = osLang.country:lower()

		local file = M.path ..'locales.'.. M.locale
		local ok, dict = pcall( require, file )

		M.dict = ok and dict or {}
	end
end



--- Module return.
--
-- The module returns a table configured with `M` as its metatable,
-- enabling callable behavior and method access.
--
-- @usage
--   local i18n = require("i18n")
--   i18n.language()
--   print(i18n("Hello"))
--
-- @return table i18n module instance
return setmetatable({}, M )