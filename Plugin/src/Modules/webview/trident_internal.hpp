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
 * @file trident_internal.hpp
 * @brief Declarations shared by the modules that implement the Trident WebBrowser control.
 * @license GPL-2.0-or-later (GNU General Public License v2.0 or later)
 *
 * @details
 * The implementation is split by responsibility (one translation unit each):
 *
 * | File                 | Responsibility                                                          |
 * |----------------------|-------------------------------------------------------------------------|
 * | trident.cpp          | Module lifecycle: ProcessMessages, Cleanup, luaopen_trident, RegisterModule |
 * | trident_create.cpp   | `trident.create()`: shared state, locking phases, control creation      |
 * | trident_options.cpp  | Parsing of the `create{}` options table and of the initial URL         |
 * | trident_object.cpp   | Construction of the Lua browser object (methods, bind, document/window) |
 * | trident_methods.cpp  | Lua methods of the browser object (navigate, write, execScript, quit...) |
 * | trident_document.cpp | Document operations: write into about:blank, script evaluation          |
 * | trident_window.cpp   | Popup window geometry, subclassing, creation of the WebBrowser control  |
 * | trident_events.cpp   | DWebBrowserEvents2 sink, event queue, hidden dispatch window            |
 * | trident_site.cpp     | OLE client site (IDocHostUIHandler, IServiceProvider, IOleCommandTarget)|
 * | trident_external.cpp | `window.external` bridge (IDispatchEx)                                  |
 * | trident_security.cpp | Process hardening, VBScript engine block, security manager, drop filter |
 * | trident_policy.cpp   | Origin trust, navigation guard, VBScript scans                          |
 * | trident_url.cpp      | Location resolution (URL, host:port, paths)                             |
 * | trident_lifetime.cpp | Control teardown, deferred close and the re-entrancy guard              |
 * | trident_theme.cpp    | Theme and `data-*` attributes on the <html> element                     |
 * | trident_util.cpp     | Shared state, registry keys, small helpers                              |
 *
 * Everything lives in `namespace trident`. Functions declared here have external linkage; anything
 * not declared here is private to its translation unit.
 */

#pragma once

#include "trident.hpp"
#include "sanitize.hpp"
#include <Includes/rain.hpp>
#include <Includes/define.hpp>
#include <Utils/strings.hpp>
#include <Utils/filesystem.hpp>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <atlbase.h>
#include <atlhost.h>
#include <commctrl.h>
#include <docobj.h>
#include <exdisp.h>
#include <mshtmdid.h>
#include <mshtml.h>
#include <mshtmhst.h>
#include <dispex.h>
#include <urlmon.h>
#include <dwmapi.h>
#include <shellapi.h>

#pragma comment( lib, "dwmapi.lib" )
#pragma comment( lib, "urlmon.lib" )
#pragma comment( lib, "comctl32.lib" )
#pragma comment( lib, "shell32.lib" )

/// Message posted by EventSink::Invoke to the hidden window to trigger
/// immediate callback dispatch without waiting for Rainmeter's Update tick.
#define WM_TRIDENT_EVENT ( WM_APP + 2 )

/// Posted to the hidden window when a deferred close can run (see trident_lifetime.cpp).
#define WM_TRIDENT_FLUSH ( WM_APP + 4 )

#ifndef DISPID_NEWWINDOW3
	#define DISPID_NEWWINDOW3 273 ///< DWebBrowserEvents2::NewWindow3 (exdispid.h, Windows XP SP2+).
#endif

namespace trident {


	// -------------------------------------------------------------------------
	// window.external trust (fix #8) and event queue limits
	// -------------------------------------------------------------------------

	// Item 9: hard limits on everything a page can make us allocate. The old limits
	// (2048 events x 64 KB) allowed 128 MB of queued strings, and status/progress/navigate-error
	// events were not limited at all.
	static constexpr size_t kMaxEventQueue = 512; ///< Pending events per context; extra events are dropped.
	static constexpr size_t kMaxQueuedBytes = 2 * 1024 * 1024; ///< Total bytes of queued strings per context.
	static constexpr size_t kMaxEventText = 4096; ///< Max characters of title/URL/status text kept in an event.
	static constexpr size_t kMaxExternalName = 256; ///< Max characters of external.call() name.
	static constexpr size_t kMaxExternalData = 32 * 1024; ///< Max characters of external.call() data.
	static constexpr size_t kMaxExternalNames = 256; ///< Max names a page may create on window.external (fdexNameEnsure).
	static constexpr UINT kMaxExternalArgs = 32; ///< Max arguments a page may pass to a bound function.
	static constexpr size_t kMaxExceptionText = 512; ///< Max characters of a Lua error text handed back to the page.
	/// An event produced by our own document.open/write/close must not reach Lua (item 19: write() inside a
	/// documentcomplete callback would loop forever). The flag is only honoured briefly and is cleared by real navigations.
	static const ULONGLONG kWriteEventWindowMs = 2000;

