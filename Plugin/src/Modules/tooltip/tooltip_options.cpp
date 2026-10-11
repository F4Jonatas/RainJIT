/**
 * @file tooltip_options.cpp
 * @brief Lua option parser for the tooltip module.
 * @license GPL v2.0 License
 *
 * @details
 * Every reader follows the same contract:
 * - key absent (nil)  → field untouched
 * - valid value       → field assigned, flag raised only if the value changed
 * - invalid value     → error recorded, field untouched
 */

#include "tooltip_options.hpp"

#include <lua.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <utility>

namespace tooltip {



	void ParseError::set( const char *fmt, ... ) noexcept {
		va_list args;
		va_start( args, fmt );
		std::vsnprintf( message, sizeof( message ), fmt, args );
		va_end( args );
	}



	namespace {

		/**
		 * @brief Converts a Lua number to 0xRRGGBBAA.
		 *
		 * Negative values (as produced by LuaJIT's `bit` library) wrap to their
		 * unsigned 32-bit representation.
		 */
		inline Rgba toRgba( double value ) noexcept {
			return static_cast<Rgba>( static_cast<std::int64_t>( value ) );
		}



		template <typename T> inline bool assign( T &dst, const T &src ) {
			if ( dst == src )
				return false;

			dst = src;
			return true;
		}



		/// @brief Reads a number from an array slot of a table (no error on mismatch).
		bool tableNumber( lua_State *L, int tbl, int slot, double &out ) {
			lua_rawgeti( L, tbl, slot );
			const bool ok = lua_type( L, -1 ) == LUA_TNUMBER;

			if ( ok )
				out = lua_tonumber( L, -1 );

			lua_pop( L, 1 );
			return ok;
		}



		template <typename T> void readNumber( lua_State *L, int t, const char *key, T &field, bool &flag, ParseError &err, double lo, double hi ) {
			if ( err )
				return;

			lua_getfield( L, t, key );
			const int type = lua_type( L, -1 );

			if ( type == LUA_TNUMBER ) {
				const double v = ( std::clamp )( lua_tonumber( L, -1 ), lo, hi );
				T converted;

				if constexpr ( std::is_integral_v<T> )
					converted = static_cast<T>( std::llround( v ) );
				else
					converted = static_cast<T>( v );

				if ( assign( field, converted ) )
					flag = true;
			} else if ( type != LUA_TNIL )
				err.set( "tooltip option '%s' must be a number", key );

			lua_pop( L, 1 );
		}



		void readBool( lua_State *L, int t, const char *key, bool &field, bool &flag, ParseError &err ) {
			if ( err )
				return;

			lua_getfield( L, t, key );
			const int type = lua_type( L, -1 );

			if ( type == LUA_TBOOLEAN ) {
				const bool v = lua_toboolean( L, -1 ) != 0;

				if ( assign( field, v ) )
					flag = true;
			} else if ( type != LUA_TNIL )
				err.set( "tooltip option '%s' must be a boolean", key );

			lua_pop( L, 1 );
		}



		/// @brief String option. `false` clears the value.
		void readString( lua_State *L, int t, const char *key, std::string &field, bool &flag, ParseError &err ) {
			if ( err )
				return;

			lua_getfield( L, t, key );
			const int type = lua_type( L, -1 );

			if ( type == LUA_TSTRING ) {
				size_t len = 0;
				const char *s = lua_tolstring( L, -1, &len );
				const std::string v( s, len );

				if ( assign( field, v ) )
					flag = true;
			} else if ( type == LUA_TBOOLEAN && !lua_toboolean( L, -1 ) ) {
				const std::string v;

				if ( assign( field, v ) )
					flag = true;
			} else if ( type != LUA_TNIL )
				err.set( "tooltip option '%s' must be a string or false", key );

			lua_pop( L, 1 );
		}



