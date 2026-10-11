/**
 * @file tooltip_renderer.cpp
 * @brief Direct2D / DirectWrite renderer that draws a tooltip into a Surface.
 * @license GPL v2.0 License
 *
 * @details
 * Rendering happens in two passes:
 * 1. Measure: DirectWrite lays out the title and the body text and the image
 *    sizes are resolved, which gives the exact surface size
 * 2. Draw:    Direct2D (DC render target bound to the surface) draws the
 *    background, border, icon, image and text in a single BeginDraw/EndDraw
 *
 * Text goes through DirectWrite, so missing glyphs use the system font
 * fallback and color fonts (emoji) are drawn in color. GDI+ is used only to
 * decode image files and to pre-scale them with high quality filtering.
 */

#include "tooltip_renderer.hpp"

#include "tooltip_gdiplus.hpp"
#include "tooltip_text.hpp"

#include <utils/strings.hpp>

#include <wrl/client.h>

#include <algorithm>
#include <cmath>

namespace tooltip {

	namespace {

		using Microsoft::WRL::ComPtr;

		constexpr double kPointToPixel = 96.0 / 72.0;
		constexpr double kBlockGap = 6.0;
		constexpr double kPi = 3.14159265358979323846;
		constexpr float kUnbounded = 100000.0f;
		constexpr int kMaxSurface = 16384;

		// D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT (Windows 8.1+), spelled as a value so
		// the module still builds with older SDK headers.
		constexpr D2D1_DRAW_TEXT_OPTIONS kColorFont = static_cast<D2D1_DRAW_TEXT_OPTIONS>( 0x4 );



		/// @brief 0xRRGGBBAA to a Direct2D color.
		inline D2D1_COLOR_F toColor( Rgba c ) noexcept {
			return D2D1::ColorF( static_cast<float>( ( c >> 24 ) & 0xFF ) / 255.0f, static_cast<float>( ( c >> 16 ) & 0xFF ) / 255.0f, static_cast<float>( ( c >> 8 ) & 0xFF ) / 255.0f, static_cast<float>( c & 0xFF ) / 255.0f );
		}



		/**
		 * @struct Block
		 * @brief A laid out piece of text and its measured size (device pixels).
		 */
		struct Block {
			ComPtr<IDWriteTextLayout> Layout;

			float Width = 0.0f; ///< Width of the widest line (rounded up).
			float Height = 0.0f; ///< Total height (rounded up).
			float Left = 0.0f; ///< Offset of the text inside the layout box (alignment).
		};



		/**
		 * @brief Returns @p name when the family is installed, "Segoe UI" otherwise.
		 *
		 * DirectWrite would silently pick some default for an unknown family, which
		 * makes a wrong font name hard to notice. Falling back explicitly keeps the
		 * result predictable.
		 */
		std::wstring resolveFamily( IDWriteFactory *factory, const std::string &name ) {
			std::wstring wide = utf8_to_wstring( name );

			if ( wide.empty() )
				return L"Segoe UI";

			ComPtr<IDWriteFontCollection> collection;

			if ( SUCCEEDED( factory->GetSystemFontCollection( collection.GetAddressOf(), FALSE ) ) && collection ) {
				UINT32 index = 0;
				BOOL exists = FALSE;

				if ( SUCCEEDED( collection->FindFamilyName( wide.c_str(), &index, &exists ) ) && exists )
					return wide;
			}

			return L"Segoe UI";
		}