	// -------------------------------------------------------------------------
	// Types shared between modules
	// -------------------------------------------------------------------------

	/// Subclass data of the skin window (ParentSubclassProc).
	struct ParentSubclassData {
		Control *ctrl; ///< Owning control.
		HWND hWndParent; ///< Handle to the parent window.
	};

	/// Subclass data of the Internet Explorer_Server window (IEServerSubclassProc).
	struct IEServerSubclassData {
		Control *ctrl; ///< Owning control.
	};

	/// Popup (browser) window subclass data - used to suppress DWM shadow.
	struct ControlSubclassData {
		Control *ctrl; ///< Owning control.
	};

	/// Scheme + host + port of an http(s) URL, with default ports made explicit.
	struct OriginParts {
		std::string scheme;
		std::string host;
		int port = 0;
		bool valid = false;

		/// Canonical "scheme://host[:port]" (default ports omitted). Empty if invalid.
		std::string canonical() const {
			if ( !valid )
				return {};
			std::string s = scheme + "://" + host;
			int def = ( scheme == "https" ) ? 443 : 80;
			if ( port != def )
				s += ":" + std::to_string( port );
			return s;
		}
	};

	/**
	 * @brief Stable indirection between Lua closures and a live Control.
	 *
	 * All Lua closures hold a ControlHandle* instead of a raw Control*.
	 * DoQuit nulls handle->ctrl BEFORE freeing the Control, so any closure
	 * called after quit - including l_gc fired by the GC after Cleanup() -
	 * sees a null ctrl and returns immediately.
	 *
	 * Lifetime: allocated in l_create, freed by l_gc after DoQuit has nulled ctrl.
	 */
	struct ControlHandle {
		Control *ctrl; ///< Non-null while the control is alive; nullptr after DoQuit.
	};

	// -------------------------------------------------------------------------
	// Process-wide state (defined in trident_util.cpp)
	// -------------------------------------------------------------------------

	/// Live contexts, one per Rain instance (guarded by g_contextsMutex).
	extern std::unordered_map<Rain *, std::shared_ptr<Context>> g_contexts;
	/// Guards g_contexts.
	extern std::mutex g_contextsMutex;
	/// Process-wide source of control ids (see trident_util.cpp).
	extern std::atomic<int> g_nextControlId;
	/// OleInitialize() was called by this module.
	extern bool g_comInitialized;
	/// OleUninitialize() must run at the end (OleInitialize succeeded or returned S_FALSE).
	extern bool g_comNeedsUninitialize;
	/// Depth of frames that may re-enter Lua or COM (see ReentryGuard).
	extern int g_reentry;
	/// Depth of frames that run inside a COM event of the browser (see ComEventGuard).
	extern int g_comEvent;

	// -------------------------------------------------------------------------
	// Shared helpers (trident_util.cpp)
	// -------------------------------------------------------------------------

	/// Registered window message used to raise the skin when the browser is clicked.
	UINT FocusMessage();

	/// Navigate2 flags for programmatic navigations (cache isolation).
	long NavigateFlags( const Control &ctrl );

	/// Lua registry key of the callback of a control.
	std::string GetCallbackKey( Rain *rain, int configId );

	/// Lua registry key of the browser object of a control.
	std::string GetBrowserKey( Rain *rain, int configId );

	/// 0xRRGGBB (Lua) to COLORREF.
	COLORREF RgbToColorRef( lua_Integer rgb );

	/// COLORREF to a CSS "#rrggbb" string.
	std::wstring ColorRefToCss( COLORREF c );

	// -------------------------------------------------------------------------
	// Location resolution (trident_url.cpp)
	// -------------------------------------------------------------------------

