/**
 * @file tooltip_window.cpp
 * @brief Layered tooltip window, fade animation and monitor helpers implementation.
 * @license GPL v2.0 License
 */

#include "tooltip_window.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#pragma comment( lib, "advapi32.lib" )

namespace tooltip {

	namespace {

		constexpr const wchar_t *kClassName = L"RainJIT.Tooltip";
		constexpr UINT_PTR kTimeoutTimer = 1;
		constexpr UINT_PTR kFadeTimer = 2;
		constexpr UINT kFadeInterval = 15; // ms (the system timer resolution is ~15.6 ms)
		constexpr std::uint32_t kMaxFade = 60000; // ms



		/**
		 * @brief Window procedure of the tooltip window.
		 *
		 * Refuses activation and forwards the timers to the owning Window.
		 */
		LRESULT CALLBACK windowProc( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam ) {
			switch ( msg ) {
			case WM_MOUSEACTIVATE:
				return MA_NOACTIVATE;

			case WM_TIMER: {
				auto *self = reinterpret_cast<Window *>( GetWindowLongPtrW( hwnd, GWLP_USERDATA ) );

				if ( self && self->Handle == hwnd )
					self->onTimer( static_cast<UINT_PTR>( wParam ) );

				return 0;
			}

			default:
				break;
			}

			return DefWindowProcW( hwnd, msg, wParam, lParam );
		}



		using GetDpiForMonitorFn = HRESULT( WINAPI * )( HMONITOR, int, UINT *, UINT * );



		/// @brief Resolves shcore!GetDpiForMonitor lazily (not available before Windows 8.1).
		GetDpiForMonitorFn resolveGetDpiForMonitor() noexcept {
			static const GetDpiForMonitorFn fn = []() -> GetDpiForMonitorFn {
				HMODULE shcore = LoadLibraryW( L"shcore.dll" );

				if ( !shcore )
					return nullptr;

				return reinterpret_cast<GetDpiForMonitorFn>( GetProcAddress( shcore, "GetDpiForMonitor" ) );
			}();

			return fn;
		}



		// ---- system backdrop (resolved at runtime, so the module builds with any SDK) ----

		// DWM attribute ids (dwmapi.h, Windows 11 SDK)
		constexpr DWORD kDwmUseImmersiveDarkMode = 20;
		constexpr DWORD kDwmWindowCornerPref = 33;
		constexpr DWORD kDwmBorderColor = 34;
		constexpr DWORD kDwmSystemBackdropType = 38;

		constexpr DWORD kDwmColorDefault = 0xFFFFFFFF;
		constexpr DWORD kDwmColorNone = 0xFFFFFFFE;

		// SetWindowCompositionAttribute ids and accent states (undocumented, stable since Windows 10)
		constexpr DWORD kWcaForceActiveWindowAppearance = 15;
		constexpr DWORD kWcaAccentPolicy = 19;
		constexpr DWORD kAccentDisabled = 0;
		constexpr DWORD kAccentAcrylicBlurBehind = 4;

		constexpr DWORD kBuildAcrylicAccent = 17134; // Windows 10 1803
		constexpr DWORD kBuildWindows11 = 22000;
		constexpr DWORD kBuildSystemBackdrop = 22621; // Windows 11 22H2

		/// @brief Same layout as MARGINS.
		struct DwmMargins {
			int Left;
			int Right;
			int Top;
			int Bottom;
		};

		/// @brief Same layout as ACCENT_POLICY.
		struct AccentPolicy {
			DWORD State;
			DWORD Flags;
			DWORD GradientColor; // 0xAABBGGRR
			DWORD AnimationId;
		};

		/// @brief Same layout as WINDOWCOMPOSITIONATTRIBDATA.
		struct CompositionAttribData {
			DWORD Attrib;
			PVOID Data;
			SIZE_T Size;
		};

		using DwmSetWindowAttributeFn = HRESULT( WINAPI * )( HWND, DWORD, LPCVOID, DWORD );
		using DwmExtendFrameIntoClientAreaFn = HRESULT( WINAPI * )( HWND, const DwmMargins * );
		using SetWindowCompositionAttributeFn = BOOL( WINAPI * )( HWND, CompositionAttribData * );



		HMODULE dwmModule() noexcept {
			static const HMODULE module = LoadLibraryW( L"dwmapi.dll" );
			return module;
		}



