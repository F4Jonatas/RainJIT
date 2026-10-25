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
 * @file trident_security.cpp
 * @brief Process hardening, VBScript engine block, IInternetSecurityManager and drop filter.
 * @license GPL-2.0-or-later (GNU General Public License v2.0 or later)
 *
 * Part of the Trident module; see trident_internal.hpp for the overall layout.
 */

#include "trident_internal.hpp"

namespace trident {

	// -------------------------------------------------------------------------
	// VBScript: engine-level block (unconditional)
	//
	// The sanitizer, the URL validator, the file scan and the DOM tripwire cover everything
	// Trident sees. None of them can stop a page that builds a VBScript <script> at run time
	// (document.write, createElement, execScript( code, "VBScript" )) or a remote page loaded
	// with sanitize = false. The engine itself is therefore made unavailable: class objects that
	// refuse to create an instance are registered, in this process, for the CLSIDs of the
	// VBScript and VBScript.Encode engines. In-process activation consults classes registered
	// with CoRegisterClassObject before the registry, so MSHTML cannot instantiate the engine and
	// any VBScript block fails to run. (verificar: test with sanitize = false and
	// <script language="vbscript">MsgBox "x"</script>; no message box must appear.)
	// -------------------------------------------------------------------------
	namespace {
	struct DeniedClassFactory : IClassFactory {
		STDMETHODIMP QueryInterface( REFIID riid, void **ppv ) override {
			if ( !ppv )
				return E_POINTER;
			if ( riid == IID_IUnknown || riid == IID_IClassFactory ) {
				*ppv = static_cast<IClassFactory *>( this );
				return S_OK; // static lifetime: no reference counting
			}
			*ppv = nullptr;
			return E_NOINTERFACE;
		}
		STDMETHODIMP_( ULONG ) AddRef() override {
			return 2;
		}
		STDMETHODIMP_( ULONG ) Release() override {
			return 1;
		}
		STDMETHODIMP CreateInstance( IUnknown *, REFIID, void **ppv ) override {
			if ( ppv )
				*ppv = nullptr;
			return E_ACCESSDENIED;
		}
		STDMETHODIMP LockServer( BOOL ) override {
			return S_OK;
		}
	};
	} // namespace

	static DeniedClassFactory g_deniedFactory;

	static DWORD g_vbsCookies[2] = { 0, 0 };

	/// Registers the refusing class objects. Idempotent; must run on the STA thread after OleInitialize.
	void BlockVbScriptEngine() {
		static const CLSID kClsids[2] = {
			{ 0xB54F3741, 0x5B07, 0x11CF, { 0xA4, 0xB0, 0x00, 0xAA, 0x00, 0x4A, 0x55, 0xE8 } }, // VBScript
			{ 0xB54F3743, 0x5B07, 0x11CF, { 0xA4, 0xB0, 0x00, 0xAA, 0x00, 0x4A, 0x55, 0xE8 } }, // VBScript.Encode
		};
		for ( int i = 0; i < 2; ++i ) {
			if ( g_vbsCookies[i] )
				continue;
			CoRegisterClassObject( kClsids[i], &g_deniedFactory, CLSCTX_INPROC_SERVER, REGCLS_MULTIPLEUSE, &g_vbsCookies[i] );
		}
	}

	void UnblockVbScriptEngine() {
		for ( DWORD &c : g_vbsCookies ) {
			if ( c ) {
				CoRevokeClassObject( c );
				c = 0;
			}
		}
	}