		void readColor( lua_State *L, int t, const char *key, Rgba &field, bool &flag, ParseError &err ) {
			if ( err )
				return;

			lua_getfield( L, t, key );
			const int type = lua_type( L, -1 );

			if ( type == LUA_TNUMBER ) {
				if ( assign( field, toRgba( lua_tonumber( L, -1 ) ) ) )
					flag = true;
			} else if ( type != LUA_TNIL )
				err.set( "tooltip option '%s' must be a color number (0xRRGGBBAA)", key );

			lua_pop( L, 1 );
		}



		/// @brief Optional color. `false` clears the value.
		void readOptColor( lua_State *L, int t, const char *key, std::optional<Rgba> &field, bool &flag, ParseError &err ) {
			if ( err )
				return;

			lua_getfield( L, t, key );
			const int type = lua_type( L, -1 );
			std::optional<Rgba> next = field;

			if ( type == LUA_TNUMBER )
				next = toRgba( lua_tonumber( L, -1 ) );
			else if ( type == LUA_TBOOLEAN && !lua_toboolean( L, -1 ) )
				next = std::nullopt;
			else if ( type != LUA_TNIL )
				err.set( "tooltip option '%s' must be a color number or false", key );

			if ( !err && next != field ) {
				field = next;
				flag = true;
			}

			lua_pop( L, 1 );
		}



		/// @brief Optional integer. `false` clears the value.
		void readOptInt( lua_State *L, int t, const char *key, std::optional<int> &field, bool &flag, ParseError &err ) {
			if ( err )
				return;

			lua_getfield( L, t, key );
			const int type = lua_type( L, -1 );
			std::optional<int> next = field;

			if ( type == LUA_TNUMBER )
				next = static_cast<int>( std::llround( ( std::clamp )( lua_tonumber( L, -1 ), -1.0e6, 1.0e6 ) ) );
			else if ( type == LUA_TBOOLEAN && !lua_toboolean( L, -1 ) )
				next = std::nullopt;
			else if ( type != LUA_TNIL )
				err.set( "tooltip option '%s' must be a number or false", key );

			if ( !err && next != field ) {
				field = next;
				flag = true;
			}

			lua_pop( L, 1 );
		}



		/// @brief Timeout in ms. `false` disables it.
		void readTimeout( lua_State *L, int t, const char *key, std::uint32_t &field, bool &flag, ParseError &err ) {
			if ( err )
				return;

			lua_getfield( L, t, key );
			const int type = lua_type( L, -1 );
			std::uint32_t next = field;

			if ( type == LUA_TNUMBER )
				next = static_cast<std::uint32_t>( std::llround( ( std::clamp )( lua_tonumber( L, -1 ), 0.0, 86400000.0 ) ) );
			else if ( type == LUA_TBOOLEAN && !lua_toboolean( L, -1 ) )
				next = 0;
			else if ( type != LUA_TNIL )
				err.set( "tooltip option '%s' must be a number (ms) or false", key );

			if ( !err && assign( field, next ) )
				flag = true;

			lua_pop( L, 1 );
		}



		/// @brief Background: a color number or `{ from, to [, angle] }`.
		void readFill( lua_State *L, int t, const char *key, Fill &field, bool &flag, ParseError &err ) {
			if ( err )
				return;

			lua_getfield( L, t, key );
			const int type = lua_type( L, -1 );
			Fill next = field;

			if ( type == LUA_TNUMBER ) {
				next.From = toRgba( lua_tonumber( L, -1 ) );
				next.To = next.From;
				next.Gradient = false;
			} else if ( type == LUA_TTABLE ) {
				const int tbl = lua_gettop( L );
				double from = 0.0, to = 0.0, angle = next.Angle;

				if ( !tableNumber( L, tbl, 1, from ) || !tableNumber( L, tbl, 2, to ) )
					err.set( "tooltip option '%s' gradient must be { color1, color2 [, angle] }", key );
				else {
					tableNumber( L, tbl, 3, angle );
					next.From = toRgba( from );
					next.To = toRgba( to );
					next.Angle = angle;
					next.Gradient = true;
				}
			} else if ( type != LUA_TNIL )
				err.set( "tooltip option '%s' must be a color number or a gradient table", key );

			if ( !err && assign( field, next ) )
				flag = true;

			lua_pop( L, 1 );
		}



