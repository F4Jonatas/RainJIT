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
 * @file trident.hpp
 * @brief Trident WebBrowser control module for RainJIT.
 * @license GPL-2.0-or-later (GNU General Public License v2.0 or later)
 *
 * @par window.external / events (security)
 * - `window.external.call(name, data)` produces `{ type = "external", name = name, data = <string> }`.
 *   `data` is ALWAYS a string (never evaluated as Lua): decode it in Lua with a JSON module inside pcall.
 *   `window.external.call("name")` (one argument) is an event with a name and no data.
 * - `browser:bind(name, fn)` exposes `window.external.<name>(...)` to JavaScript; the Lua return value is
 *   converted back. `call` is reserved for the event channel and cannot be bound. Errors raised by `fn`
 *   reach the page as a script error carrying the Lua message. Reading a member without calling it
 *   (`if (window.external.call) ...`) yields a callable reference instead of an error.
 * - `window.external` is only exposed to about:blank documents, local files inside the skin folder
 *   and origins listed in create{ externalOrigins = {"https://host"} }. `external = false` disables it.
 *   Origins are matched as parsed scheme/host/port ("https://a.com" == "https://A.com:443", never
 *   "https://a.com.evil.com" or "https://a.com@evil.com").
 *
 * @par Sanitizer options (create{ sanitize = ..., allowedHosts = ..., reservedNames = ... })
 * - `<style>` elements are rebuilt rule by rule (no @import, no filter/progid/behavior/expression).
 * - `allowedHosts = {"cdn.example.com", "*.example.com"}`: remote images/CSS urls may only load from these hosts
 *   (an empty table blocks all remote sub-resources). Plain links are not restricted.
 * - `reservedNames = {"myGlobal"}`: id/name values dropped in addition to the built-in clobbering list.
 * - `//host` and `\\host\share` URLs are always rejected (SMB / NTLM leak).
 *
 * @par Navigation, downloads, drag & drop, ActiveX (create options)
 * - Every navigation (clicked links, `location=`, forms, meta refresh, frames, and the FINAL url
 *   after redirects) goes through the URL validator; blocked ones are cancelled or dropped to about:blank.
 * - `navigation = "locked"`: the user/page cannot navigate the top level at all; only
 *   navigate()/back()/forward()/refresh() can. Default `"free"`.
 * - `allowDownloads = true`: file downloads are cancelled by default and reported as `{ type = "filedownload" }`.
 * - `cache = true`: use the WinINet cache shared with Internet Explorer. By default navigations neither read nor
 *   write that cache (navNoReadFromCache | navNoWriteToCache), so the control does not mix data with the user's IE.
 * - Popups (`{ type = "newwindow", url = "..." }`, synchronous): cancelled unless the callback returns `true`. An allowed
 *   popup is opened in the user's default browser, and only for http://, https:// and mailto: targets.
 * - Dropping files or links onto the control is refused (text drops into inputs still work).
 * - `allowActiveX = true`: by default ActiveX, shell-execute, downloads of controls and Java are DENIED for
 *   every URL (so `CreateObject("WScript.Shell")` fails); skin files only receive a small allowlist.
 * - `allowCredentials = true` / `allowCookies = true`: by default pages get no silent NTLM/Kerberos logon and no cookies
 *   (the engine shares WinINet with Internet Explorer). Third-party cookies are never allowed.
 *
 * @par write() and execScript() results
 * - write( html ) returns `true`, `true, "deferred"` (the HTML is written once about:blank has loaded; it is never
 *   written into a remote document, whose origin document.open() would keep) or `nil, message`.
 * - execScript( js ) runs the text as a function body through the page's own `window.eval` and returns the typed
 *   result (number, string, boolean, nil, or a ComProxy for objects) or `nil, message`. The argument is treated
 *   as a file only if it is a single line ending in ".js".
 * - `silent` hides script error dialogs (put_Silent + OLECMDID_SHOWSCRIPTERROR handler).
 *
 * @par VBScript (always blocked, no option re-enables it)
 * - Sanitizer: `<script>` with a non-JavaScript language/type, the `language` attribute, `vbscript:` values,
 *   and `<meta http-equiv="Content-Script-Type">` are dropped even with allow_scripts/allow_events/allow_css.
 * - write() with `sanitize = false` is refused if the text mentions VBScript; navigate()/create()/links to
 *   `vbscript:` URLs and to local files that mention VBScript (or .vbs/.vbe/.hta/.htc/...) are blocked.
 * - Engine level: the VBScript and VBScript.Encode engines cannot be instantiated in the process
 *   (class objects that refuse creation are registered), so run-time created VBScript fails too.
 * - Tripwire: a document that ends up with a VBScript `<script>` is replaced by about:blank.
 *
 * @par Process hardening
 * Internet Feature Controls (no ActiveX install prompts, downloads need a gesture, zone elevation, mk: protocol,
 * user:pass@host URLs, ...) are enabled process-wide on first create(). Navigations skip the shared IE cache.
 * The engine still runs inside the Rainmeter process and cannot be sandboxed from here.
 *
 * - `window.external` also requires the CALLING frame to be trusted (a third-party iframe in a trusted
 *   page cannot use it); `about:blank` frames below the top level are never trusted.
 *
 * @par Theme and data attributes (create options theme / data, method redraw)
 * - The engine (MSHTML) supports neither `var(--x)` nor `light-dark()`, so the page styles itself with attribute
 *   selectors, which it does support. The module writes attributes on the `<html>` element and never touches the CSS:
 *   @code{.css}
 *   [data-theme="dark"] body { background: #111; }
 *   [data-accent="red"] a    { color: red; }
 *   @endcode
 * - `theme = "light" | "dark"` is written as `data-theme`. Without the option the Windows app theme is used.
 * - `data = { accent = "red", ["theme-color"] = "blue" }` is written as `data-accent="red"` and
 *   `data-theme-color="blue"`. Names are lowercased (an optional "data-" prefix is dropped) and accept letters,
 *   digits, `_`, `.` and `-`; values may be strings, numbers or booleans; `theme` is reserved. At most 64 entries.
 * - The attributes are written as soon as the document exists (NavigateComplete2, so the first paint already
 *   matches them), again at DocumentComplete, and after write().
 * - `browser:redraw( [data [, theme]] )`: `redraw()` re-reads the Windows theme (when `theme` was not given) and
 *   writes everything again; `redraw( { ... } )` REPLACES all data attributes (the theme is kept) and removes the
 *   ones that are gone; `redraw( {} )` removes them all; `redraw( nil, "dark" )` changes the theme ("light",
 *   "dark", or "" / "auto" for the Windows theme). Returns the data table in effect and the theme in use, or
 *   `nil, message` for invalid arguments. Called from a browser event, the page is updated right after it.
 *
 * @par Control ids and lifetime
 * - Control ids (used for subclassing, the event queue and Lua registry keys) are unique per process, so
 *   several measures of the same skin can each own controls without interfering with each other.
 * - A control lives until `browser:quit()` or until the skin unloads; the module keeps the browser table
 *   referenced for that purpose, so keeping the return value of create() is optional. Methods saved by the
 *   user are safe to call after quit(): they simply do nothing.
 * - The host-level border flags and context-menu policy are applied through the ATL host before the first
 *   page loads; the per-document handler (window.external, drop filter) is attached at NavigateComplete2.
 *
 * Embeds an Internet Explorer WebBrowser control (Shell.Explorer) into a Rainmeter skin window.
 * Provides Lua bindings for navigation, script injection, transparency, and event handling.
 *
 * @module trident
 * @usage local trident = require("webview.trident")
 *
 * @details
 * - Creates a layered popup window hosting the WebBrowser ActiveX control.
 * - Supports per-control transparency via color keying and `WS_EX_LAYERED`.
 * - Automatically constrains the control within the skin bounds if `insideSkin` is true.
 * - Allows padding and rounded corners (via window regions).
 * - Event callbacks are delivered asynchronously through `ProcessMessages`.
 * - COM is initialized in apartment-threaded mode on first use.
 *
 * @example
 * @code{.lua}
 * local trident = require("webview.trident")
 *
 * local browser = trident.create({
 *     url = "about:blank",
 *     width = 400,
 *     height = 300,
 *     left = 10,
 *     top = 10,
 *     transparent = true,
 *     colorKey = 0xFF00FF, -- magenta
 *     insideSkin = true,
 *     padding = {5, 5, 10, 10}, -- left, top, width, height
 *     cornerRadius = 12,
 *     silent = true,
 *     callback = function(browser, event)
 *         -- write() ends with document.close(), which may raise another documentcomplete:
 *         -- write only once (the event caused by write() itself is also filtered out by the module).
 *         if event.type == "documentcomplete" and not browser.written then
 *             browser.written = true
 *             browser:write([[
 *                 <html><body style="color:white">
 *                     <h1>Hello from Lua!</h1>
 *                 </body></html>
 *             ]])
 *         end
 *     end
 * })
 * @endcode
 */

#pragma once

#include <ExDisp.h>
#include <ExDispid.h>
#include <Windows.h>
#include <docobj.h>
#include <lua.hpp>
#include <mshtml.h>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include "luaVariant.hpp"
#include "comProxy.hpp"


struct Rain;


namespace trident {

	/**
	 * @brief Bitmask controlling which sanitization passes are active per control.
	 *
	 * @details
	 * Default is SANITIZE_ALL (block everything). Use a Lua table with allow_*
	 * tokens to selectively re-enable content that is blocked by default.
	 *
	 * Lua sanitize option:
	 *   sanitize = true              -- SANITIZE_ALL (default, block everything)
	 *   sanitize = false             -- SANITIZE_NONE (no filtering)
	 *   sanitize = { 'allow_*', }   -- starts from SANITIZE_ALL, clears named blocks
	 *
	 * Available allow tokens:
	 *   "allow_clobbering" -- keep id/name values that shadow document/window/form members
	 *   "allow_overlay"    -- keep position:fixed|sticky and large z-index in CSS
	 *   "allow_scripts" -- permit script, iframe, object, embed and their children
	 *   "allow_events"  -- permit on* attributes (onclick, onload, etc.)
	 *   "allow_style"   -- pass style attribute through entirely (implies allow_css)
	 *   "allow_css"     -- keep style attribute but skip dangerous CSS filtering
	 *   "allow_urls"    -- skip URL validation on href/src/action and navigate/create
	 *   "allow_local"   -- permit file:// URLs (only meaningful when allow_urls is NOT set)
	 */
	enum SanitizeFlags : uint32_t {
		SANITIZE_NONE = 0,

		BLOCK_SCRIPTS = 1 << 0, ///< Strip script/iframe/object/embed + entire subtree.
		BLOCK_EVENTS = 1 << 1, ///< Strip on* event handler attributes.
		BLOCK_STYLE = 1 << 2, ///< Remove style attribute entirely (overrides FILTER_CSS).
		FILTER_CSS = 1 << 3, ///< Filter dangerous declarations in style attribute.
		VALIDATE_URLS = 1 << 4, ///< Validate href/src/action attrs and navigate/create URLs.
		ALLOW_LOCAL = 1 << 5, ///< Permit file:///path URLs (modifier for VALIDATE_URLS). UNC / file://host is never allowed.
		BLOCK_CLOBBERING = 1 << 6, ///< Drop id/name values that shadow document/window/form members (DOM clobbering).
		BLOCK_OVERLAY = 1 << 7, ///< Drop position:fixed|sticky and large z-index (UI redress).

		SANITIZE_ALL = BLOCK_SCRIPTS | BLOCK_EVENTS | FILTER_CSS | VALIDATE_URLS | BLOCK_CLOBBERING | BLOCK_OVERLAY,
	};

	/**
	 * @brief Top-level navigation policy (Lua option `navigation`).
	 */
	enum NavigationMode {
		NAV_FREE = 0, ///< Default. Any navigation whose URL passes the URL validator is allowed.
		NAV_LOCKED = 1, ///< Only navigate()/back()/forward()/refresh()/create{url} may change the top-level page.
	};

	/**
	 * @brief Event types emitted by the WebBrowser control.
	 */
	enum EventType {
		EVENT_NONE = 0, ///< No event.
		EVENT_DOCUMENT_COMPLETE, ///< Document loading finished.
		EVENT_NAVIGATE_COMPLETE, ///< Navigation to a URL completed.
		EVENT_TITLE_CHANGE, ///< Document title changed.
		EVENT_EXTERNAL, ///< JS called window.external.call(name, data).
		EVENT_STATUS_TEXT_CHANGE, // status bar text changed
		EVENT_PROGRESS_CHANGE, // download progress
		EVENT_DOWNLOAD_BEGIN, // download started
		EVENT_DOWNLOAD_COMPLETE, // download finished
		EVENT_NAVIGATE_ERROR, // navigation failed
		EVENT_NEW_WINDOW, // popup window requested (cancelable)
		EVENT_WINDOW_CLOSING, // window.close() called (cancelable)
		EVENT_FILE_DOWNLOAD, // file download requested; cancelled unless allowDownloads (event is informational)
		EVENT_COMMAND_STATE_CHANGE, // back/forward state changed
	};

	/**
	 * @brief Thread-safe event structure passed from the browser sink to the main thread.
	 */
	struct MshtmlEvent {
		EventType type; ///< Type of the event.
		std::wstring title; ///< New document title (EVENT_TITLE_CHANGE only).
		std::wstring name; ///< External event name  (EVENT_EXTERNAL only).
		std::wstring data; ///< External event data  (EVENT_EXTERNAL only).
		int configId; ///< ID of the control that generated the event.
		ULONGLONG timestamp; ///< Milliseconds since system start.
		long progress = 0; // EVENT_PROGRESS_CHANGE
		long progressMax = 0; // EVENT_PROGRESS_CHANGE
		long statusCode = 0; // EVENT_NAVIGATE_ERROR
		bool cancel = false; // cancelable events
	};

	/**
	 * @brief Per-control state and COM interface pointers.
	 */
	struct Control {
		HWND hwndParent; ///< Handle to the Rainmeter skin window.
		HWND hwndControl; ///< Handle to the popup window hosting the browser.
		IWebBrowser2 *webBrowser; ///< Main browser interface.
		IOleObject *oleObject; ///< OLE object interface (currently unused, reserved).
		DWORD eventCookie; ///< Connection point cookie for event sinking.
		IConnectionPoint *eventCP; ///< Connection point for DWebBrowserEvents2.
		IDispatch *eventSink; ///< Custom event sink object.
		int configId; ///< Unique identifier for this control.
		bool transparent; ///< Whether color-key transparency is enabled.
		COLORREF colorKey; ///< Color-key for transparency, stored as COLORREF. Lua passes 0xRRGGBB; the body background is painted with this color.
		long width, height; ///< Desired dimensions (before constraint and padding).
		long left, top; ///< Desired position relative to parent.
		Rain *rain; ///< Owning Rainmeter instance.
		std::string callbackKey; ///< Lua registry key for the callback function.
		std::string browserKey; ///< Lua registry key for the browser object (used to inject self).
		bool enabled; ///< Whether event processing is active.
		void *subclassData;        ///< Opaque pointer to ParentSubclassData.
		void *controlSubclassData; ///< Opaque pointer to ControlSubclassData (popup window subclass).
		bool silent; ///< Suppress script error dialogs. Default true; only an explicit `silent = false` disables it.
		bool writePending; ///< True right after write() called document.close(): the next top-level documentcomplete is ours and is not forwarded to Lua.
		ULONGLONG writeTick; ///< GetTickCount64() of that write(); the flag expires after ~2 s.
		bool hasPendingWrite; ///< write() was deferred: the document is not about:blank yet (item 31).
		std::string pendingHtml; ///< Sanitized HTML waiting for the about:blank document (see hasPendingWrite).

		// Layout constraint and appearance
		bool insideSkin; ///< If true, control is clipped to parent window bounds.
		int padLeft; ///< Left padding (positive shrinks from left edge).
		int padTop; ///< Top padding (positive shrinks from top edge).
		int padWidth; ///< Width reduction.
		int padHeight; ///< Height reduction.
		int cornerRadius; ///< Radius for rounded corners (0 = no rounding).

		// Security
		uint32_t sanitizeFlags; ///< Bitmask of active SanitizeFlags. Default: SANITIZE_ALL.
		int navigationMode; ///< NavigationMode. Default NAV_FREE.
		bool programmaticNav; ///< True only while navigate()/back()/forward()/refresh() is running (consumed by BeforeNavigate2).
		bool allowDownloads; ///< If false (default) file downloads are cancelled and reported as "filedownload".
		bool useCache; ///< create{ cache = true }: use the WinINet cache shared with IE. Default false (isolated).
		bool closing; ///< quit() was requested: no more callbacks; the teardown may still be pending (item 36).
		bool allowActiveX; ///< If false (default) the security manager denies ActiveX/shell/Java actions for every URL.
		bool allowCredentials; ///< If false (default) no silent NTLM/Kerberos/Basic logon: pages are limited to anonymous requests.
		bool allowCookies; ///< If false (default) cookies are denied. Third-party cookies are always denied.
		bool restrictHosts; ///< If true, remote sub-resources (src, CSS url(), ...) must match allowedHosts. Set by create{ allowedHosts = {...} }.
		std::vector<std::string> allowedHosts; ///< Lowercase hosts: "example.com" or "*.example.com". Empty + restrictHosts = no remote sub-resources.
		std::vector<std::string> reservedNames; ///< Extra lowercase id/name values dropped from sanitized HTML (e.g. the skin's JS globals).

		// window.external exposure (fix #8)
		bool externalEnabled; ///< If false, window.external is never exposed. Default: true.
		std::vector<std::string> externalOrigins; ///< Extra trusted origins, canonical "scheme://host[:port]" (lowercase, default port omitted), compared as parsed origins. Local skin files and about:blank are always trusted.

		// Theme and custom CSS (trident_theme.cpp)
		std::string themeOption; ///< create{ theme = ... }: "light", "dark", or empty = follow the Windows app theme.
		std::map<std::string, std::string> data; ///< create{ data = {...} }: attribute name (without "data-") -> value.
		std::vector<std::string> dataApplied; ///< Attribute names written on the current document (to remove the ones dropped by redraw()).

		// Visibility
		bool hidden; ///< If true, window is created but kept hidden. Use show()/hide() to toggle.

		// Interaction
		bool contextMenu; ///< If false, suppresses the browser context menu and forwards right-click to the skin window.
		HWND hwndIEServer; ///< Handle to the Internet Explorer_Server child window.
		void *ieServerSubclassData; ///< Opaque pointer to IEServerSubclassData.

		// pointer to the ExternalDispatch object (IDispatchEx)
		void *externalDispatch;

		// Back-reference to the ControlHandle owned by the Lua sentinel.
		// Cleanup() nulls handle->ctrl via this pointer so that any pending
		// l_gc call after the context is destroyed is a safe no-op.
		void *handle; ///< ControlHandle*; nullptr until l_create sets it.
	};

	/**
	 * @brief Global context for trident module, one per Rain instance.
	 */
	struct Context {
		std::unordered_map<int, std::unique_ptr<Control>> controls; ///< Active controls mapped by configId. unique_ptr keeps Control addresses stable across rehash.
		Rain *rain = nullptr; ///< Owning Rainmeter instance.
		HWND hiddenWindow = nullptr; ///< Hidden message-only window for COM STA.
		std::queue<MshtmlEvent> eventBuffer; ///< Thread-safe event queue.
		std::mutex eventMutex; ///< Mutex protecting eventBuffer.
		bool hasPendingEvents = false; ///< Flag indicating non-empty buffer.
		size_t queuedBytes = 0; ///< Bytes of strings currently queued in eventBuffer (guarded by eventMutex). Bounded by kMaxQueuedBytes.
		bool alive = true; ///< false once Cleanup() ran (fix #11). Held by std::shared_ptr so COM objects given to MSHTML never touch freed memory.
		bool draining = false; ///< true while DrainEventQueue() runs Lua callbacks (a nested drain is postponed).
		std::vector<int> pendingClose; ///< Controls whose quit() arrived inside a guarded frame; closed by FlushDeferred().
		std::vector<std::function<void()>> deferred; ///< Actions (navigate, write...) requested inside a COM event; run by FlushDeferred().
	};

	/**
	 * @brief Registers the webview.trident module with Lua's package.preload.
	 *
	 * Called during plugin initialization.
	 *
	 * @param L   Lua state.
	 * @param rain Pointer to the Rain instance (passed as upvalue).
	 */
	void RegisterModule( lua_State *L, Rain *rain );

	/**
	 * @brief Processes pending browser events and invokes Lua callbacks.
	 *
	 * Must be called periodically (e.g., from Rainmeter's Update cycle).
	 *
	 * @param rain Pointer to the Rain instance.
	 * @return Number of events processed.
	 */
	int ProcessMessages( Rain *rain );

	/**
	 * @brief Releases all resources associated with a Rain instance.
	 *
	 * Disconnects event sinks, destroys windows, and uninitializes COM if this
	 * was the last active context.
	 *
	 * @param rain Pointer to the Rain instance to clean up.
	 */
	void Cleanup( Rain *rain );

} // namespace trident