	/// Resolves a URL, host:port or path into a Navigate2 URL.
	std::wstring ResolveUrl( Rain *rain, const std::string &url, bool &isLocal );

	// -------------------------------------------------------------------------
	// URL / origin policy (trident_policy.cpp)
	// -------------------------------------------------------------------------

	/// Case-folds a path and normalizes separators for comparisons.
	std::wstring FoldPath( std::wstring s );

	/// file:/// URL to a folded local path (empty when unsafe).
	std::wstring FileUrlToFoldedPath( const std::string &url );

	/// Parses scheme://host[:port] of an http(s) URL.
	OriginParts ParseOrigin( std::string url, bool requireNoPath );

	/// True for "about:blank" (lowercase input, with optional fragment).
	bool IsAboutBlank( const std::string &lowerUrl );

	/// COM identity comparison.
	bool SameObject( IUnknown *a, IUnknown *b );

	/// True if the caller of window.external belongs to a trusted origin.
	bool IsOriginTrusted( Control *ctrl, IServiceProvider *pspCaller = nullptr );

	/// True if the loaded document contains VBScript (DOM tripwire).
	bool DocumentUsesVbScript( IHTMLDocument2 *doc );

	/// Unconditional navigation guard (schemes, VBScript, local files).
	bool IsNavigationAllowed( const Control &ctrl, const std::wstring &wurl );

	// -------------------------------------------------------------------------
	// Process hardening and security objects (trident_security.cpp)
	// -------------------------------------------------------------------------

	/// Registers refusing class objects for the VBScript engines (idempotent, STA thread).
	void BlockVbScriptEngine();

	/// Revokes the class objects registered by BlockVbScriptEngine().
	void UnblockVbScriptEngine();

	/// Process-wide Internet Feature Controls that harden the embedded engine (idempotent).
	void ApplyProcessHardening();

	// -------------------------------------------------------------------------
	// WebBrowser site (trident_site.cpp)
	// -------------------------------------------------------------------------

	/// Pushes the host UI flags and ambient properties to the ATL host of a control.
	void ApplyAmbientHostUi( const Control &ctrl );

	/// Installs the WebBrowserSite as UI handler of a freshly loaded document.
	void InstallUiHandler( IDispatch *docDisp, int configId, const std::shared_ptr<Context> &ctx );

	// -------------------------------------------------------------------------
	// Events (trident_events.cpp)
	// -------------------------------------------------------------------------

	/// Queues an event for the Lua callback (bounded; drops when full).
	bool EnqueueEvent( Context &ctx, MshtmlEvent ev );

	/// Delivers queued events to the Lua callbacks; returns the number delivered.
	int DrainEventQueue( Context *ctx, Rain *rain );

	/// Creates the message-only window that dispatches queued events on the Rainmeter thread.
	HWND CreateHiddenWindow( Context *ctx );

	/// Unregisters the hidden window class (all hidden windows must be gone). Safe to call when not registered.
	void UnregisterHiddenWindowClass();

	/// Connects the DWebBrowserEvents2 sink to the control.
	void ConnectEventSink( Control &ctrl, std::shared_ptr<Context> ctx );

	/// Disconnects and releases the event sink of the control.
	void DisconnectEventSink( Control &ctrl );

	// -------------------------------------------------------------------------
	// Window and control creation (trident_window.cpp)
	// -------------------------------------------------------------------------

	/// Moves/resizes the popup to follow the skin.
	void UpdateControlPosition( Control *ctrl );

	// -------------------------------------------------------------------------
	// Theme and data-* attributes (trident_theme.cpp)
	// -------------------------------------------------------------------------

	/// "dark" or "light": the Windows app theme.
	std::string WindowsTheme();

	/// The theme in use: create{ theme } when set, otherwise the Windows theme.
	std::string EffectiveTheme( const Control &ctrl );

	/// "light" / "dark" (any case) -> itself; "" or "auto" -> ""; anything else -> "" and ok = false.
	std::string NormalizeThemeName( const std::string &name, bool &ok );

	/**
	 * @brief Reads a Lua table of custom attributes ({ name = value, ... }) from stack index `idx`.
	 *
	 * Names are lowercased, an optional "data-" prefix is dropped, and only [a-z0-9_.-] (1..64 chars, not
	 * starting with '.' or '-') is accepted. Values may be strings, numbers or booleans. `theme` is reserved.
	 * Invalid entries are skipped and described in `problems`.
	 */
	void ReadDataTable( lua_State *L, int idx, std::map<std::string, std::string> &out, std::vector<std::string> &problems );