		DwmSetWindowAttributeFn resolveDwmSetWindowAttribute() noexcept {
			static const DwmSetWindowAttributeFn fn = []() -> DwmSetWindowAttributeFn {
				HMODULE dwm = dwmModule();
				return dwm ? reinterpret_cast<DwmSetWindowAttributeFn>( GetProcAddress( dwm, "DwmSetWindowAttribute" ) ) : nullptr;
			}();

			return fn;
		}



		DwmExtendFrameIntoClientAreaFn resolveDwmExtendFrame() noexcept {
			static const DwmExtendFrameIntoClientAreaFn fn = []() -> DwmExtendFrameIntoClientAreaFn {
				HMODULE dwm = dwmModule();
				return dwm ? reinterpret_cast<DwmExtendFrameIntoClientAreaFn>( GetProcAddress( dwm, "DwmExtendFrameIntoClientArea" ) ) : nullptr;
			}();

			return fn;
		}



		SetWindowCompositionAttributeFn resolveSetWindowCompositionAttribute() noexcept {
			static const SetWindowCompositionAttributeFn fn = []() -> SetWindowCompositionAttributeFn {
				HMODULE user32 = GetModuleHandleW( L"user32.dll" );
				return user32 ? reinterpret_cast<SetWindowCompositionAttributeFn>( GetProcAddress( user32, "SetWindowCompositionAttribute" ) ) : nullptr;
			}();

			return fn;
		}



		/// @brief Windows build number via RtlGetVersion (not affected by manifest shims).
		DWORD windowsBuild() noexcept {
			static const DWORD build = []() -> DWORD {
				using RtlGetVersionFn = LONG( WINAPI * )( OSVERSIONINFOW * );

				HMODULE ntdll = GetModuleHandleW( L"ntdll.dll" );
				auto fn = ntdll ? reinterpret_cast<RtlGetVersionFn>( GetProcAddress( ntdll, "RtlGetVersion" ) ) : nullptr;

				if ( !fn )
					return 0;

				OSVERSIONINFOW info{};
				info.dwOSVersionInfoSize = sizeof( info );

				return fn( &info ) == 0 ? info.dwBuildNumber : 0;
			}();

			return build;
		}



		/// @brief True when apps are using the dark theme (HKCU Personalize\AppsUseLightTheme == 0).
		bool systemUsesDarkTheme() noexcept {
			DWORD value = 1;
			DWORD size = sizeof( value );

			// clang-format off
	const LSTATUS status = RegGetValueW(
		HKEY_CURRENT_USER,
		L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
		L"AppsUseLightTheme",
		RRF_RT_REG_DWORD,
		nullptr,
		&value,
		&size
	);
			// clang-format on

			return status == ERROR_SUCCESS && value == 0;
		}

	} // namespace



	MonitorInfo MonitorInfo::at( POINT point ) noexcept {
		MonitorInfo info;

		HMONITOR monitor = MonitorFromPoint( point, MONITOR_DEFAULTTONEAREST );

		MONITORINFO mi{};
		mi.cbSize = sizeof( mi );

		if ( monitor && GetMonitorInfoW( monitor, &mi ) )
			info.Work = mi.rcWork;
		else
			SystemParametersInfoW( SPI_GETWORKAREA, 0, &info.Work, 0 );

		if ( auto fn = resolveGetDpiForMonitor(); fn && monitor ) {
			UINT dpiX = 96;
			UINT dpiY = 96;

			// 0 = MDT_EFFECTIVE_DPI
			if ( SUCCEEDED( fn( monitor, 0, &dpiX, &dpiY ) ) && dpiX > 0 )
				info.Dpi = dpiX;
		}

		return info;
	}



	Window::~Window() {
		destroy();
	}



	HINSTANCE Window::module() noexcept {
		HMODULE handle = nullptr;

		// clang-format off
	GetModuleHandleExW(
		GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>( &windowProc ),
		&handle
	);
		// clang-format on

		return handle;
	}



	bool Window::registerClass() noexcept {
		WNDCLASSEXW wc{};
		wc.cbSize = sizeof( wc );
		wc.lpfnWndProc = windowProc;
		wc.hInstance = module();
		wc.hCursor = LoadCursorW( nullptr, IDC_ARROW );
		wc.lpszClassName = kClassName;

		if ( RegisterClassExW( &wc ) )
			return true;

		return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
	}



	void Window::unregisterClass() noexcept {
		UnregisterClassW( kClassName, module() );
	}