	/**
	 * @brief Item 8: process-wide Internet Feature Controls that harden the embedded engine.
	 *
	 * The MSHTML engine runs inside the Rainmeter process and shares WinINet state (cookies, cache,
	 * credentials) with Internet Explorer; it cannot be sandboxed from here. These switches shrink
	 * what a page can do with that access. They apply to every browser control in the process and
	 * are best-effort (failures are ignored). Must run before the first control is created.
	 */
	void ApplyProcessHardening() {
		static bool done = false;
		if ( done )
			return;
		done = true;
		static const INTERNETFEATURELIST kFeatures[] = {
			FEATURE_RESTRICT_ACTIVEXINSTALL, // no ActiveX install prompts
			FEATURE_RESTRICT_FILEDOWNLOAD, // downloads only after a user gesture
			FEATURE_ZONE_ELEVATION, // no navigation to a more privileged zone
			FEATURE_MIME_SNIFFING, // do not promote content types by sniffing
			FEATURE_DISABLE_MK_PROTOCOL, // mk: protocol
			FEATURE_HTTP_USERNAME_PASSWORD_DISABLE, // user:pass@host URLs
			FEATURE_UNC_SAVEDFILECHECK, // zone check for saved files on UNC paths
			FEATURE_SAFE_BINDTOOBJECT, // safe-for-initialization check on <object>
			FEATURE_VALIDATE_NAVIGATE_URL, // malformed navigation URLs are rejected
			FEATURE_WINDOW_RESTRICTIONS, // popups cannot position/resize off-screen or fake chrome
		};
		for ( INTERNETFEATURELIST f : kFeatures )
			CoInternetSetFeatureEnabled( f, SET_FEATURE_ON_PROCESS, TRUE );
	}

	// -------------------------------------------------------------------------
	// SkinSecurityManager - IInternetSecurityManager
	//
	// Lets MSHTML load local resources referenced with relative paths from
	// file:// documents inside the skin folder.
	//
	// Item 4: it used to grant URLPOLICY_ALLOW for EVERY action on skin URLs
	// (ActiveX, WScript.Shell, cross-domain, shell execute...) and compared the URL
	// with a raw prefix (no normalization of "..", "%2e%2e", no trailing slash).
	// Now:
	//   - isSkinUrl uses the same strict parser as IsOriginTrusted (FileUrlToFoldedPath)
	//     and requires the trailing slash of the folder ("skin" does not match "skin-evil");
	//   - dangerous actions (ActiveX, shell, downloads, Java) are DENIED for every URL,
	//     unless create{ allowActiveX = true };
	//   - only a small allowlist of benign actions is granted for skin URLs;
	//   - everything else falls back to the default behavior (INET_E_DEFAULT_ACTION).
	// -------------------------------------------------------------------------
	struct SkinSecurityManager : IInternetSecurityManager {
		LONG m_refCount;
		std::wstring m_skinPath; ///< Folded skin folder (FoldPath), '/' separators and a trailing slash. Empty = no URL belongs to the skin.
		bool m_allowActiveX; ///< true = do not deny ActiveX/shell/download/Java actions.
		bool m_allowCredentials; ///< false = silent NTLM/Kerberos/Basic logon disabled (anonymous requests only).
		bool m_allowCookies; ///< false = cookies denied; third-party cookies are always denied.

		SkinSecurityManager( const std::wstring &skinPath, bool allowSkin, bool allowActiveX, bool allowCredentials, bool allowCookies ) :
			m_refCount( 1 ),
			m_allowActiveX( allowActiveX ),
			m_allowCredentials( allowCredentials ),
			m_allowCookies( allowCookies ) {
			if ( allowSkin && !skinPath.empty() ) {
				m_skinPath = FoldPath( skinPath );
				if ( m_skinPath.back() != L'/' )
					m_skinPath += L'/';
			}
		}