		/// @brief Margin: a number (all sides) or `{ left, top, right, bottom }`.
		void readMargin( lua_State *L, int t, const char *key, Margin &field, bool &flag, ParseError &err ) {
			if ( err )
				return;

			lua_getfield( L, t, key );
			const int type = lua_type( L, -1 );
			Margin next = field;

			if ( type == LUA_TNUMBER ) {
				const double v = ( std::max )( 0.0, lua_tonumber( L, -1 ) );
				next = Margin{ v, v, v, v };
			} else if ( type == LUA_TTABLE ) {
				const int tbl = lua_gettop( L );
				double l = 0.0, tp = 0.0, r = 0.0, b = 0.0;

				if ( !tableNumber( L, tbl, 1, l ) || !tableNumber( L, tbl, 2, tp ) || !tableNumber( L, tbl, 3, r ) || !tableNumber( L, tbl, 4, b ) )
					err.set( "tooltip option '%s' must be a number or { left, top, right, bottom }", key );
				else
					next = Margin{ ( std::max )( 0.0, l ), ( std::max )( 0.0, tp ), ( std::max )( 0.0, r ), ( std::max )( 0.0, b ) };
			} else if ( type != LUA_TNIL )
				err.set( "tooltip option '%s' must be a number or { left, top, right, bottom }", key );

			if ( !err && assign( field, next ) )
				flag = true;

			lua_pop( L, 1 );
		}



		void readAlign( lua_State *L, int t, const char *key, Align &field, bool &flag, ParseError &err ) {
			if ( err )
				return;

			lua_getfield( L, t, key );
			const int type = lua_type( L, -1 );

			if ( type == LUA_TSTRING ) {
				std::string v = lua_tostring( L, -1 );
				std::transform( v.begin(), v.end(), v.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );

				Align next = field;

				if ( v == "left" )
					next = Align::Left;
				else if ( v == "center" )
					next = Align::Center;
				else if ( v == "right" )
					next = Align::Right;
				else
					err.set( "tooltip option '%s' must be \"left\", \"center\" or \"right\"", key );

				if ( !err && assign( field, next ) )
					flag = true;
			} else if ( type != LUA_TNIL )
				err.set( "tooltip option '%s' must be a string", key );

			lua_pop( L, 1 );
		}



		/**
		 * @brief Reads a string option that must match one of @p choices (case-insensitive).
		 *
		 * @param choices    Lowercase names and their values.
		 * @param expected   Human readable list used in the error message.
		 * @param falseValue Value assigned when the option is `false` (nullptr rejects false).
		 */
		template <typename E, std::size_t N> void readChoice( lua_State *L, int t, const char *key, E &field, bool &flag, ParseError &err, const std::pair<const char *, E> ( &choices )[N], const char *expected, const E *falseValue ) {
			if ( err )
				return;

			lua_getfield( L, t, key );
			const int type = lua_type( L, -1 );
			E next = field;

			if ( type == LUA_TSTRING ) {
				std::string v = lua_tostring( L, -1 );
				std::transform( v.begin(), v.end(), v.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );

				bool found = false;

				for ( const auto &choice : choices ) {
					if ( v == choice.first ) {
						next = choice.second;
						found = true;
						break;
					}
				}

				if ( !found )
					err.set( "tooltip option '%s' must be %s", key, expected );
			} else if ( type == LUA_TBOOLEAN && !lua_toboolean( L, -1 ) && falseValue )
				next = *falseValue;
			else if ( type != LUA_TNIL )
				err.set( "tooltip option '%s' must be %s", key, expected );

			if ( !err && assign( field, next ) )
				flag = true;

			lua_pop( L, 1 );
		}



