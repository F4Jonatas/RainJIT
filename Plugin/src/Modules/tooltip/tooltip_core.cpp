/**
 * @file tooltip_core.cpp
 * @brief Tooltip lifecycle, positioning and refresh logic.
 * @license GPL v2.0 License
 *
 * @details
 * Refresh strategy (the reason this tooltip is cheap):
 * - Render only when content/style changed, the monitor DPI changed or
 *   there is no bitmap yet
 * - Present (UpdateLayeredWindow) only after a render or a style change
 * - Otherwise only SetWindowPos is used to move the window
 * - Fading only changes the constant alpha of the window (see Window)
 */

#include "tooltip.hpp"

#include "tooltip_gdiplus.hpp"
#include "tooltip_renderer.hpp"
#include "tooltip_runtime.hpp"

#include <algorithm>
#include <utility>

namespace tooltip {



	Tooltip::Tooltip() = default;



	Tooltip::~Tooltip() {
		destroy();
	}



	bool Tooltip::open( std::string text, Options options ) noexcept {
		try {
			if ( !Runtime::acquire() ) {
				Warning = "failed to initialize the rendering engine (GDI+, Direct2D or DirectWrite)";
				return false;
			}

			Acquired = true;

			Opts = std::move( options );
			Text = std::move( text );

			if ( !Win.create( Opts.Topmost, Opts.ClickThrough ) ) {
				Warning = "failed to create the window";
				destroy();
				return false;
			}

			Win.OnGone = &Tooltip::onGone;
			Win.Context = this;

			Dirty dirty;
			dirty.Render = true;
			dirty.Place = true;
			dirty.Style = true;
			dirty.Timer = true;
			dirty.Show = true;

			if ( !refresh( dirty ) ) {
				destroy();
				return false;
			}

			return true;
		} catch ( ... ) {
			destroy();
			return false;
		}
	}



	bool Tooltip::commit( Options options, std::string text, const Dirty &dirty ) noexcept {
		try {
			Opts = std::move( options );
			Text = std::move( text );
		} catch ( ... ) {
			return false;
		}

		return refresh( dirty );
	}



	bool Tooltip::moveTo( int x, int y ) noexcept {
		try {
			Opts.X = x;
			Opts.Y = y;
		} catch ( ... ) {
			return false;
		}

		Dirty dirty;
		dirty.Place = true;

		return refresh( dirty );
	}



	void Tooltip::show() noexcept {
		if ( !alive() )
			return;

		Win.AutoFadeMs = Opts.FadeDuration;
		Win.show( Opts.FadeDuration, true );
		Win.setTimeout( Opts.Timeout );
	}



	void Tooltip::hide() noexcept {
		Win.AutoFadeMs = Opts.FadeDuration;
		Win.hide( Opts.FadeDuration );
	}



	bool Tooltip::visible() const noexcept {
		return Win.visible();
	}



	bool Tooltip::alive() const noexcept {
		return Win.Handle != nullptr;
	}



	void Tooltip::destroy() noexcept {
		Win.destroy();
		Canvas.release();

		// GDI+ objects must die before the runtime is released.
		Icon.reset();
		Image.reset();
		IconPath.clear();
		ImagePath.clear();

		if ( Acquired ) {
			Acquired = false;
			Runtime::release();
		}
	}



	void Tooltip::close( std::uint32_t fadeMs ) noexcept {
		if ( fadeMs == 0 || !alive() || !Win.visible() ) {
			destroy();
			return;
		}

		Win.AutoFadeMs = Opts.FadeDuration;
		Win.close( fadeMs );
	}



	void Tooltip::onGone( void *context ) noexcept {
		// The window is already gone: release everything else (bitmaps, runtime reference).
		if ( context )
			static_cast<Tooltip *>( context )->destroy();
	}



	std::string Tooltip::takeWarning() noexcept {
		std::string out;
		out.swap( Warning );
		return out;
	}



	void Tooltip::loadImages() {
		if ( ImagePath != Opts.Image ) {
			ImagePath = Opts.Image;
			Image = ImagePath.empty() ? nullptr : Renderer::load( ImagePath );

			if ( !ImagePath.empty() && !Image )
				Warning += "failed to load image: " + ImagePath + "\n";
		}

		if ( IconPath != Opts.Icon ) {
			IconPath = Opts.Icon;
			Icon = IconPath.empty() ? nullptr : Renderer::load( IconPath );

			if ( !IconPath.empty() && !Icon )
				Warning += "failed to load icon: " + IconPath + "\n";
		}
	}