		/// Actions that are always denied (urlmon.h). Numeric values, so the code does not depend on the SDK version.
		static bool IsDeniedAction( DWORD a ) {
			switch ( a ) {
			case 0x1001: // URLACTION_DOWNLOAD_SIGNED_ACTIVEX
			case 0x1004: // URLACTION_DOWNLOAD_UNSIGNED_ACTIVEX
			case 0x1200: // URLACTION_ACTIVEX_RUN
			case 0x1201: // URLACTION_ACTIVEX_OVERRIDE_OBJECT_SAFETY
			case 0x1202: // URLACTION_ACTIVEX_OVERRIDE_DATA_SAFETY
			case 0x1203: // URLACTION_ACTIVEX_OVERRIDE_SCRIPT_SAFETY
			case 0x1204: // URLACTION_ACTIVEX_CONFIRM_NOOBJECTSAFETY
			case 0x1205: // URLACTION_ACTIVEX_TREATASUNTRUSTED
			case 0x1206: // URLACTION_ACTIVEX_NO_WEBOC_SCRIPT
			case 0x1207: // URLACTION_ACTIVEX_OVERRIDE_REPURPOSEDETECTION
			case 0x1208: // URLACTION_ACTIVEX_OVERRIDE_OPTIN
			case 0x1209: // URLACTION_ACTIVEX_SCRIPTLET_RUN
			case 0x1401: // URLACTION_SCRIPT_OVERRIDE_SAFETY
			case 0x1800: // URLACTION_SHELL_INSTALL_DTITEMS
			case 0x1802: // URLACTION_SHELL_MOVE_OR_COPY
			case 0x1803: // URLACTION_SHELL_FILE_DOWNLOAD
			case 0x1806: // URLACTION_SHELL_EXECUTE_HIGHRISK
			case 0x1807: // URLACTION_SHELL_EXECUTE_MODRISK
			case 0x1808: // URLACTION_SHELL_EXECUTE_LOWRISK
			case 0x1C00: // URLACTION_JAVA_PERMISSIONS
				return true;
			default:
				return false;
			}
		}

		/// Benign actions granted ONLY to skin URLs (everything else: default behavior).
		static bool IsSkinAllowedAction( DWORD a ) {
			switch ( a ) {
			case 0x1400: // URLACTION_SCRIPT_RUN - o JS da propria skin
			case 0x1406: // URLACTION_CROSS_DOMAIN_DATA - read skin files
			case 0x1601: // URLACTION_HTML_SUBMIT_FORMS
			case 0x1602: // URLACTION_HTML_SUBMIT_FORMS_FROM
			case 0x1603: // URLACTION_HTML_SUBMIT_FORMS_TO
			case 0x1604: // URLACTION_HTML_FONT_DOWNLOAD
			case 0x1606: // URLACTION_HTML_USERDATA_SAVE
			case 0x1607: // URLACTION_HTML_SUBFRAME_NAVIGATE
				return true;
			default:
				return false;
			}
		}

		// IUnknown
		STDMETHODIMP QueryInterface( REFIID riid, void **ppv ) override {
			if ( !ppv )
				return E_POINTER;
			if ( riid == IID_IUnknown || riid == IID_IInternetSecurityManager ) {
				*ppv = static_cast<IInternetSecurityManager *>( this );
				AddRef();
				return S_OK;
			}
			*ppv = nullptr;
			return E_NOINTERFACE;
		}
		STDMETHODIMP_( ULONG ) AddRef() override {
			return InterlockedIncrement( &m_refCount );
		}
		STDMETHODIMP_( ULONG ) Release() override {
			LONG ref = InterlockedDecrement( &m_refCount );
			if ( ref == 0 )
				delete this;
			return ref;
		}

		/// True if the URL is a file:// inside the skin folder.
		bool isSkinUrl( LPCWSTR url ) {
			if ( !url || m_skinPath.empty() )
				return false;
			// Strict parser: decodes %XX, treats '\' as '/', rejects "..", control characters and UNC paths.
			std::wstring path = FileUrlToFoldedPath( wstring_to_utf8( url ) );
			return !path.empty() && path.compare( 0, m_skinPath.size(), m_skinPath ) == 0;
		}

		// Returns URLZONE_LOCAL_MACHINE for skin resources - allows them to load.
		STDMETHODIMP MapUrlToZone( LPCWSTR pwszUrl, DWORD *pdwZone, DWORD ) override {
			if ( !pdwZone )
				return E_POINTER;
			if ( isSkinUrl( pwszUrl ) ) {
				*pdwZone = URLZONE_LOCAL_MACHINE;
				return S_OK;
			}
			return INET_E_DEFAULT_ACTION;
		}