	bool Window::create( bool topmost, bool clickThrough ) noexcept {
		Topmost = topmost;

		DWORD exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;

		if ( topmost )
			exStyle |= WS_EX_TOPMOST;

		if ( clickThrough )
			exStyle |= WS_EX_TRANSPARENT;

		// clang-format off
	Handle = CreateWindowExW(
		exStyle,
		kClassName,
		L"",
		WS_POPUP,
		0, 0, 1, 1,
		nullptr,
		nullptr,
		module(),
		nullptr
	);
		// clang-format on

		if ( !Handle )
			return false;

		SetWindowLongPtrW( Handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( this ) );

		Current = 0;
		Fading = false;
		Then = After::None;

		return true;
	}



	void Window::destroy() noexcept {
		if ( !Handle )
			return;

		HWND handle = Handle;

		// Detach first: the window procedure must not reach this object while the
		// window is being torn down. The window timers die with the window.
		Handle = nullptr;
		Fading = false;
		Then = After::None;

		SetWindowLongPtrW( handle, GWLP_USERDATA, 0 );
		DestroyWindow( handle );
	}



	void Window::finish() noexcept {
		const GoneFn fn = OnGone;
		void *context = Context;

		destroy();

		if ( fn )
			fn( context );
	}



	bool Window::present( const Surface &surface, int x, int y ) noexcept {
		if ( !Handle || !surface.Dc )
			return false;

		POINT destination{ x, y };
		POINT source{ 0, 0 };
		SIZE size{ surface.Width, surface.Height };

		BLENDFUNCTION blend{};
		blend.BlendOp = AC_SRC_OVER;
		blend.SourceConstantAlpha = Current;
		blend.AlphaFormat = AC_SRC_ALPHA;

		// clang-format off
	return UpdateLayeredWindow(
		Handle,
		nullptr,
		&destination,
		&size,
		surface.Dc,
		&source,
		0,
		&blend,
		ULW_ALPHA
	) != FALSE;
		// clang-format on
	}