	void Tooltip::applyBackdrop() {
		if ( Opts.Effect == AppliedEffect && Opts.BackdropCorner == AppliedCorner && Opts.Dark == AppliedDark && Opts.BackdropTint == AppliedTint )
			return;

		// Only touch the system when a backdrop is (or was) involved.
		const bool involved = Opts.Effect != Backdrop::None || AppliedEffect != Backdrop::None;

		AppliedEffect = Opts.Effect;
		AppliedCorner = Opts.BackdropCorner;
		AppliedDark = Opts.Dark;
		AppliedTint = Opts.BackdropTint;

		if ( !involved )
			return;

		const BackdropResult result = Win.setBackdrop( Opts.Effect, Opts.BackdropCorner, Opts.Dark, Opts.BackdropTint );

		if ( result == BackdropResult::Unsupported ) {
			if ( Opts.Effect == Backdrop::Acrylic )
				Warning += "acrylic requires Windows 10 1803 (build 17134) or newer\n";
			else
				Warning += "mica requires Windows 11 22H2 (build 22621) or newer\n";
		} else if ( result == BackdropResult::Failed )
			Warning += "failed to apply the backdrop\n";
	}



	bool Tooltip::refreshImpl( const Dirty &dirty ) {
		if ( !alive() )
			return false;

		POINT cursor{};
		GetCursorPos( &cursor );

		const POINT anchor{ Opts.X ? static_cast<LONG>( *Opts.X ) : cursor.x, Opts.Y ? static_cast<LONG>( *Opts.Y ) : cursor.y };

		const MonitorInfo monitor = MonitorInfo::at( anchor );

		// ---- render only when needed ---------------------------------------
		bool rendered = false;

		if ( dirty.Render || Canvas.Bits == nullptr || monitor.Dpi != LastDpi ) {
			loadImages();

			const RenderRequest request{ Opts, Text, Icon.get(), Image.get(), static_cast<double>( monitor.Dpi ) / 96.0 };

			if ( !Renderer::render( Canvas, request ) )
				return false;

			LastDpi = monitor.Dpi;
			rendered = true;
		}

		// ---- position (flip near the edges, then clamp) --------------------
		const RECT &work = monitor.Work;
		const int w = Canvas.Width;
		const int h = Canvas.Height;

		int x = 0;
		int y = 0;

		if ( Opts.X )
			x = *Opts.X;
		else {
			x = static_cast<int>( cursor.x ) + Opts.OffsetX;

			if ( x + w > work.right )
				x = static_cast<int>( cursor.x ) - Opts.OffsetX - w;
		}

		if ( Opts.Y )
			y = *Opts.Y;
		else {
			y = static_cast<int>( cursor.y ) + Opts.OffsetY;

			if ( y + h > work.bottom )
				y = static_cast<int>( cursor.y ) - Opts.OffsetY - h;
		}

		x = ( std::max )( static_cast<int>( work.left ), ( std::min )( x, static_cast<int>( work.right ) - w ) );
		y = ( std::max )( static_cast<int>( work.top ), ( std::min )( y, static_cast<int>( work.bottom ) - h ) );

		// ---- window attributes ---------------------------------------------
		Win.AutoFadeMs = Opts.FadeDuration;

		if ( dirty.Style ) {
			Win.setTopmost( Opts.Topmost );
			Win.setClickThrough( Opts.ClickThrough );
			Win.setLevel( Opts.Opacity );
			applyBackdrop();
		}

		// ---- present or just move ------------------------------------------
		if ( rendered || dirty.Style ) {
			if ( !Win.present( Canvas, x, y ) )
				return false;
		} else
			Win.moveTo( x, y );

		// ---- visibility (fade-in) + timer ----------------------------------
		if ( dirty.Show )
			Win.show( Opts.FadeDuration, rendered );

		if ( dirty.Show || dirty.Timer )
			Win.setTimeout( Opts.Timeout );

		return true;
	}



	bool Tooltip::refresh( const Dirty &dirty ) noexcept {
		try {
			return refreshImpl( dirty );
		} catch ( ... ) {
			return false;
		}
	}

} // namespace tooltip