		// Denies dangerous actions for every URL; grants only the allowlist for skin URLs.
		STDMETHODIMP ProcessUrlAction( LPCWSTR pwszUrl, DWORD dwAction, BYTE *pPolicy, DWORD cbPolicy, BYTE *, DWORD, DWORD, DWORD ) override {
			if ( !m_allowActiveX && IsDeniedAction( dwAction ) ) {
				if ( pPolicy && cbPolicy >= sizeof( DWORD ) )
					*reinterpret_cast<DWORD *>( pPolicy ) = URLPOLICY_DISALLOW;
				return S_FALSE;
			}
			// Item 8: the control shares WinINet with Internet Explorer.
			//  - credentials: no silent NTLM/Kerberos logon in the Intranet zone (URLACTION_CREDENTIALS_USE);
			//  - cookies: denied unless allowCookies; third-party cookies are always denied.
			if ( dwAction == 0x1A00 /* URLACTION_CREDENTIALS_USE */ && !m_allowCredentials ) {
				if ( pPolicy && cbPolicy >= sizeof( DWORD ) )
					*reinterpret_cast<DWORD *>( pPolicy ) = 0x00030000; // URLPOLICY_CREDENTIALS_ANONYMOUS_ONLY
				return S_OK;
			}
			if ( dwAction == 0x1A04 /* URLACTION_CLIENT_CERT_PROMPT */ || dwAction == 0x1A05 /* URLACTION_COOKIES_THIRD_PARTY */ || dwAction == 0x1A06 /* URLACTION_COOKIES_SESSION_THIRD_PARTY */ ) {
				if ( pPolicy && cbPolicy >= sizeof( DWORD ) )
					*reinterpret_cast<DWORD *>( pPolicy ) = URLPOLICY_DISALLOW;
				return S_FALSE;
			}
			if ( !m_allowCookies && ( dwAction == 0x1A02 /* URLACTION_COOKIES */ || dwAction == 0x1A03 /* URLACTION_COOKIES_SESSION */ || dwAction == 0x1A10 /* URLACTION_COOKIES_ENABLED */ ) ) {
				if ( pPolicy && cbPolicy >= sizeof( DWORD ) )
					*reinterpret_cast<DWORD *>( pPolicy ) = URLPOLICY_DISALLOW;
				return S_FALSE;
			}
			if ( IsSkinAllowedAction( dwAction ) && isSkinUrl( pwszUrl ) && pPolicy && cbPolicy >= sizeof( DWORD ) ) {
				*reinterpret_cast<DWORD *>( pPolicy ) = URLPOLICY_ALLOW;
				return S_OK;
			}
			return INET_E_DEFAULT_ACTION;
		}

		// Remaining interface methods - defer to the default behavior.
		STDMETHODIMP SetSecuritySite( IInternetSecurityMgrSite * ) override {
			return INET_E_DEFAULT_ACTION;
		}
		STDMETHODIMP GetSecuritySite( IInternetSecurityMgrSite ** ) override {
			return INET_E_DEFAULT_ACTION;
		}
		STDMETHODIMP GetSecurityId( LPCWSTR, BYTE *, DWORD *, DWORD_PTR ) override {
			return INET_E_DEFAULT_ACTION;
		}
		STDMETHODIMP QueryCustomPolicy( LPCWSTR, REFGUID, BYTE **, DWORD *, BYTE *, DWORD, DWORD ) override {
			return INET_E_DEFAULT_ACTION;
		}
		STDMETHODIMP SetZoneMapping( DWORD, LPCWSTR, DWORD ) override {
			return INET_E_DEFAULT_ACTION;
		}
		STDMETHODIMP GetZoneMappings( DWORD, IEnumString **, DWORD ) override {
			return INET_E_DEFAULT_ACTION;
		}
	};

	// -------------------------------------------------------------------------
	// FilteringDropTarget (item 7)
	//
	// GetDropTarget used to return E_NOTIMPL, so MSHTML used its default drop target:
	// dropping a file (or link) on the control NAVIGATED to it (file://, UNC, a local
	// .html with scripts). This wrapper forwards everything that is safe (e.g. text
	// dragged into an <input>) to MSHTML's default drop target, but refuses data that
	// carries files (CF_HDROP, FileGroupDescriptor) or URLs.
	// -------------------------------------------------------------------------
	struct FilteringDropTarget : IDropTarget {
		LONG m_refCount;
		IDropTarget *m_inner; ///< MSHTML's default drop target (AddRef'd), may be nullptr.
		bool m_blocked; ///< True while the current drag carries files/URLs.