	/// Pushes { name = value, ... } (names without the "data-" prefix).
	void PushDataTable( lua_State *L, const std::map<std::string, std::string> &data );

	/// Forgets which attributes were written on the previous document (call when a new document was loaded).
	void ResetThemeState( Control &ctrl );

	/// Writes data-theme and the data-* attributes on the document's <html> element. Idempotent.
	void ApplyThemeData( Control &ctrl );

	/// Keeps the popup directly above the skin window, whatever the skin's z-position is (trident_window.cpp).
	void SyncControlZOrder( Control *ctrl );

	/// Brings the skin to the front and keeps the popup glued above it, without activating (trident_window.cpp).
	void BringSkinToFront( Control *ctrl );

	/// Subclass procedure of the skin window.
	LRESULT CALLBACK ParentSubclassProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData );

	/// Subclass procedure of the Internet Explorer_Server window.
	LRESULT CALLBACK IEServerSubclassProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData );

	/// Subclass procedure of the popup window.
	LRESULT CALLBACK ControlSubclassProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData );

	/// Finds the Internet Explorer_Server child window and subclasses it.
	void EnumAndSubclassIEServer( Control *ctrl );

	/// Creates the popup window and the hosted WebBrowser control.
	bool CreateWebBrowserControl( Control &ctrl, Rain *rain, std::string &outError );

	// -------------------------------------------------------------------------
	// Document operations (trident_document.cpp)
	// -------------------------------------------------------------------------

	/// True if the control currently shows about:blank (or nothing).
	bool IsBlankLocation( Control &ctrl );

	/// Writes sanitized HTML into an about:blank document.
	bool WriteIntoDocument( Control &ctrl, const std::string &content, std::string &err );

	/// True if an execScript argument names a single-line .js file.
	bool LooksLikeScriptFile( const std::string &s );

	/// Runs a function body in the page and returns its typed result.
	bool EvalInWindow( IHTMLWindow2 *window, const std::wstring &body, VARIANT *out, std::wstring &error );

	// -------------------------------------------------------------------------
	// Lua methods (trident_methods.cpp)
	// -------------------------------------------------------------------------

	/// Resolves the ControlHandle of the running Lua closure.
	ControlHandle *HandleFromUpvalue( lua_State *L );

	/// browser:navigate( url )
	int l_navigate( lua_State *L );

	/// browser:back()
	int l_back( lua_State *L );

	/// browser:forward()
	int l_forward( lua_State *L );

	/// browser:refresh()
	int l_refresh( lua_State *L );

	/// browser:stop()
	int l_stop( lua_State *L );

	/// browser:write( html )
	int l_write( lua_State *L );

	/// browser:setTransparent( ... )
	int l_setTransparent( lua_State *L );

	/// browser:execScript( jsOrFile )
	int l_execScript( lua_State *L );

	/// browser:getURL()
	int l_getURL( lua_State *L );

	/// browser:reposition()
	int l_reposition( lua_State *L );

	/// browser:show()
	int l_show( lua_State *L );

	/// browser:hide()
	int l_hide( lua_State *L );

	/// browser:setContextMenu( bool )
	int l_setContextMenu( lua_State *L );
	int l_redraw( lua_State *L );

	/// browser:quit()
	int l_quit( lua_State *L );

	/// __gc of the handle box
	int l_gc( lua_State *L );

	// -------------------------------------------------------------------------
	// trident.create() options (trident_options.cpp)
	// -------------------------------------------------------------------------

	/// Reads the trident.create() options table into a Control (defaults included).
	void ParseCreateOptions( lua_State *L, Rain *rain, Control &ctrl );

	/// Resolves and validates the url option of trident.create().
	std::wstring ResolveInitialUrl( lua_State *L, Rain *rain, const Control &ctrl );

	// -------------------------------------------------------------------------
	// Lua browser object (trident_object.cpp)
	// -------------------------------------------------------------------------

	/// Builds the Lua browser object (handle box, methods, bind, document/window).
	int PushBrowserObject( lua_State *L, Rain *rain, Control *storedCtrl, ControlHandle *handle );

	// -------------------------------------------------------------------------
	// trident.create() (trident_create.cpp)
	// -------------------------------------------------------------------------

	/// trident.create( options )
	int l_create( lua_State *L );

	// -------------------------------------------------------------------------
	// Control lifetime (trident_lifetime.cpp)
	// -------------------------------------------------------------------------

	/**
	 * @brief Marks a frame that can re-enter Lua or COM (event handlers, Lua callbacks, nested message loops).
	 *
	 * While at least one guard is alive, quit() and garbage collection only queue the close of a control;
	 * the real teardown runs after the last guard is gone (item 36). Hold it for the whole frame.
	 */
	class ReentryGuard {
	public:
		ReentryGuard();
		~ReentryGuard();
		ReentryGuard( const ReentryGuard & ) = delete;
		ReentryGuard &operator=( const ReentryGuard & ) = delete;
	};

	/**
	 * @brief A ReentryGuard for frames that run inside a COM event of the browser (BeforeNavigate2, NewWindow3,
	 *        a bound window.external function, a Lua event handler called by MSHTML).
	 *
	 * Besides deferring quit(), navigate()/back()/forward()/refresh()/write() requested from such a frame
	 * are queued and run after it (item 36): starting another navigation from inside BeforeNavigate2 of the
	 * same browser is what used to bring the process down.
	 */
	class ComEventGuard : public ReentryGuard {
	public:
		ComEventGuard();
		~ComEventGuard();
	};

	/// Enter/leave of the guards as plain functions (ReentryEnter/Leave are plain; ComCall* also mark a COM event).
	void ReentryEnter();
	void ReentryLeave();
	void ComCallEnter();
	void ComCallLeave();

	/// True while a frame inside a COM event of the browser is on the stack.
	bool InComEvent();

	/// The live control with this id, or nullptr (valid until the control is closed).
	Control *FindControl( Rain *rain, int configId );

	/// Queues `fn` to run when no guarded frame is left. The function must look the control up again by id.
	void DeferToIdle( Rain *rain, std::function<void()> fn );

	/// The live context of a Rain instance, or nullptr.
	std::shared_ptr<Context> FindContext( Rain *rain );

	/// Releases every resource of a control (idempotent). Used by both CloseControl() and Cleanup().
	void TeardownControl( Control &ctrl, Rain *rain );

	/// Sets a Lua registry entry to nil (no-op for an empty key).
	void ClearRegistryKey( lua_State *L, const std::string &key );

	/// Tears the control down now and removes it from its context.
	void CloseControl( Rain *rain, int configId );

	/// quit(): stops the callbacks of the control and closes it now, or later if a guarded frame is active.
	void RequestCloseControl( Rain *rain, int configId );

	/// Runs the queued actions and closes when no guarded frame is left (called from the hidden window).
	void FlushDeferred();

	/// Posts WM_TRIDENT_FLUSH to every context that has queued work.
	void PostFlushRequests();

	/// Forgets the queued work of a context that is being cleaned up.
	void DiscardDeferred( Context &ctx );

	// -------------------------------------------------------------------------
	// window.external (trident_external.cpp) - ExternalDispatch stays private to that file
	// -------------------------------------------------------------------------

	/// Creates the window.external object of a control (reference count 1, owned by Control::externalDispatch).
	void *ExternalCreate( std::shared_ptr<Context> ctx, int configId );
	/// Returns the object as IDispatch with an added reference (for IDocHostUIHandler::GetExternal).
	void ExternalGetDispatch( void *ext, IDispatch **out );
	/// Binds the Lua function at stack index `idx` as window.external.<name>.
	HRESULT ExternalBind( void *ext, const char *name, lua_State *L, int idx );
	/// Releases every bound Lua function and drops the owner reference.
	void ExternalShutdown( void *ext );

	// -------------------------------------------------------------------------
	// Security objects (trident_security.cpp) - the classes stay private to that file
	// -------------------------------------------------------------------------

	/// Creates the IInternetSecurityManager handed to MSHTML (reference count as returned by `new`).
	IInternetSecurityManager *CreateSkinSecurityManager( const std::wstring &skinPath, bool allowSkin, bool allowActiveX, bool allowCredentials, bool allowCookies );
	/// Wraps an IDropTarget so that dropped files and links are filtered.
	IDropTarget *CreateFilteringDropTarget( IDropTarget *inner );

} // namespace trident