		/// @brief `dark`: true / false, or "auto" to follow the system theme.
		void readDark( lua_State *L, int t, const char *key, std::optional<bool> &field, bool &flag, ParseError &err ) {
			if ( err )
				return;

			lua_getfield( L, t, key );
			const int type = lua_type( L, -1 );
			std::optional<bool> next = field;

			if ( type == LUA_TBOOLEAN )
				next = lua_toboolean( L, -1 ) != 0;
			else if ( type == LUA_TSTRING && std::string( lua_tostring( L, -1 ) ) == "auto" )
				next = std::nullopt;
			else if ( type != LUA_TNIL )
				err.set( "tooltip option '%s' must be a boolean or \"auto\"", key );

			if ( !err && next != field ) {
				field = next;
				flag = true;
			}

			lua_pop( L, 1 );
		}



		/// @brief Parses "bold italic underline strikeout" (any order, any separator).
		bool parseFontStyle( const char *text, std::uint8_t &mask ) {
			std::uint8_t result = 0;
			std::string word;

			auto flush = [&]() -> bool {
				if ( word.empty() )
					return true;

				bool ok = true;

				if ( word == "regular" || word == "normal" ) {
				} else if ( word == "bold" )
					result = static_cast<std::uint8_t>( result | FontFlags::Bold );
				else if ( word == "italic" )
					result = static_cast<std::uint8_t>( result | FontFlags::Italic );
				else if ( word == "underline" )
					result = static_cast<std::uint8_t>( result | FontFlags::Underline );
				else if ( word == "strikeout" || word == "strike" )
					result = static_cast<std::uint8_t>( result | FontFlags::Strikeout );
				else
					ok = false;

				word.clear();
				return ok;
			};

			for ( const char *p = text;; ++p ) {
				const unsigned char c = static_cast<unsigned char>( *p );

				if ( std::isalpha( c ) )
					word.push_back( static_cast<char>( std::tolower( c ) ) );
				else {
					if ( !flush() )
						return false;

					if ( c == 0 )
						break;
				}
			}

			mask = result;
			return true;
		}



		void readStyle( lua_State *L, int t, const char *key, std::uint8_t &field, bool &flag, ParseError &err ) {
			if ( err )
				return;

			lua_getfield( L, t, key );
			const int type = lua_type( L, -1 );

			if ( type == LUA_TSTRING ) {
				std::uint8_t next = field;

				if ( !parseFontStyle( lua_tostring( L, -1 ), next ) )
					err.set( "tooltip option '%s' accepts: regular, bold, italic, underline, strikeout", key );
				else if ( assign( field, next ) )
					flag = true;
			} else if ( type != LUA_TNIL )
				err.set( "tooltip option '%s' must be a string", key );

			lua_pop( L, 1 );
		}

	} // namespace



