/**
 * @file tooltip_renderer.cpp
 * @brief GDI+ renderer that draws a tooltip into a Surface.
 * @license GPL v2.0 License
 *
 * @details
 * Rendering happens in two passes:
 * 1. Measure: fonts, text, icon and image are measured on a 1x1 probe bitmap
 * 2. Draw:    the surface is resized and every block is drawn once
 */

#include "tooltip_renderer.hpp"

#include "tooltip_gdiplus.hpp"

#include <utils/strings.hpp>

#include <algorithm>
#include <cmath>

namespace tooltip {

	namespace {

		constexpr double kPointToPixel = 96.0 / 72.0;
		constexpr double kBlockGap = 6.0;
		constexpr float kUnbounded = 100000.0f;
		constexpr int kMaxSurface = 16384;



		inline Gdiplus::Color toColor( Rgba c ) noexcept {
			return Gdiplus::Color( static_cast<BYTE>( c & 0xFF ), static_cast<BYTE>( ( c >> 24 ) & 0xFF ), static_cast<BYTE>( ( c >> 16 ) & 0xFF ), static_cast<BYTE>( ( c >> 8 ) & 0xFF ) );
		}



		/**
		 * @struct FontSet
		 * @brief A font together with the family it was created from.
		 */
		struct FontSet {
			std::unique_ptr<Gdiplus::FontFamily> Family;
			std::unique_ptr<Gdiplus::Font> Font;
		};



		/**
		 * @brief Creates a font, falling back to the generic sans-serif family
		 *        and to the regular style when the requested ones are unavailable.
		 */
		FontSet makeFont( const std::string &name, double pixels, int style ) {
			FontSet set;

			const std::wstring wide = utf8_to_wstring( name );
			set.Family = std::make_unique<Gdiplus::FontFamily>( wide.c_str() );

			if ( !set.Family->IsAvailable() )
				set.Family.reset( Gdiplus::FontFamily::GenericSansSerif()->Clone() );

			const Gdiplus::REAL size = static_cast<Gdiplus::REAL>( ( std::max )( 1.0, pixels ) );

			set.Font = std::make_unique<Gdiplus::Font>( set.Family.get(), size, style, Gdiplus::UnitPixel );

			if ( !set.Font->IsAvailable() )
				set.Font = std::make_unique<Gdiplus::Font>( set.Family.get(), size, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel );

			return set;
		}



		void roundedRect( Gdiplus::GraphicsPath &path, const Gdiplus::RectF &r, float radius ) {
			radius = ( std::min )( radius, ( std::min )( r.Width, r.Height ) / 2.0f );

			if ( radius <= 0.5f ) {
				path.AddRectangle( r );
				return;
			}

			const float d = radius * 2.0f;

			path.AddArc( r.X, r.Y, d, d, 180.0f, 90.0f );
			path.AddArc( r.X + r.Width - d, r.Y, d, d, 270.0f, 90.0f );
			path.AddArc( r.X + r.Width - d, r.Y + r.Height - d, d, d, 0.0f, 90.0f );
			path.AddArc( r.X, r.Y + r.Height - d, d, d, 90.0f, 90.0f );
			path.CloseFigure();
		}



		/// @brief Draws a bitmap scaled into @p dest with a constant opacity.
		void drawBitmap( Gdiplus::Graphics &g, Gdiplus::Bitmap *bitmap, const Gdiplus::RectF &dest, std::uint8_t opacity ) {
			Gdiplus::ImageAttributes attributes;

			Gdiplus::ColorMatrix matrix = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, static_cast<float>( opacity ) / 255.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };

			attributes.SetColorMatrix( &matrix, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap );
			attributes.SetWrapMode( Gdiplus::WrapModeTileFlipXY );

			// clang-format off
	g.DrawImage(
		bitmap,
		dest,
		0.0f, 0.0f,
		static_cast<float>( bitmap->GetWidth() ),
		static_cast<float>( bitmap->GetHeight() ),
		Gdiplus::UnitPixel,
		&attributes
	);
			// clang-format on
		}