		explicit FilteringDropTarget( IDropTarget *inner ) :
			m_refCount( 1 ),
			m_inner( inner ),
			m_blocked( false ) {
			if ( m_inner )
				m_inner->AddRef();
		}
		~FilteringDropTarget() {
			if ( m_inner )
				m_inner->Release();
		}

		STDMETHODIMP QueryInterface( REFIID riid, void **ppv ) override {
			if ( !ppv )
				return E_POINTER;
			if ( riid == IID_IUnknown || riid == IID_IDropTarget ) {
				*ppv = static_cast<IDropTarget *>( this );
				AddRef();
				return S_OK;
			}
			*ppv = nullptr;
			return E_NOINTERFACE;
		}
		STDMETHODIMP_( ULONG ) AddRef() override {
			return InterlockedIncrement( &m_refCount );
		}
		STDMETHODIMP_( ULONG ) Release() override {
			LONG ref = InterlockedDecrement( &m_refCount );
			if ( ref == 0 )
				delete this;
			return ref;
		}

		/// True if the data object carries files or URLs.
		static bool CarriesFilesOrUrls( IDataObject *data ) {
			if ( !data )
				return true; // fail closed
			static const UINT kFormats[] = {
				CF_HDROP,
				RegisterClipboardFormatW( L"FileGroupDescriptorW" ),
				RegisterClipboardFormatW( L"FileGroupDescriptor" ),
				RegisterClipboardFormatW( L"UniformResourceLocatorW" ),
				RegisterClipboardFormatW( L"UniformResourceLocator" ),
				RegisterClipboardFormatW( L"Shell IDList Array" ),
			};
			for ( UINT cf : kFormats ) {
				if ( !cf )
					continue;
				FORMATETC fe = { (CLIPFORMAT)cf, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL | TYMED_ISTREAM | TYMED_ISTORAGE };
				if ( data->QueryGetData( &fe ) == S_OK )
					return true;
			}
			return false;
		}

		STDMETHODIMP DragEnter( IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect ) override {
			m_blocked = CarriesFilesOrUrls( pDataObj );
			if ( m_blocked || !m_inner ) {
				if ( pdwEffect )
					*pdwEffect = DROPEFFECT_NONE;
				return S_OK;
			}
			return m_inner->DragEnter( pDataObj, grfKeyState, pt, pdwEffect );
		}
		STDMETHODIMP DragOver( DWORD grfKeyState, POINTL pt, DWORD *pdwEffect ) override {
			if ( m_blocked || !m_inner ) {
				if ( pdwEffect )
					*pdwEffect = DROPEFFECT_NONE;
				return S_OK;
			}
			return m_inner->DragOver( grfKeyState, pt, pdwEffect );
		}
		STDMETHODIMP DragLeave() override {
			bool wasBlocked = m_blocked;
			m_blocked = false;
			return ( wasBlocked || !m_inner ) ? S_OK : m_inner->DragLeave();
		}
		STDMETHODIMP Drop( IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect ) override {
			// Re-check at drop time: never trust state from DragEnter alone.
			bool blocked = m_blocked || CarriesFilesOrUrls( pDataObj );
			m_blocked = false;
			if ( blocked || !m_inner ) {
				if ( pdwEffect )
					*pdwEffect = DROPEFFECT_NONE;
				return S_OK;
			}
			return m_inner->Drop( pDataObj, grfKeyState, pt, pdwEffect );
		}
	};

	// -------------------------------------------------------------------------
	// Factories used by the other modules (the classes stay private to this file)
	// -------------------------------------------------------------------------

	IInternetSecurityManager *CreateSkinSecurityManager( const std::wstring &skinPath, bool allowSkin, bool allowActiveX, bool allowCredentials, bool allowCookies ) {
		return new SkinSecurityManager( skinPath, allowSkin, allowActiveX, allowCredentials, allowCookies );
	}

	IDropTarget *CreateFilteringDropTarget( IDropTarget *inner ) {
		return new FilteringDropTarget( inner );
	}


} // namespace trident
