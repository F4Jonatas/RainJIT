/**
 * @file tooltip_window.cpp
 * @brief Layered tooltip window and monitor helpers implementation.
 * @license GPL v2.0 License
 */

#include "tooltip_window.hpp"

#pragma comment( lib, "advapi32.lib" )

namespace tooltip {

	namespace {

		constexpr const wchar_t *kClassName = L"RainJIT.Tooltip";
		constexpr UINT_PTR kTimerId = 1;



		/**
		 * @brief Window procedure of the tooltip window.
		 *
		 * Refuses activation and hides the window when the auto-hide timer fires.
		 */
		LRESULT CALLBACK windowProc( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam ) {
			switch ( msg ) {
			case WM_MOUSEACTIVATE:
				return MA_NOACTIVATE;

			case WM_TIMER:
				if ( wParam == kTimerId ) {
					KillTimer( hwnd, kTimerId );
					ShowWindow( hwnd, SW_HIDE );
					return 0;
				}
				break;

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

		// ---- DWM (resolved at runtime, so the module builds with any SDK) ---------

		// Attribute ids (dwmapi.h, Windows 11 SDK)
		constexpr DWORD kDwmUseImmersiveDarkMode = 20;
		constexpr DWORD kDwmWindowCornerPref = 33;
		constexpr DWORD kDwmBorderColor = 34;
		constexpr DWORD kDwmSystemBackdropType = 38;

		constexpr DWORD kDwmColorDefault = 0xFFFFFFFF;
		constexpr DWORD kDwmColorNone = 0xFFFFFFFE;

		constexpr DWORD kBuildWindows11 = 22000;
		constexpr DWORD kBuildSystemBackdrop = 22621;

		/// @brief Same layout as MARGINS.
		struct DwmMargins {
			int Left;
			int Right;
			int Top;
			int Bottom;
		};

		using DwmSetWindowAttributeFn = HRESULT( WINAPI * )( HWND, DWORD, LPCVOID, DWORD );
		using DwmExtendFrameIntoClientAreaFn = HRESULT( WINAPI * )( HWND, const DwmMargins * );



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

		return Handle != nullptr;
	}



	void Window::destroy() noexcept {
		if ( !Handle )
			return;

		DestroyWindow( Handle );
		Handle = nullptr;
	}



	bool Window::present( const Surface &surface, int x, int y, std::uint8_t opacity ) noexcept {
		if ( !Handle || !surface.Dc )
			return false;

		POINT destination{ x, y };
		POINT source{ 0, 0 };
		SIZE size{ surface.Width, surface.Height };

		BLENDFUNCTION blend{};
		blend.BlendOp = AC_SRC_OVER;
		blend.SourceConstantAlpha = opacity;
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



	void Window::show() noexcept {
		if ( !Handle )
			return;

		ShowWindow( Handle, SW_SHOWNOACTIVATE );
		raise();
	}



	void Window::hide() noexcept {
		if ( !Handle )
			return;

		KillTimer( Handle, kTimerId );
		ShowWindow( Handle, SW_HIDE );
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



	void Window::setTimeout( std::uint32_t milliseconds ) noexcept {
		if ( !Handle )
			return;

		if ( milliseconds == 0 )
			KillTimer( Handle, kTimerId );
		else
			SetTimer( Handle, kTimerId, milliseconds, nullptr );
	}



	BackdropResult Window::setBackdrop( Backdrop effect, Corner corner, std::optional<bool> dark ) noexcept {
		if ( !Handle )
			return BackdropResult::Failed;

		const bool active = effect != Backdrop::None;
		const DWORD build = windowsBuild();

		// Nothing to reset on systems that never had these attributes.
		if ( !active && build < kBuildWindows11 )
			return BackdropResult::Ok;

		if ( build < kBuildSystemBackdrop )
			return BackdropResult::Unsupported;

		const auto setAttribute = resolveDwmSetWindowAttribute();

		if ( !setAttribute )
			return BackdropResult::Unsupported;

		// DWM_SYSTEMBACKDROP_TYPE: 1 none, 2 mica, 3 acrylic (transient), 4 mica alt (tabbed)
		DWORD type = 1;

		switch ( effect ) {
		case Backdrop::Mica:
			type = 2;
			break;
		case Backdrop::Acrylic:
			type = 3;
			break;
		case Backdrop::MicaAlt:
			type = 4;
			break;
		default:
			break;
		}

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
		const BOOL useDark = dark.value_or( systemUsesDarkTheme() ) ? TRUE : FALSE;

		setAttribute( Handle, kDwmUseImmersiveDarkMode, &useDark, sizeof( useDark ) );
		setAttribute( Handle, kDwmWindowCornerPref, &cornerPreference, sizeof( cornerPreference ) );
		setAttribute( Handle, kDwmBorderColor, &border, sizeof( border ) );

		const HRESULT result = setAttribute( Handle, kDwmSystemBackdropType, &type, sizeof( type ) );

		// The material is rendered in the "frame" area, which must cover the whole window.
		if ( const auto extend = resolveDwmExtendFrame() ) {
			const DwmMargins margins = active ? DwmMargins{ -1, -1, -1, -1 } : DwmMargins{ 0, 0, 0, 0 };
			extend( Handle, &margins );
		}

		return SUCCEEDED( result ) ? BackdropResult::Ok : BackdropResult::Failed;
	}

} // namespace tooltip
