/**
 * @file tooltip_options.hpp
 * @brief Option model and Lua option parser for the tooltip module.
 * @license GPL v2.0 License
 *
 * @details
 * - Colors are stored as 0xRRGGBBAA
 * - Sizes are expressed in logical pixels (96 DPI) and scaled at render time
 * - Options are merged: only the keys present in the Lua table are changed
 * - A `false` value removes optional entries (x, y, title, icon, image, ...)
 *
 * @example
 * @code{.lua}
 * local tooltip = require("tooltip")
 * local tt = tooltip("Hello", {
 *     title           = "Info",
 *     backgroundColor = { 0x202020FF, 0x404040FF, 90 },
 *     margin          = { 10, 6, 10, 6 },
 * })
 * @endcode
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>

struct lua_State;

namespace tooltip {

	/// @brief Color packed as 0xRRGGBBAA.
	using Rgba = std::uint32_t;



	/**
	 * @struct FontFlags
	 * @brief Font style bit flags (values match Gdiplus::FontStyle).
	 */
	struct FontFlags {
		static constexpr std::uint8_t Regular = 0;
		static constexpr std::uint8_t Bold = 1;
		static constexpr std::uint8_t Italic = 2;
		static constexpr std::uint8_t Underline = 4;
		static constexpr std::uint8_t Strikeout = 8;
	};



	/// @brief Horizontal alignment of the content blocks.
	enum class Align : std::uint8_t {
		Left,
		Center,
		Right
	};



	/**
	 * @enum Backdrop
	 * @brief System backdrop material applied behind the tooltip.
	 *
	 * - Acrylic: SetWindowCompositionAttribute accent policy (Windows 10 1803+).
	 *            It keeps the blur while the window is not active.
	 * - Mica / MicaAlt: DWM system backdrop (Windows 11 22H2+, build 22621).
	 *
	 * @note The material is visible only where the drawn background is
	 *       translucent. Use a low alpha in `backgroundColor` to reveal it.
	 */
	enum class Backdrop : std::uint8_t {
		None,
		Acrylic,
		Mica,
		MicaAlt
	};



	/**
	 * @enum Corner
	 * @brief Corner style used by the system when a backdrop is active (Windows 11+).
	 *
	 * When a backdrop is active the drawn radius follows this value
	 * (Round / Default = 8, Small = 4, None = 0) instead of Options::Radius,
	 * so the drawn shape matches the shape clipped by the system.
	 */
	enum class Corner : std::uint8_t {
		Default,
		Round,
		Small,
		None
	};



	/**
	 * @struct Margin
	 * @brief Inner spacing between the tooltip border and its content.
	 */
	struct Margin {
		double Left = 8.0;
		double Top = 8.0;
		double Right = 8.0;
		double Bottom = 8.0;

		bool operator==( const Margin &o ) const noexcept {
			return Left == o.Left && Top == o.Top && Right == o.Right && Bottom == o.Bottom;
		}
	};



	/**
	 * @struct Fill
	 * @brief Solid color or two-stop linear gradient.
	 */
	struct Fill {
		Rgba From = 0x202020E0;
		Rgba To = 0x202020E0;
		double Angle = 90.0;
		bool Gradient = false;

		bool operator==( const Fill &o ) const noexcept {
			return From == o.From && To == o.To && Angle == o.Angle && Gradient == o.Gradient;
		}
	};



	/**
	 * @struct Dirty
	 * @brief Tracks which aspects of a tooltip changed after merging options.
	 *
	 * @details
	 * - Render: the bitmap must be redrawn
	 * - Place:  only the position changed
	 * - Style:  window attributes changed (opacity, topmost, click-through)
	 * - Timer:  the auto-hide timeout changed (fadeDuration needs no flag, it is read on use)
	 * - Show:   the call must make the tooltip visible
	 */
	struct Dirty {
		bool Render = false;
		bool Place = false;
		bool Style = false;
		bool Timer = false;
		bool Show = false;
	};



	/**
	 * @struct ParseError
	 * @brief Trivially destructible error buffer.
	 *
	 * @note It is intentionally a POD so it is safe to hold on the stack while
	 *       calling luaL_error (which may longjmp).
	 */
	struct ParseError {
		char message[192] = {};

		/// @brief Formats the error message (printf style).
		void set( const char *fmt, ... ) noexcept;

		/// @brief True when an error has been recorded.
		explicit operator bool() const noexcept {
			return message[0] != '\0';
		}
	};



	/**
	 * @struct Options
	 * @brief Complete, merged set of tooltip options.
	 */
	struct Options {

		/// @name Placement
		/// @{
		std::optional<int> X; ///< Absolute screen X. Unset follows the cursor.
		std::optional<int> Y; ///< Absolute screen Y. Unset follows the cursor.
		int OffsetX = 16; ///< Cursor offset (follow mode).
		int OffsetY = 16; ///< Cursor offset (follow mode).
		/// @}

		/// @name Text
		/// @{
		std::string Font = "Segoe UI";
		double FontSize = 12.0; ///< Points.
		std::uint8_t FontStyle = FontFlags::Regular;
		Rgba TextColor = 0xFFFFFFFF;
		/// @}

		/// @name Title and icon
		/// @{
		std::string Title;
		std::string TitleFont; ///< Empty uses Font.
		double TitleFontSize = 0.0; ///< 0 uses FontSize + 1.
		std::uint8_t TitleFontStyle = FontFlags::Bold;
		std::optional<Rgba> TitleColor; ///< Unset uses TextColor.
		std::string Icon; ///< Image path, shown next to the title.
		double IconSize = 16.0;
		double IconGap = 6.0;
		/// @}

		/// @name Image
		/// @{
		std::string Image; ///< Image path.
		double ImageWidth = 0.0; ///< 0 keeps the original size / ratio.
		double ImageHeight = 0.0;
		std::uint8_t ImageOpacity = 255;
		/// @}

		/// @name Box
		/// @{
		Fill Background;
		Rgba BorderColor = 0x606060FF;
		double BorderWidth = 1.0;
		double Radius = 6.0;
		Margin Padding;
		double MaxWidth = 0.0; ///< Outer width limit. 0 disables wrapping.
		Align Alignment = Align::Left;
		/// @}

		/// @name Window
		/// @{
		std::uint8_t Opacity = 255;
		std::uint32_t Timeout = 0; ///< Auto-hide in ms. 0 disables.
		std::uint32_t FadeDuration = 0; ///< Fade-in / fade-out time in ms. 0 disables.
		bool Topmost = true;
		bool ClickThrough = true;
		/// @}

		/// @name Backdrop
		/// @{
		Backdrop Effect = Backdrop::None; ///< Lua: backdrop.
		Corner BackdropCorner = Corner::Default; ///< Lua: backdropCorner (Windows 11).
		std::optional<bool> Dark; ///< Lua: dark. Unset follows the system theme.
		std::optional<Rgba> BackdropTint; ///< Lua: backdropTint. Acrylic tint, unset derives it from `dark`.
		/// @}



		/**
		 * @brief Merges the keys of a Lua table into this object.
		 *
		 * Only the keys present in the table are changed. Types are validated and
		 * a descriptive message is stored in @p error on the first failure.
		 * Unknown keys are not an error: they are listed in @p unknown (when given)
		 * so the host can log a warning.
		 *
		 * @param L        Lua state.
		 * @param index    Stack index of the options table.
		 * @param dirty    Receives the flags of what changed.
		 * @param error    Receives the error message, if any.
		 * @param unknown  Optional. Receives the unknown keys, separated by ", ".
		 */
		void apply( lua_State *L, int index, Dirty &dirty, ParseError &error, std::string *unknown = nullptr );
	};

} // namespace tooltip