	void Options::apply( lua_State *L, int index, Dirty &d, ParseError &err, std::string *unknown ) {
		if ( index < 0 )
			index = lua_gettop( L ) + index + 1;

		// placement
		readOptInt( L, index, "x", X, d.Place, err );
		readOptInt( L, index, "y", Y, d.Place, err );
		readNumber( L, index, "offsetX", OffsetX, d.Place, err, -100000.0, 100000.0 );
		readNumber( L, index, "offsetY", OffsetY, d.Place, err, -100000.0, 100000.0 );

		// text
		readString( L, index, "font", Font, d.Render, err );
		readNumber( L, index, "fontSize", FontSize, d.Render, err, 1.0, 512.0 );
		readStyle( L, index, "fontStyle", FontStyle, d.Render, err );
		readColor( L, index, "textColor", TextColor, d.Render, err );

		// title + icon
		readString( L, index, "title", Title, d.Render, err );
		readString( L, index, "titleFont", TitleFont, d.Render, err );
		readNumber( L, index, "titleFontSize", TitleFontSize, d.Render, err, 0.0, 512.0 );
		readStyle( L, index, "titleFontStyle", TitleFontStyle, d.Render, err );
		readOptColor( L, index, "titleColor", TitleColor, d.Render, err );
		readString( L, index, "icon", Icon, d.Render, err );
		readNumber( L, index, "iconSize", IconSize, d.Render, err, 1.0, 1024.0 );
		readNumber( L, index, "iconGap", IconGap, d.Render, err, 0.0, 1024.0 );

		// image
		readString( L, index, "image", Image, d.Render, err );
		readNumber( L, index, "imageWidth", ImageWidth, d.Render, err, 0.0, 16384.0 );
		readNumber( L, index, "imageHeight", ImageHeight, d.Render, err, 0.0, 16384.0 );
		readNumber( L, index, "imageOpacity", ImageOpacity, d.Render, err, 0.0, 255.0 );

		// box
		readFill( L, index, "backgroundColor", Background, d.Render, err );
		readColor( L, index, "borderColor", BorderColor, d.Render, err );
		readNumber( L, index, "borderWidth", BorderWidth, d.Render, err, 0.0, 64.0 );
		readNumber( L, index, "radius", Radius, d.Render, err, 0.0, 1024.0 );
		readMargin( L, index, "margin", Padding, d.Render, err );
		readNumber( L, index, "maxWidth", MaxWidth, d.Render, err, 0.0, 16384.0 );
		readAlign( L, index, "align", Alignment, d.Render, err );

		// window
		readNumber( L, index, "opacity", Opacity, d.Style, err, 0.0, 255.0 );
		readBool( L, index, "topmost", Topmost, d.Style, err );
		readBool( L, index, "clickThrough", ClickThrough, d.Style, err );
		readTimeout( L, index, "timeout", Timeout, d.Timer, err );

		// fadeDuration is read on use (show / hide / destroy), so it needs no dirty flag
		bool fadeChanged = false;
		readTimeout( L, index, "fadeDuration", FadeDuration, fadeChanged, err );

		// backdrop (changing any of these needs a new window attribute and a redraw,
		// because the drawn corner radius follows the backdrop corner)
		static const std::pair<const char *, Backdrop> kBackdrops[] = { { "none", Backdrop::None }, { "acrylic", Backdrop::Acrylic }, { "mica", Backdrop::Mica }, { "micaalt", Backdrop::MicaAlt } };

		static const std::pair<const char *, Corner> kCorners[] = { { "default", Corner::Default }, { "round", Corner::Round }, { "small", Corner::Small }, { "none", Corner::None } };

		const Backdrop noBackdrop = Backdrop::None;
		const Corner defaultCorner = Corner::Default;
		bool backdropChange = false;

		readChoice( L, index, "backdrop", Effect, backdropChange, err, kBackdrops, "\"acrylic\", \"mica\", \"micaAlt\", \"none\" or false", &noBackdrop );
		readChoice( L, index, "backdropCorner", BackdropCorner, backdropChange, err, kCorners, "\"round\", \"small\", \"none\" or \"default\"", &defaultCorner );
		readDark( L, index, "dark", Dark, backdropChange, err );
		readOptColor( L, index, "backdropTint", BackdropTint, backdropChange, err );

		if ( backdropChange ) {
			d.Style = true;
			d.Render = true;
		}

		// unknown keys are reported (not an error), so a typo like `fontName` is not silent
		if ( unknown ) {
			static const char *const kKnown[] = { "x", "y", "offsetX", "offsetY", "font", "fontSize", "fontStyle", "textColor", "title", "titleFont", "titleFontSize", "titleFontStyle", "titleColor", "icon", "iconSize", "iconGap", "image", "imageWidth", "imageHeight", "imageOpacity", "backgroundColor", "borderColor", "borderWidth", "radius", "margin", "maxWidth", "align", "opacity", "timeout", "fadeDuration", "topmost", "clickThrough", "backdrop", "backdropCorner", "backdropTint", "dark" };

			lua_pushnil( L );

			while ( lua_next( L, index ) != 0 ) {
				// key at -2, value at -1. Only string keys are inspected (lua_tostring would
				// convert a number key in place and break the traversal).
				if ( lua_type( L, -2 ) == LUA_TSTRING ) {
					const char *key = lua_tostring( L, -2 );
					bool known = false;

					for ( const char *name : kKnown ) {
						if ( std::strcmp( name, key ) == 0 ) {
							known = true;
							break;
						}
					}

					if ( !known ) {
						if ( !unknown->empty() )
							unknown->append( ", " );

						unknown->append( key );
					}
				}

				lua_pop( L, 1 );
			}
		}
	}

} // namespace tooltip