		bool renderImpl( Surface &surface, const RenderRequest &rq ) {
			const Options &o = rq.Opts;
			const double s = rq.Scale > 0.0 ? rq.Scale : 1.0;

			// ---- metrics in device pixels --------------------------------------
			const float ml = static_cast<float>( o.Padding.Left * s );
			const float mt = static_cast<float>( o.Padding.Top * s );
			const float mr = static_cast<float>( o.Padding.Right * s );
			const float mb = static_cast<float>( o.Padding.Bottom * s );
			const float borderW = static_cast<float>( o.BorderWidth * s );
			const float blockGap = static_cast<float>( kBlockGap * s );
			const float iconSize = static_cast<float>( o.IconSize * s );
			const float iconGap = static_cast<float>( o.IconGap * s );

			const bool hasTitle = !o.Title.empty();
			const bool hasIcon = hasTitle && rq.Icon != nullptr;
			const bool hasImage = rq.Image != nullptr;
			const bool hasText = !rq.Text.empty();
			const bool wraps = o.MaxWidth > 0.0;

			const float limit = wraps ? ( std::max )( 1.0f, static_cast<float>( o.MaxWidth * s ) - ml - mr ) : kUnbounded;

			// ---- fonts ---------------------------------------------------------
			FontSet textFont = makeFont( o.Font, o.FontSize * s * kPointToPixel, o.FontStyle );

			const double titleSize = o.TitleFontSize > 0.0 ? o.TitleFontSize : o.FontSize + 1.0;

			FontSet titleFont = makeFont( o.TitleFont.empty() ? o.Font : o.TitleFont, titleSize * s * kPointToPixel, o.TitleFontStyle );

			if ( !textFont.Font || !titleFont.Font )
				return false;

			// ---- string formats ------------------------------------------------
			Gdiplus::StringAlignment alignment = Gdiplus::StringAlignmentNear;

			if ( o.Alignment == Align::Center )
				alignment = Gdiplus::StringAlignmentCenter;
			else if ( o.Alignment == Align::Right )
				alignment = Gdiplus::StringAlignmentFar;

			int flags = Gdiplus::StringFormat::GenericTypographic()->GetFormatFlags() | Gdiplus::StringFormatFlagsMeasureTrailingSpaces;

			if ( !wraps )
				flags |= Gdiplus::StringFormatFlagsNoWrap;

			Gdiplus::StringFormat textFmt( Gdiplus::StringFormat::GenericTypographic() );
			textFmt.SetFormatFlags( flags );
			textFmt.SetAlignment( alignment );

			Gdiplus::StringFormat titleFmt( Gdiplus::StringFormat::GenericTypographic() );
			titleFmt.SetFormatFlags( flags | Gdiplus::StringFormatFlagsNoWrap );
			titleFmt.SetAlignment( Gdiplus::StringAlignmentNear );
			titleFmt.SetTrimming( Gdiplus::StringTrimmingEllipsisCharacter );

			// ---- measure pass --------------------------------------------------
			Gdiplus::Bitmap probe( 1, 1, PixelFormat32bppPARGB );
			Gdiplus::Graphics meter( &probe );
			meter.SetTextRenderingHint( Gdiplus::TextRenderingHintAntiAlias );

			auto measure = [&]( const std::wstring &str, const Gdiplus::Font *font, const Gdiplus::StringFormat *fmt, float width ) {
				Gdiplus::RectF box;
				Gdiplus::RectF layout( 0.0f, 0.0f, width, kUnbounded );
				meter.MeasureString( str.c_str(), -1, font, layout, fmt, &box );
				return Gdiplus::SizeF( std::ceil( box.Width ) + 1.0f, std::ceil( box.Height ) );
			};

			std::wstring wtitle;
			std::wstring wtext;

			float titleW = 0.0f, titleH = 0.0f, titleAvail = limit;
			float rowW = 0.0f, rowH = 0.0f;

			if ( hasTitle ) {
				wtitle = utf8_to_wstring( o.Title );

				if ( hasIcon )
					titleAvail = ( std::max )( 1.0f, limit - iconSize - iconGap );

				const Gdiplus::SizeF m = measure( wtitle, titleFont.Font.get(), &titleFmt, titleAvail );

				titleW = ( std::min )( m.Width, titleAvail );
				titleH = m.Height;
				rowW = titleW + ( hasIcon ? iconSize + iconGap : 0.0f );
				rowH = ( std::max )( titleH, hasIcon ? iconSize : 0.0f );
			}

			float imageW = 0.0f, imageH = 0.0f;

			if ( hasImage ) {
				const float srcW = static_cast<float>( rq.Image->GetWidth() );
				const float srcH = static_cast<float>( rq.Image->GetHeight() );

				if ( srcW < 1.0f || srcH < 1.0f )
					return false;

				if ( o.ImageWidth > 0.0 && o.ImageHeight > 0.0 ) {
					imageW = static_cast<float>( o.ImageWidth * s );
					imageH = static_cast<float>( o.ImageHeight * s );
				} else if ( o.ImageWidth > 0.0 ) {
					imageW = static_cast<float>( o.ImageWidth * s );
					imageH = imageW * srcH / srcW;
				} else if ( o.ImageHeight > 0.0 ) {
					imageH = static_cast<float>( o.ImageHeight * s );
					imageW = imageH * srcW / srcH;
				} else {
					imageW = static_cast<float>( srcW * s );
					imageH = static_cast<float>( srcH * s );
				}

				if ( imageW > limit ) {
					imageH *= limit / imageW;
					imageW = limit;
				}
			}

			float textW = 0.0f, textH = 0.0f;

			if ( hasText ) {
				wtext = utf8_to_wstring( rq.Text );

				const Gdiplus::SizeF m = measure( wtext, textFont.Font.get(), &textFmt, limit );

				textW = ( std::min )( m.Width, limit );
				textH = m.Height;
			}

			const int blocks = ( hasTitle ? 1 : 0 ) + ( hasImage ? 1 : 0 ) + ( hasText ? 1 : 0 );

			const float contentW = ( std::max )( { rowW, imageW, textW } );
			float contentH = rowH + imageH + textH;

			if ( blocks > 1 )
				contentH += blockGap * static_cast<float>( blocks - 1 );

			const int width = ( std::max )( 1, static_cast<int>( std::ceil( contentW + ml + mr ) ) );
			const int height = ( std::max )( 1, static_cast<int>( std::ceil( contentH + mt + mb ) ) );

			if ( width > kMaxSurface || height > kMaxSurface )
				return false;

			// ---- draw pass -----------------------------------------------------
			if ( !surface.resize( width, height ) )
				return false;

			surface.clear();

			Gdiplus::Bitmap target( width, height, width * 4, PixelFormat32bppPARGB, static_cast<BYTE *>( surface.Bits ) );
			if ( target.GetLastStatus() != Gdiplus::Ok )
				return false;

			Gdiplus::Graphics g( &target );
			g.SetSmoothingMode( Gdiplus::SmoothingModeAntiAlias );
			g.SetPixelOffsetMode( Gdiplus::PixelOffsetModeHalf );
			g.SetInterpolationMode( Gdiplus::InterpolationModeHighQualityBicubic );
			g.SetTextRenderingHint( Gdiplus::TextRenderingHintAntiAlias );

			// background + border
			{
				const Gdiplus::RectF box( borderW / 2.0f, borderW / 2.0f, static_cast<float>( width ) - borderW, static_cast<float>( height ) - borderW );

				// With a backdrop the system clips the window itself, so the drawn
				// shape must match the system corner (8 = round, 4 = small, 0 = none).
				double radius = o.Radius;

				if ( o.Effect != Backdrop::None ) {
					switch ( o.BackdropCorner ) {
					case Corner::Small:
						radius = 4.0;
						break;
					case Corner::None:
						radius = 0.0;
						break;
					default:
						radius = 8.0;
						break;
					}
				}

				Gdiplus::GraphicsPath path;
				roundedRect( path, box, static_cast<float>( radius * s ) );

				if ( o.Background.Gradient ) {
					// clang-format off
			Gdiplus::LinearGradientBrush brush(
				Gdiplus::RectF( 0.0f, 0.0f, static_cast<float>( width ), static_cast<float>( height ) ),
				toColor( o.Background.From ),
				toColor( o.Background.To ),
				static_cast<Gdiplus::REAL>( o.Background.Angle ),
				FALSE
			);
					// clang-format on
					g.FillPath( &brush, &path );
				} else {
					Gdiplus::SolidBrush brush( toColor( o.Background.From ) );
					g.FillPath( &brush, &path );
				}

				if ( borderW > 0.0f && ( o.BorderColor & 0xFF ) != 0 ) {
					Gdiplus::Pen pen( toColor( o.BorderColor ), borderW );
					pen.SetLineJoin( Gdiplus::LineJoinRound );
					g.DrawPath( &pen, &path );
				}
			}

			auto alignX = [&]( float blockW ) -> float {
				switch ( o.Alignment ) {
				case Align::Center:
					return ( contentW - blockW ) / 2.0f;
				case Align::Right:
					return contentW - blockW;
				default:
					return 0.0f;
				}
			};

			float y = mt;

			// title row
			if ( hasTitle ) {
				float x = ml + alignX( rowW );

				if ( hasIcon ) {
					const Gdiplus::RectF dest( x, y + ( rowH - iconSize ) / 2.0f, iconSize, iconSize );
					drawBitmap( g, rq.Icon, dest, 255 );
					x += iconSize + iconGap;
				}

				Gdiplus::SolidBrush brush( toColor( o.TitleColor.value_or( o.TextColor ) ) );
				const Gdiplus::RectF dest( x, y + ( rowH - titleH ) / 2.0f, titleW, titleH );

				g.DrawString( wtitle.c_str(), -1, titleFont.Font.get(), dest, &titleFmt, &brush );

				y += rowH + blockGap;
			}

			// image
			if ( hasImage ) {
				const Gdiplus::RectF dest( ml + alignX( imageW ), y, imageW, imageH );
				drawBitmap( g, rq.Image, dest, o.ImageOpacity );

				y += imageH + blockGap;
			}

			// text
			if ( hasText ) {
				Gdiplus::SolidBrush brush( toColor( o.TextColor ) );
				const Gdiplus::RectF dest( ml, y, contentW, textH );

				g.DrawString( wtext.c_str(), -1, textFont.Font.get(), dest, &textFmt, &brush );
			}

			g.Flush( Gdiplus::FlushIntentionSync );

			return true;
		}

	} // namespace



	bool Renderer::render( Surface &surface, const RenderRequest &request ) noexcept {
		try {
			return renderImpl( surface, request );
		} catch ( ... ) {
			return false;
		}
	}



	std::unique_ptr<Gdiplus::Bitmap> Renderer::load( const std::string &path ) noexcept {
		try {
			const std::wstring wide = utf8_to_wstring( path );

			Gdiplus::Bitmap source( wide.c_str(), FALSE );
			if ( source.GetLastStatus() != Gdiplus::Ok )
				return nullptr;

			const INT w = static_cast<INT>( source.GetWidth() );
			const INT h = static_cast<INT>( source.GetHeight() );

			if ( w <= 0 || h <= 0 )
				return nullptr;

			auto copy = std::make_unique<Gdiplus::Bitmap>( w, h, PixelFormat32bppPARGB );
			if ( copy->GetLastStatus() != Gdiplus::Ok )
				return nullptr;

			{
				Gdiplus::Graphics g( copy.get() );
				g.SetCompositingMode( Gdiplus::CompositingModeSourceCopy );
				g.DrawImage( &source, Gdiplus::Rect( 0, 0, w, h ) );
			}

			return copy;
		} catch ( ... ) {
			return nullptr;
		}
	}

} // namespace tooltip
