/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */


/**
 * @file trident_window.cpp
 * @brief Popup window: geometry, rounded corners, subclassing and creation of the WebBrowser control.
 * @license GPL-2.0-or-later (GNU General Public License v2.0 or later)
 *
 * Part of the Trident module; see trident_internal.hpp for the overall layout.
 */

#include "trident_internal.hpp"

namespace trident {

	/**
	 * @brief HELPER - Applies a rounded rectangle region to the specified window.
	 */
	static void ApplyRoundedCorners( HWND hWnd, int width, int height, int radius ) {
		if ( radius <= 0 || width <= 0 || height <= 0 )
			return;

		HRGN hRgn = CreateRoundRectRgn( 0, 0, width, height, radius, radius );
		SetWindowRgn( hWnd, hRgn, TRUE );
	}

	/**
	 * @brief HELPER - Calculates the final screen rectangle for the browser popup.
	 */
	static void GetConstrainedScreenRect( Control *ctrl, const RECT &parentRect, int &outX, int &outY, int &outW, int &outH ) {
		int desiredX = parentRect.left + ctrl->left;
		int desiredY = parentRect.top + ctrl->top;
		int desiredW = ctrl->width;
		int desiredH = ctrl->height;

		if ( ctrl->insideSkin ) {
			int skinLeft = parentRect.left;
			int skinTop = parentRect.top;
			int skinRight = parentRect.right;
			int skinBottom = parentRect.bottom;

			if ( desiredX < skinLeft )
				desiredX = skinLeft;
			if ( desiredY < skinTop )
				desiredY = skinTop;

			int maxW = skinRight - desiredX;
			int maxH = skinBottom - desiredY;

			if ( desiredW > maxW )
				desiredW = ( maxW > 0 ) ? maxW : 1;
			if ( desiredH > maxH )
				desiredH = ( maxH > 0 ) ? maxH : 1;
		}

		int paddedX = desiredX + ctrl->padLeft;
		int paddedY = desiredY + ctrl->padTop;
		int paddedW = desiredW - ctrl->padWidth;
		int paddedH = desiredH - ctrl->padHeight;

		if ( paddedW < 1 )
			paddedW = 1;
		if ( paddedH < 1 )
			paddedH = 1;

		outX = paddedX;
		outY = paddedY;
		outW = paddedW;
		outH = paddedH;
	}

	/**
	 * @brief Keeps the popup directly above the skin window in the z-order.
	 *
	 * Uses the skin as the reference instead of an absolute position (HWND_TOP), so it works whatever the
	 * skin's z-position is (Normal, On Desktop, Topmost, ...) and whenever Rainmeter changes it:
	 *  1. the topmost flag of the popup is aligned with the skin's;
	 *  2. the popup is inserted right after the window that precedes the skin (Windows places a window
	 *     BEHIND the window passed as hWndInsertAfter), or at the top when nothing precedes the skin.
	 *
	 * Nothing is done when the popup is already the window directly above the skin.
	 */
	void SyncControlZOrder( Control *ctrl ) {
		if ( !ctrl || !ctrl->hwndControl || !IsWindow( ctrl->hwndControl ) )
			return;
		if ( !ctrl->hwndParent || !IsWindow( ctrl->hwndParent ) )
			return;

		const HWND popup = ctrl->hwndControl;
		const HWND skin = ctrl->hwndParent;
		const UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE;

		const bool skinTopmost = ( GetWindowLongPtr( skin, GWL_EXSTYLE ) & WS_EX_TOPMOST ) != 0;
		const bool popupTopmost = ( GetWindowLongPtr( popup, GWL_EXSTYLE ) & WS_EX_TOPMOST ) != 0;
		if ( skinTopmost != popupTopmost )
			SetWindowPos( popup, skinTopmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, flags );

		HWND above = GetWindow( skin, GW_HWNDPREV );
		if ( above == popup )
			return; // already directly above the skin

		SetWindowPos( popup, above ? above : HWND_TOP, 0, 0, 0, 0, flags );
	}