		/**
		 * @brief Builds and measures a text block.
		 *
		 * @param limit    Maximum block width. kUnbounded means no limit.
		 * @param wrap     Wrap lines at @p limit. When false the text is one line per
		 *                 explicit newline and is trimmed with an ellipsis if @p ellipsis.
		 * @param ellipsis Trim overflowing text with "…" (single line blocks).
		 */
		bool buildBlock( IDWriteFactory *factory, Block &block, const std::wstring &text, const std::wstring &family, float sizePx, std::uint8_t style, float limit, bool wrap, Align align, bool ellipsis ) {

			ComPtr<IDWriteTextFormat> format;

			// clang-format off
	HRESULT hr = factory->CreateTextFormat(
		family.c_str(),
		nullptr,
		( style & FontFlags::Bold )   ? DWRITE_FONT_WEIGHT_BOLD  : DWRITE_FONT_WEIGHT_NORMAL,
		( style & FontFlags::Italic ) ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL,
		( std::max )( 1.0f, sizePx ),
		TextEngine::locale(),
		format.GetAddressOf()
	);
			// clang-format on

			if ( FAILED( hr ) )
				return false;

			format->SetWordWrapping( wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP );
			format->SetParagraphAlignment( DWRITE_PARAGRAPH_ALIGNMENT_NEAR );

			switch ( align ) {
			case Align::Center:
				format->SetTextAlignment( DWRITE_TEXT_ALIGNMENT_CENTER );
				break;
			case Align::Right:
				format->SetTextAlignment( DWRITE_TEXT_ALIGNMENT_TRAILING );
				break;
			default:
				format->SetTextAlignment( DWRITE_TEXT_ALIGNMENT_LEADING );
				break;
			}

			// clang-format off
	hr = factory->CreateTextLayout(
		text.c_str(),
		static_cast<UINT32>( text.size() ),
		format.Get(),
		wrap ? limit : kUnbounded,
		kUnbounded,
		block.Layout.GetAddressOf()
	);
			// clang-format on

			if ( FAILED( hr ) )
				return false;

			const DWRITE_TEXT_RANGE all{ 0, static_cast<UINT32>( text.size() ) };

			if ( style & FontFlags::Underline )
				block.Layout->SetUnderline( TRUE, all );

			if ( style & FontFlags::Strikeout )
				block.Layout->SetStrikethrough( TRUE, all );

			if ( ellipsis ) {
				ComPtr<IDWriteInlineObject> sign;

				if ( SUCCEEDED( factory->CreateEllipsisTrimmingSign( block.Layout.Get(), sign.GetAddressOf() ) ) ) {
					const DWRITE_TRIMMING trimming{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
					block.Layout->SetTrimming( &trimming, sign.Get() );
				}
			}

			DWRITE_TEXT_METRICS metrics{};

			if ( FAILED( block.Layout->GetMetrics( &metrics ) ) )
				return false;

			// +1 keeps the last glyph from being clipped by rounding.
			const float width = ( std::min )( std::ceil( metrics.width ) + 1.0f, limit );

			if ( !wrap ) {
				// Single line blocks: shrink the layout box to the text (or to the limit, which
				// engages the ellipsis) so the alignment of explicit lines is relative to it.
				block.Layout->SetMaxWidth( width );

				if ( FAILED( block.Layout->GetMetrics( &metrics ) ) )
					return false;
			}

			block.Width = width;
			block.Height = std::ceil( metrics.height );
			block.Left = metrics.left;

			return true;
		}



		/**
		 * @brief Pre-scales a GDI+ bitmap with high quality filtering and uploads it as a Direct2D bitmap.
		 */
		ComPtr<ID2D1Bitmap> uploadBitmap( ID2D1RenderTarget *target, Gdiplus::Bitmap *source, int width, int height ) {
			ComPtr<ID2D1Bitmap> result;

			if ( !source || width < 1 || height < 1 )
				return result;

			const int srcW = static_cast<int>( source->GetWidth() );
			const int srcH = static_cast<int>( source->GetHeight() );

			if ( srcW < 1 || srcH < 1 )
				return result;

			Gdiplus::Bitmap scaled( width, height, PixelFormat32bppPARGB );

			if ( scaled.GetLastStatus() != Gdiplus::Ok )
				return result;

			{
				Gdiplus::Graphics g( &scaled );
				g.SetCompositingMode( Gdiplus::CompositingModeSourceCopy );
				g.SetInterpolationMode( Gdiplus::InterpolationModeHighQualityBicubic );
				g.SetPixelOffsetMode( Gdiplus::PixelOffsetModeHalf );

				Gdiplus::ImageAttributes attributes;
				attributes.SetWrapMode( Gdiplus::WrapModeTileFlipXY );

				// clang-format off
		g.DrawImage(
			source,
			Gdiplus::Rect( 0, 0, width, height ),
			0, 0, srcW, srcH,
			Gdiplus::UnitPixel,
			&attributes
		);
				// clang-format on
			}

			Gdiplus::BitmapData data{};
			const Gdiplus::Rect whole( 0, 0, width, height );

			if ( scaled.LockBits( &whole, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &data ) != Gdiplus::Ok )
				return result;

			// clang-format off
	const D2D1_BITMAP_PROPERTIES properties = D2D1::BitmapProperties(
		D2D1::PixelFormat( DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED ),
		96.0f,
		96.0f
	);

	target->CreateBitmap(
		D2D1::SizeU( static_cast<UINT32>( width ), static_cast<UINT32>( height ) ),
		data.Scan0,
		static_cast<UINT32>( data.Stride ),
		properties,
		result.GetAddressOf()
	);
			// clang-format on

			scaled.UnlockBits( &data );

			return result;
		}



		/// @brief Start / end points of a gradient line for a rectangle, matching GDI+ unscaled angles.
		void gradientLine( double angleDegrees, float w, float h, D2D1_POINT_2F &start, D2D1_POINT_2F &end ) {
			const double a = angleDegrees * kPi / 180.0;
			const double dx = std::cos( a );
			const double dy = std::sin( a );
			const double half = 0.5 * ( std::fabs( w * dx ) + std::fabs( h * dy ) );
			const double cx = w / 2.0;
			const double cy = h / 2.0;

			start = D2D1::Point2F( static_cast<float>( cx - dx * half ), static_cast<float>( cy - dy * half ) );
			end = D2D1::Point2F( static_cast<float>( cx + dx * half ), static_cast<float>( cy + dy * half ) );
		}



		ComPtr<ID2D1Brush> makeFillBrush( ID2D1RenderTarget *target, const Fill &fill, float w, float h ) {
			ComPtr<ID2D1Brush> brush;

			if ( fill.Gradient ) {
				const D2D1_GRADIENT_STOP stops[2] = { { 0.0f, toColor( fill.From ) }, { 1.0f, toColor( fill.To ) } };

				ComPtr<ID2D1GradientStopCollection> collection;

				if ( SUCCEEDED( target->CreateGradientStopCollection( stops, 2, collection.GetAddressOf() ) ) ) {
					D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES properties{};
					gradientLine( fill.Angle, w, h, properties.startPoint, properties.endPoint );

					ComPtr<ID2D1LinearGradientBrush> gradient;

					if ( SUCCEEDED( target->CreateLinearGradientBrush( properties, collection.Get(), gradient.GetAddressOf() ) ) ) {
						gradient.As( &brush );
						return brush;
					}
				}
			}

			ComPtr<ID2D1SolidColorBrush> solid;

			if ( SUCCEEDED( target->CreateSolidColorBrush( toColor( fill.From ), solid.GetAddressOf() ) ) )
				solid.As( &brush );

			return brush;
		}



		bool renderImpl( Surface &surface, const RenderRequest &rq ) {
			IDWriteFactory *factory = TextEngine::dwrite();

			if ( !factory )
				return false;

			const Options &o = rq.Opts;
			const double s = rq.Scale > 0.0 ? rq.Scale : 1.0;

			// ---- metrics in device pixels --------------------------------------
			const float ml = static_cast<float>( o.Padding.Left * s );
			const float mt = static_cast<float>( o.Padding.Top * s );
			const float mr = static_cast<float>( o.Padding.Right * s );
			const float mb = static_cast<float>( o.Padding.Bottom * s );
			const float borderW = static_cast<float>( o.BorderWidth * s );
			const float blockGap = static_cast<float>( kBlockGap * s );
			const int iconPx = ( std::max )( 1, static_cast<int>( std::lround( o.IconSize * s ) ) );
			const float iconGap = static_cast<float>( o.IconGap * s );

			const bool hasTitle = !o.Title.empty();
			const bool hasIcon = hasTitle && rq.Icon != nullptr;
			const bool hasImage = rq.Image != nullptr;
			const bool hasText = !rq.Text.empty();
			const bool wraps = o.MaxWidth > 0.0;

			const float limit = wraps ? ( std::max )( 1.0f, static_cast<float>( o.MaxWidth * s ) - ml - mr ) : kUnbounded;

			// ---- text blocks ---------------------------------------------------
			const std::wstring textFamily = resolveFamily( factory, o.Font );
			const std::wstring titleFamily = o.TitleFont.empty() ? textFamily : resolveFamily( factory, o.TitleFont );

			const float textPx = static_cast<float>( o.FontSize * s * kPointToPixel );
			const double titleSize = o.TitleFontSize > 0.0 ? o.TitleFontSize : o.FontSize + 1.0;
			const float titlePx = static_cast<float>( titleSize * s * kPointToPixel );

			Block title;
			Block body;

			float rowW = 0.0f;
			float rowH = 0.0f;

			if ( hasTitle ) {
				const float avail = hasIcon ? ( std::max )( 1.0f, limit - static_cast<float>( iconPx ) - iconGap ) : limit;

				// clang-format off
		if ( !buildBlock( factory, title, utf8_to_wstring( o.Title ), titleFamily, titlePx,
		                  o.TitleFontStyle, avail, false, Align::Left, true ) )
			return false;
				// clang-format on

				rowW = title.Width + ( hasIcon ? static_cast<float>( iconPx ) + iconGap : 0.0f );
				rowH = ( std::max )( title.Height, hasIcon ? static_cast<float>( iconPx ) : 0.0f );
			}

			int imageW = 0;
			int imageH = 0;

			if ( hasImage ) {
				const float srcW = static_cast<float>( rq.Image->GetWidth() );
				const float srcH = static_cast<float>( rq.Image->GetHeight() );

				if ( srcW < 1.0f || srcH < 1.0f )
					return false;

				float w = 0.0f;
				float h = 0.0f;

				if ( o.ImageWidth > 0.0 && o.ImageHeight > 0.0 ) {
					w = static_cast<float>( o.ImageWidth * s );
					h = static_cast<float>( o.ImageHeight * s );
				} else if ( o.ImageWidth > 0.0 ) {
					w = static_cast<float>( o.ImageWidth * s );
					h = w * srcH / srcW;
				} else if ( o.ImageHeight > 0.0 ) {
					h = static_cast<float>( o.ImageHeight * s );
					w = h * srcW / srcH;
				} else {
					w = static_cast<float>( srcW * s );
					h = static_cast<float>( srcH * s );
				}

				if ( w > limit ) {
					h *= limit / w;
					w = limit;
				}

				imageW = ( std::max )( 1, static_cast<int>( std::lround( w ) ) );
				imageH = ( std::max )( 1, static_cast<int>( std::lround( h ) ) );
			}

			if ( hasText ) {
				// clang-format off
		if ( !buildBlock( factory, body, utf8_to_wstring( rq.Text ), textFamily, textPx,
		                  o.FontStyle, limit, wraps, o.Alignment, false ) )
			return false;
				// clang-format on
			}

			const int blocks = ( hasTitle ? 1 : 0 ) + ( hasImage ? 1 : 0 ) + ( hasText ? 1 : 0 );

			const float contentW = ( std::max )( { rowW, static_cast<float>( imageW ), body.Width } );
			float contentH = rowH + static_cast<float>( imageH ) + body.Height;

			if ( blocks > 1 )
				contentH += blockGap * static_cast<float>( blocks - 1 );

			const int width = ( std::max )( 1, static_cast<int>( std::ceil( contentW + ml + mr ) ) );
			const int height = ( std::max )( 1, static_cast<int>( std::ceil( contentH + mt + mb ) ) );

			if ( width > kMaxSurface || height > kMaxSurface )
				return false;

			// ---- corner radius -------------------------------------------------
			// With a backdrop the system clips the window itself, so the drawn shape must
			// match the system corner (8 = round / default, 4 = small, 0 = none).
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

			// ---- draw pass -----------------------------------------------------
			if ( !surface.resize( width, height ) )
				return false;

			surface.clear();

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

			auto draw = [&]() -> HRESULT {
				ID2D1DCRenderTarget *rt = TextEngine::target();

				if ( !rt )
					return E_FAIL;

				const RECT bounds{ 0, 0, width, height };
				HRESULT hr = rt->BindDC( surface.Dc, &bounds );

				if ( FAILED( hr ) )
					return hr;

				rt->BeginDraw();
				rt->SetTransform( D2D1::Matrix3x2F::Identity() );
				rt->SetAntialiasMode( D2D1_ANTIALIAS_MODE_PER_PRIMITIVE );
				rt->SetTextAntialiasMode( D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE );
				rt->Clear( D2D1::ColorF( 0.0f, 0.0f, 0.0f, 0.0f ) );

				// background + border (the border is centered on the same path as the fill)
				{
					const D2D1_RECT_F rect = D2D1::RectF( borderW / 2.0f, borderW / 2.0f, static_cast<float>( width ) - borderW / 2.0f, static_cast<float>( height ) - borderW / 2.0f );

					const float r = ( std::min )( static_cast<float>( radius * s ), ( std::min )( rect.right - rect.left, rect.bottom - rect.top ) / 2.0f );

					const D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect( rect, r, r );

					ComPtr<ID2D1Brush> fill = makeFillBrush( rt, o.Background, static_cast<float>( width ), static_cast<float>( height ) );

					if ( fill )
						rt->FillRoundedRectangle( rounded, fill.Get() );

					if ( borderW > 0.0f && ( o.BorderColor & 0xFF ) != 0 ) {
						ComPtr<ID2D1SolidColorBrush> border;

						if ( SUCCEEDED( rt->CreateSolidColorBrush( toColor( o.BorderColor ), border.GetAddressOf() ) ) )
							rt->DrawRoundedRectangle( rounded, border.Get(), borderW );
					}
				}

				float y = mt;

				// title row
				if ( hasTitle ) {
					float x = ml + alignX( rowW );

					if ( hasIcon ) {
						ComPtr<ID2D1Bitmap> icon = uploadBitmap( rt, rq.Icon, iconPx, iconPx );

						if ( icon ) {
							const float top = std::round( y + ( rowH - static_cast<float>( iconPx ) ) / 2.0f );
							const float left = std::round( x );

							// clang-format off
					rt->DrawBitmap(
						icon.Get(),
						D2D1::RectF( left, top, left + static_cast<float>( iconPx ), top + static_cast<float>( iconPx ) ),
						1.0f,
						D2D1_BITMAP_INTERPOLATION_MODE_LINEAR
					);
							// clang-format on
						}

						x += static_cast<float>( iconPx ) + iconGap;
					}

					ComPtr<ID2D1SolidColorBrush> brush;

					if ( SUCCEEDED( rt->CreateSolidColorBrush( toColor( o.TitleColor.value_or( o.TextColor ) ), brush.GetAddressOf() ) ) ) {
						// clang-format off
				rt->DrawTextLayout(
					D2D1::Point2F( x - title.Left, y + ( rowH - title.Height ) / 2.0f ),
					title.Layout.Get(),
					brush.Get(),
					kColorFont
				);
						// clang-format on
					}

					y += rowH + blockGap;
				}

				// image
				if ( hasImage ) {
					ComPtr<ID2D1Bitmap> image = uploadBitmap( rt, rq.Image, imageW, imageH );

					if ( image ) {
						const float left = std::round( ml + alignX( static_cast<float>( imageW ) ) );
						const float top = std::round( y );

						// clang-format off
				rt->DrawBitmap(
					image.Get(),
					D2D1::RectF( left, top, left + static_cast<float>( imageW ), top + static_cast<float>( imageH ) ),
					static_cast<float>( o.ImageOpacity ) / 255.0f,
					D2D1_BITMAP_INTERPOLATION_MODE_LINEAR
				);
						// clang-format on
					}

					y += static_cast<float>( imageH ) + blockGap;
				}

				// text
				if ( hasText ) {
					ComPtr<ID2D1SolidColorBrush> brush;

					if ( SUCCEEDED( rt->CreateSolidColorBrush( toColor( o.TextColor ), brush.GetAddressOf() ) ) ) {
						// clang-format off
				rt->DrawTextLayout(
					D2D1::Point2F( ml + alignX( body.Width ) - body.Left, y ),
					body.Layout.Get(),
					brush.Get(),
					kColorFont
				);
						// clang-format on
					}
				}

				return rt->EndDraw();
			};

			HRESULT hr = draw();

			if ( hr == D2DERR_RECREATE_TARGET ) {
				TextEngine::resetTarget();
				hr = draw();
			}

			return SUCCEEDED( hr );
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