	void Window::moveTo( int x, int y ) noexcept {
		if ( !Handle )
			return;

		SetWindowPos( Handle, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
	}



	void Window::applyAlpha( std::uint8_t alpha ) noexcept {
		Current = alpha;

		if ( !Handle )
			return;

		BLENDFUNCTION blend{};
		blend.BlendOp = AC_SRC_OVER;
		blend.SourceConstantAlpha = alpha;
		blend.AlphaFormat = AC_SRC_ALPHA;

		// Only the blend function changes: no source DC, no bitmap upload.
		UpdateLayeredWindow( Handle, nullptr, nullptr, nullptr, nullptr, nullptr, 0, &blend, ULW_ALPHA );
	}



	void Window::stopFade() noexcept {
		if ( Fading ) {
			Fading = false;

			if ( Handle )
				KillTimer( Handle, kFadeTimer );
		}
	}



	void Window::fadeTo( std::uint8_t target, std::uint32_t ms, After then ) noexcept {
		if ( !Handle )
			return;

		ms = ( std::min )( ms, kMaxFade );

		if ( ms == 0 || target == Current ) {
			stopFade();
			applyAlpha( target );

			Then = then;
			complete();
			return;
		}

		// A partial fade (e.g. interrupted halfway) keeps the same speed as a full one.
		const std::uint32_t span = ( std::max )( static_cast<std::uint32_t>( Level ), 1u );
		const std::uint32_t distance = static_cast<std::uint32_t>( std::abs( static_cast<int>( target ) - static_cast<int>( Current ) ) );

		FadeFrom = Current;
		FadeTo = target;
		FadeMs = ( std::max )( 1u, ( std::min )( ms, ms * distance / span ) );
		FadeStart = GetTickCount64();
		Then = then;
		Fading = true;

		SetTimer( Handle, kFadeTimer, kFadeInterval, nullptr );
	}



	void Window::tick() noexcept {
		if ( !Fading || !Handle ) {
			stopFade();
			return;
		}

		const ULONGLONG elapsed = GetTickCount64() - FadeStart;

		if ( elapsed >= FadeMs ) {
			stopFade();
			applyAlpha( FadeTo );
			complete();
			return;
		}

		// smoothstep easing
		double t = static_cast<double>( elapsed ) / static_cast<double>( FadeMs );
		t = t * t * ( 3.0 - 2.0 * t );

		const int value = static_cast<int>( FadeFrom ) + static_cast<int>( std::lround( ( static_cast<double>( FadeTo ) - static_cast<double>( FadeFrom ) ) * t ) );

		applyAlpha( static_cast<std::uint8_t>( ( std::clamp )( value, 0, 255 ) ) );
	}



	void Window::complete() noexcept {
		const After then = Then;
		Then = After::None;

		switch ( then ) {
		case After::Hide:
			if ( Handle ) {
				KillTimer( Handle, kTimeoutTimer );
				ShowWindow( Handle, SW_HIDE );
			}
			break;

		case After::Destroy:
			finish();
			break;

		default:
			break;
		}
	}



	void Window::onTimer( UINT_PTR id ) noexcept {
		if ( id == kFadeTimer ) {
			tick();
			return;
		}

		if ( id == kTimeoutTimer ) {
			KillTimer( Handle, kTimeoutTimer );
			hide( AutoFadeMs );
		}
	}



	void Window::show( std::uint32_t fadeMs, bool raiseIt ) noexcept {
		if ( !Handle )
			return;

		// Showing cancels a pending hide / destroy.
		Then = After::None;

		if ( !visible() ) {
			stopFade();
			applyAlpha( fadeMs == 0 ? Level : 0 );

			ShowWindow( Handle, SW_SHOWNOACTIVATE );
			raise();

			if ( fadeMs != 0 )
				fadeTo( Level, fadeMs, After::None );

			return;
		}

		// Already visible: fade back in when it was fading out (or is not at full level).
		if ( ( Fading && FadeTo != Level ) || ( !Fading && Current != Level ) ) {
			fadeTo( Level, fadeMs, After::None );

			if ( raiseIt )
				raise();

			return;
		}

		if ( raiseIt )
			raise();
	}



	void Window::hide( std::uint32_t fadeMs ) noexcept {
		if ( !Handle )
			return;

		KillTimer( Handle, kTimeoutTimer );

		if ( !visible() ) {
			stopFade();
			Then = After::None;
			return;
		}

		if ( fadeMs == 0 ) {
			stopFade();
			Then = After::None;
			ShowWindow( Handle, SW_HIDE );
			return;
		}

		fadeTo( 0, fadeMs, After::Hide );
	}



	void Window::close( std::uint32_t fadeMs ) noexcept {
		if ( !Handle )
			return;

		KillTimer( Handle, kTimeoutTimer );

		if ( fadeMs == 0 || !visible() ) {
			finish();
			return;
		}

		fadeTo( 0, fadeMs, After::Destroy );
	}



	void Window::raise() noexcept {
		if ( !Handle || !Topmost )
			return;

		SetWindowPos( Handle, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE );
	}



	bool Window::visible() const noexcept {
		return Handle && IsWindowVisible( Handle ) != FALSE;
	}



	void Window::setTopmost( bool topmost ) noexcept {
		Topmost = topmost;

		if ( !Handle )
			return;

		// clang-format off
	SetWindowPos(
		Handle,
		topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
		0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
	);
		// clang-format on
	}



	void Window::setClickThrough( bool clickThrough ) noexcept {
		if ( !Handle )
			return;

		LONG_PTR style = GetWindowLongPtrW( Handle, GWL_EXSTYLE );

		if ( clickThrough )
			style |= WS_EX_TRANSPARENT;
		else
			style &= ~static_cast<LONG_PTR>( WS_EX_TRANSPARENT );

		SetWindowLongPtrW( Handle, GWL_EXSTYLE, style );
	}



	void Window::setLevel( std::uint8_t level ) noexcept {
		Level = level;

		if ( !Handle || !visible() )
			return;

		if ( Fading ) {
			// Fading in: retarget. Fading out: the level applies on the next show.
			if ( FadeTo != 0 )
				FadeTo = level;

			return;
		}

		if ( Current != level )
			applyAlpha( level );
	}



	void Window::setTimeout( std::uint32_t milliseconds ) noexcept {
		if ( !Handle )
			return;

		if ( milliseconds == 0 )
			KillTimer( Handle, kTimeoutTimer );
		else
			SetTimer( Handle, kTimeoutTimer, milliseconds, nullptr );
	}



	BackdropResult Window::setBackdrop( Backdrop effect, Corner corner, std::optional<bool> dark, std::optional<Rgba> tint ) noexcept {
		if ( !Handle )
			return BackdropResult::Failed;

		const bool acrylic = effect == Backdrop::Acrylic;
		const bool mica = effect == Backdrop::Mica || effect == Backdrop::MicaAlt;
		const bool active = acrylic || mica;
		const DWORD build = windowsBuild();

		// Nothing could have been applied on systems older than the accent policy.
		if ( !active && build < kBuildAcrylicAccent )
			return BackdropResult::Ok;

		if ( acrylic && build < kBuildAcrylicAccent )
			return BackdropResult::Unsupported;

		if ( mica && build < kBuildSystemBackdrop )
			return BackdropResult::Unsupported;

		const bool useDark = dark.value_or( systemUsesDarkTheme() );

		const auto setComposition = resolveSetWindowCompositionAttribute();

		if ( acrylic && !setComposition )
			return BackdropResult::Unsupported;

		bool ok = true;

		// ---- accent policy: acrylic on, anything else off ----------------------
		if ( setComposition ) {
			AccentPolicy policy{};

			if ( acrylic ) {
				// Rgba 0xRRGGBBAA -> 0xAABBGGRR. Acrylic does not accept a tint with alpha 0.
				const Rgba t = tint.value_or( useDark ? 0x00000001u : 0xFFFFFF01u );
				const DWORD r = ( t >> 24 ) & 0xFF;
				const DWORD g = ( t >> 16 ) & 0xFF;
				const DWORD b = ( t >> 8 ) & 0xFF;
				const DWORD a = ( std::max<DWORD> )( 1, static_cast<DWORD>( t & 0xFF ) );

				policy.State = kAccentAcrylicBlurBehind;
				policy.Flags = 0;
				policy.GradientColor = ( a << 24 ) | ( b << 16 ) | ( g << 8 ) | r;
			} else
				policy.State = kAccentDisabled;

			CompositionAttribData data{ kWcaAccentPolicy, &policy, sizeof( policy ) };
			const BOOL result = setComposition( Handle, &data );

			if ( acrylic )
				ok = result != FALSE;
		}

		// ---- DWM attributes (Windows 11) ---------------------------------------
		if ( build >= kBuildWindows11 ) {
			if ( const auto setAttribute = resolveDwmSetWindowAttribute() ) {
				// DWM_WINDOW_CORNER_PREFERENCE: 0 default, 1 do not round, 2 round, 3 round small
				DWORD cornerPreference = 0;

				if ( active ) {
					switch ( corner ) {
					case Corner::None:
						cornerPreference = 1;
						break;
					case Corner::Round:
						cornerPreference = 2;
						break;
					case Corner::Small:
						cornerPreference = 3;
						break;
					default:
						cornerPreference = 0;
						break;
					}
				}

				// While a backdrop is active DWM draws no border: the tooltip border has priority.
				const DWORD border = active ? kDwmColorNone : kDwmColorDefault;
				const BOOL dwmDark = useDark ? TRUE : FALSE;

				setAttribute( Handle, kDwmUseImmersiveDarkMode, &dwmDark, sizeof( dwmDark ) );
				setAttribute( Handle, kDwmWindowCornerPref, &cornerPreference, sizeof( cornerPreference ) );
				setAttribute( Handle, kDwmBorderColor, &border, sizeof( border ) );

				if ( build >= kBuildSystemBackdrop ) {
					// DWM_SYSTEMBACKDROP_TYPE: 1 none, 2 mica, 4 mica alt (tabbed).
					// Acrylic goes through the accent policy, so DWM must draw nothing.
					DWORD type = 1;

					if ( effect == Backdrop::Mica )
						type = 2;
					else if ( effect == Backdrop::MicaAlt )
						type = 4;

					const HRESULT result = setAttribute( Handle, kDwmSystemBackdropType, &type, sizeof( type ) );

					if ( mica )
						ok = SUCCEEDED( result );

					// Mica is rendered in the "frame" area, which must cover the whole window.
					if ( const auto extend = resolveDwmExtendFrame() ) {
						const DwmMargins margins = mica ? DwmMargins{ -1, -1, -1, -1 } : DwmMargins{ 0, 0, 0, 0 };
						extend( Handle, &margins );
					}

					// Mica is drawn only for the active window: keep the active look.
					if ( setComposition ) {
						BOOL force = mica ? TRUE : FALSE;
						CompositionAttribData data{ kWcaForceActiveWindowAppearance, &force, sizeof( force ) };
						setComposition( Handle, &data );
					}
				}
			}
		}

		return ok ? BackdropResult::Ok : BackdropResult::Failed;
	}

} // namespace tooltip