	/**
	 * @brief Brings the skin to the front and keeps the popup glued above it, without activating either.
	 *
	 * Order matters to avoid a visible moment where the skin covers the browser: the popup goes to the top
	 * first and the skin is then placed directly BEHIND it.
	 */
	void BringSkinToFront( Control *ctrl ) {
		if ( !ctrl || !ctrl->hwndControl || !IsWindow( ctrl->hwndControl ) )
			return;
		if ( !ctrl->hwndParent || !IsWindow( ctrl->hwndParent ) )
			return;

		const HWND popup = ctrl->hwndControl;
		const HWND skin = ctrl->hwndParent;
		const UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE;

		const bool skinTopmost = ( GetWindowLongPtr( skin, GWL_EXSTYLE ) & WS_EX_TOPMOST ) != 0;
		const bool popupTopmost = ( GetWindowLongPtr( popup, GWL_EXSTYLE ) & WS_EX_TOPMOST ) != 0;
		if ( skinTopmost != popupTopmost )
			SetWindowPos( popup, skinTopmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, flags );

		SetWindowPos( popup, HWND_TOP, 0, 0, 0, 0, flags );
		SetWindowPos( skin, popup, 0, 0, 0, 0, flags );
	}

	/**
	 * @brief Moves and resizes the popup window based on current parent position.
	 */
	void UpdateControlPosition( Control *ctrl ) {
		if ( !ctrl || !ctrl->hwndControl || !IsWindow( ctrl->hwndControl ) )
			return;

		if ( !ctrl->hwndParent || !IsWindow( ctrl->hwndParent ) )
			return;

		RECT parentRect;
		GetWindowRect( ctrl->hwndParent, &parentRect );

		int screenX, screenY, width, height;
		GetConstrainedScreenRect( ctrl, parentRect, screenX, screenY, width, height );

		// The z-order is handled separately (SyncControlZOrder): HWND_TOP here put the popup above every
		// normal window whenever the skin moved.
		SetWindowPos( ctrl->hwndControl, nullptr, screenX, screenY, width, height, SWP_NOACTIVATE | SWP_NOZORDER );
		SyncControlZOrder( ctrl );

		if ( ctrl->cornerRadius > 0 )
			ApplyRoundedCorners( ctrl->hwndControl, width, height, ctrl->cornerRadius );
	}

	// -------------------------------------------------------------------------
	// Parent window subclass
	// -------------------------------------------------------------------------

	LRESULT CALLBACK ParentSubclassProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData ) {
		ParentSubclassData *data = (ParentSubclassData *)dwRefData;
		if ( !data || !data->ctrl )
			return DefSubclassProc( hWnd, uMsg, wParam, lParam );

		Control *ctrl = data->ctrl;

		if ( uMsg == FocusMessage() ) {
			// Posted by IEServerSubclassProc when the user left-clicks inside the browser.
			// Brings the skin window to the top without stealing activation from other apps; the popup
			// is placed above it in the same step, so the skin never covers the browser.
			BringSkinToFront( ctrl );
			return 0; // consumed: Rainmeter's own window procedure never sees it
		}

		switch ( uMsg ) {
		case WM_WINDOWPOSCHANGED:
		case WM_MOVE:
		case WM_SIZE: {
			// Let the skin finish its own handling first (Rainmeter may re-assert its z-position there),
			// then follow it: position, size and z-order (the popup stays directly above the skin).
			LRESULT result = DefSubclassProc( hWnd, uMsg, wParam, lParam );
			if ( !ctrl->hidden )
				UpdateControlPosition( ctrl );
			return result;
		}

		case WM_SHOWWINDOW:
			if ( ctrl->hwndControl && IsWindow( ctrl->hwndControl ) ) {
				if ( wParam ) {
					if ( ctrl->hidden )
						ShowWindow( ctrl->hwndControl, SW_HIDE );
					else {
						UpdateControlPosition( ctrl );
						ShowWindow( ctrl->hwndControl, SW_SHOW );
					}
				} else {
					ShowWindow( ctrl->hwndControl, SW_HIDE );
				}
			}
			break;

		case WM_ACTIVATE:
			if ( LOWORD( wParam ) != WA_INACTIVE ) {
				SyncControlZOrder( ctrl );
			}
			break;

		case WM_NCDESTROY:
			RemoveWindowSubclass( hWnd, ParentSubclassProc, uIdSubclass );
			// Fix #12: the Control still points at this data; null it so DoQuit()/Cleanup()
			// never delete it (or use data->hWndParent) a second time.
			if ( ctrl->subclassData == data )
				ctrl->subclassData = nullptr;
			delete data;
			break;
		}

		return DefSubclassProc( hWnd, uMsg, wParam, lParam );
	}

	// -------------------------------------------------------------------------
	// Internet Explorer_Server subclass
	// -------------------------------------------------------------------------

	LRESULT CALLBACK IEServerSubclassProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData ) {
		IEServerSubclassData *data = (IEServerSubclassData *)dwRefData;

		if ( data && data->ctrl && uMsg == WM_LBUTTONDOWN ) {
			HWND hwndParent = data->ctrl->hwndParent;
			if ( hwndParent && IsWindow( hwndParent ) )
				PostMessage( hwndParent, FocusMessage(), 0, 0 );
		}

		if ( uMsg == WM_NCDESTROY ) {
			RemoveWindowSubclass( hWnd, IEServerSubclassProc, uIdSubclass );
			// Fix #12: avoid the double delete in DoQuit()/Cleanup()/EnumAndSubclassIEServer().
			if ( data && data->ctrl && data->ctrl->ieServerSubclassData == data ) {
				data->ctrl->ieServerSubclassData = nullptr;
				if ( data->ctrl->hwndIEServer == hWnd )
					data->ctrl->hwndIEServer = nullptr;
			}
			delete data;
		}

		return DefSubclassProc( hWnd, uMsg, wParam, lParam );
	}

	/**
	 * @brief Subclass procedure for the browser popup window (hwndControl).
	 *
	 * Intercepts WM_DWMNCRENDERINGCHANGED to suppress the DWM drop shadow
	 * whenever an external caller (e.g. the glass Lua module) re-enables NC
	 * rendering.  wParam == TRUE means rendering was just enabled; we
	 * immediately re-apply DWMNCRP_DISABLED.  wParam == FALSE is our own
	 * suppression firing back - ignored to avoid an infinite loop.
	 */
	LRESULT CALLBACK ControlSubclassProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData ) {
		if ( uMsg == WM_DWMNCRENDERINGCHANGED && wParam == TRUE ) {
			DWMNCRENDERINGPOLICY policy = DWMNCRP_DISABLED;
			DwmSetWindowAttribute( hWnd, DWMWA_NCRENDERING_POLICY, &policy, sizeof( policy ) );
			return 0;
		}

		if ( uMsg == WM_NCDESTROY ) {
			RemoveWindowSubclass( hWnd, ControlSubclassProc, uIdSubclass );
			// Fix #12: avoid the double delete in DoQuit().
			ControlSubclassData *csd = (ControlSubclassData *)dwRefData;
			if ( csd && csd->ctrl && csd->ctrl->controlSubclassData == csd )
				csd->ctrl->controlSubclassData = nullptr;
			delete csd;
		}

		return DefSubclassProc( hWnd, uMsg, wParam, lParam );
	}

	static BOOL CALLBACK FindIEServerProc( HWND hWnd, LPARAM lParam ) {
		wchar_t className[64] = {};
		GetClassNameW( hWnd, className, 64 );
		if ( wcscmp( className, L"Internet Explorer_Server" ) == 0 ) {
			*reinterpret_cast<HWND *>( lParam ) = hWnd;
			return FALSE;
		}
		return TRUE;
	}

	void EnumAndSubclassIEServer( Control *ctrl ) {
		if ( !ctrl || !ctrl->hwndControl || !IsWindow( ctrl->hwndControl ) )
			return;

		// Remove previous subclass if window changed
		if ( ctrl->hwndIEServer && IsWindow( ctrl->hwndIEServer ) ) {
			RemoveWindowSubclass( ctrl->hwndIEServer, IEServerSubclassProc, (UINT_PTR)ctrl->configId );
			if ( ctrl->ieServerSubclassData ) {
				delete (IEServerSubclassData *)ctrl->ieServerSubclassData;
				ctrl->ieServerSubclassData = nullptr;
			}
		}
		ctrl->hwndIEServer = nullptr;

		HWND found = nullptr;
		EnumChildWindows( ctrl->hwndControl, FindIEServerProc, (LPARAM)&found );
		if ( !found ) return;

		ctrl->hwndIEServer = found;
		IEServerSubclassData *subData = new IEServerSubclassData{};
		subData->ctrl = ctrl;

		if ( !SetWindowSubclass( found, IEServerSubclassProc, (UINT_PTR)ctrl->configId, (DWORD_PTR)subData ) ) {
			delete subData;
			ctrl->ieServerSubclassData = nullptr;
		} else {
			ctrl->ieServerSubclassData = subData;
		}
	}

	// -------------------------------------------------------------------------
	// CreateWebBrowserControl
	// -------------------------------------------------------------------------

	/**
	 * @brief Creates the popup window hosting the Shell.Explorer control.
	 */
	bool CreateWebBrowserControl( Control &ctrl, Rain *rain, std::string &outError ) {
		if ( !IsWindow( ctrl.hwndParent ) ) {
			outError = "Invalid parent window handle";
			RN_LOG( rain, LOG_ERROR, L"[RainJIT:Trident] Invalid hwndParent." );
			return false;
		}

		// Item 6: the system atl.dll is still used (it registers the global "AtlAxWin" class that
		// the popup window needs), but it is now loaded ONLY from System32: the old
		// LoadLibraryW( L"atl.dll" ) searched the application/current directories (DLL planting).
		// LOAD_LIBRARY_SEARCH_SYSTEM32 needs Windows 7 + KB2533623 or later. The module is kept
		// loaded for the life of the process on purpose (the window class points into it), so the
		// handle is not leaked per call: it is loaded once.
		static bool atlInitTried = false;
		if ( !atlInitTried ) {
			HMODULE hAtl = LoadLibraryExW( L"atl.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32 );
			if ( hAtl ) {
				typedef BOOL( WINAPI * AtlAxWinInitProc )();
				auto pInit = (AtlAxWinInitProc)GetProcAddress( hAtl, "AtlAxWinInit" );
				if ( pInit )
					pInit();
				else
					AtlAxWinInit();
			} else {
				AtlAxWinInit(); // statically linked ATL as a fallback
			}
			atlInitTried = true;
		}

		RECT parentRect;
		GetWindowRect( ctrl.hwndParent, &parentRect );
		int screenX, screenY, finalW, finalH;
		GetConstrainedScreenRect( &ctrl, parentRect, screenX, screenY, finalW, finalH );

		DWORD dwStyle = WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS;
		DWORD dwExStyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;

		// The "AtlAxWin" class is now registered by the statically linked ATL (item 6), i.e. with
		// THIS module's HINSTANCE, unless it is a global class. Try this module first, then the
		// host exe (what the system atl.dll's global class used to accept).
		HMODULE hSelf = nullptr;
		GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>( &CreateWebBrowserControl ), &hSelf );

		HWND hwndPopup = nullptr;
		for ( HINSTANCE inst : { (HINSTANCE)hSelf, GetModuleHandleW( nullptr ) } ) {
			if ( !inst )
				continue;
			// clang-format off
			hwndPopup = CreateWindowExW(
				dwExStyle,
				L"AtlAxWin",
				L"Shell.Explorer",
				dwStyle,
				screenX, screenY, finalW, finalH,
				ctrl.hwndParent, // owner window - keeps browser above skin in z-order
				nullptr,
				inst,
				nullptr
			);
			// clang-format on
			if ( hwndPopup )
				break;
		}

		if ( !hwndPopup ) {
			DWORD err = GetLastError();
			wchar_t buf[128];
			swprintf_s( buf, L"[RainJIT:Trident] CreateWindowEx failed. GetLastError=%lu", err );
			outError = wstring_to_utf8( buf );
			RN_LOG( rain, LOG_ERROR, buf );
			return false;
		}

		ctrl.hwndControl = hwndPopup;

		// Remove border that AtlAxWin may add implicitly
		SetWindowLongW( hwndPopup, GWL_STYLE, GetWindowLongW( hwndPopup, GWL_STYLE ) & ~WS_BORDER );

		// Remove DWM drop shadow (applied automatically to WS_POPUP windows)
		{
			DWMNCRENDERINGPOLICY policy = DWMNCRP_DISABLED;
			DwmSetWindowAttribute( hwndPopup, DWMWA_NCRENDERING_POLICY, &policy, sizeof( policy ) );
		}

		// Subclass the popup to re-suppress shadow whenever an external caller
		// (e.g. the glass Lua module calling resetAll) re-enables NC rendering.
		{
			ControlSubclassData *ctrlSubData = new ControlSubclassData{ &ctrl };
			if ( SetWindowSubclass( hwndPopup, ControlSubclassProc, (UINT_PTR)ctrl.configId, (DWORD_PTR)ctrlSubData ) )
				ctrl.controlSubclassData = ctrlSubData;
			else {
				delete ctrlSubData;
				ctrl.controlSubclassData = nullptr;
			}
		}

		if ( ctrl.cornerRadius > 0 )
			ApplyRoundedCorners( hwndPopup, finalW, finalH, ctrl.cornerRadius );

		CComPtr<IUnknown> unk;
		HRESULT hr = AtlAxGetControl( hwndPopup, &unk );
		if ( FAILED( hr ) || !unk ) {
			DestroyWindow( hwndPopup );
			ctrl.hwndControl = nullptr;
			outError = "AtlAxGetControl failed";
			RN_LOG( rain, LOG_ERROR, L"[RainJIT:Trident] AtlAxGetControl failed." );
			return false;
		}

		hr = unk->QueryInterface( IID_IWebBrowser2, (void **)&ctrl.webBrowser );
		if ( FAILED( hr ) || !ctrl.webBrowser ) {
			DestroyWindow( hwndPopup );
			ctrl.hwndControl = nullptr;
			outError = "QueryInterface(IWebBrowser2) failed";
			RN_LOG( rain, LOG_ERROR, L"[RainJIT:Trident] QueryInterface(IWebBrowser2) failed." );
			return false;
		}

		if ( ctrl.transparent ) {
			LONG_PTR exStyle = GetWindowLongPtr( hwndPopup, GWL_EXSTYLE );
			SetWindowLongPtr( hwndPopup, GWL_EXSTYLE, exStyle | WS_EX_LAYERED );
			SetLayeredWindowAttributes( hwndPopup, ctrl.colorKey, 0, LWA_COLORKEY );
			SetWindowPos( hwndPopup, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED );
		}

		if ( ctrl.silent ) {
			CComVariant silent( true );
			ctrl.webBrowser->put_Silent( silent.boolVal );
		}

		ApplyAmbientHostUi( ctrl ); // item 21: border flags + context-menu policy before the first page loads

		ShowWindow( hwndPopup, ctrl.hidden ? SW_HIDE : SW_SHOW );
		UpdateWindow( hwndPopup );

		ParentSubclassData *subData = new ParentSubclassData{};
		subData->ctrl = &ctrl;
		subData->hWndParent = ctrl.hwndParent;

		UINT_PTR subclassId = (UINT_PTR)ctrl.configId;

		if ( !SetWindowSubclass( ctrl.hwndParent, ParentSubclassProc, subclassId, (DWORD_PTR)subData ) ) {
			delete subData;
			ctrl.subclassData = nullptr;
			RN_LOG( rain, LOG_WARNING, L"[RainJIT:Trident] SetWindowSubclass failed." );
		} else {
			ctrl.subclassData = subData;
			RN_LOG( rain, LOG_DEBUG, L"[RainJIT:Trident] Parent subclass installed." );
		}

		{
			RECT rc;
			GetWindowRect( hwndPopup, &rc );
			// clang-format off
			wchar_t buf[256];
			swprintf_s(
				buf,
				L"[RainJIT:Trident] Popup created. Rect=(%d,%d,%d,%d) insideSkin=%s padding=(%d,%d,%d,%d) radius=%d",
				rc.left, rc.top, rc.right, rc.bottom,
				ctrl.insideSkin ? L"YES" : L"NO",
				ctrl.padLeft, ctrl.padTop, ctrl.padWidth, ctrl.padHeight,
				ctrl.cornerRadius
			);
			// clang-format on
			RN_LOG( rain, LOG_DEBUG, buf );
		}

		return true;
	}


} // namespace trident
