/**
 * @file trident.hpp
 * @brief Trident WebBrowser control module for RainJIT.
 * @license GPL v2.0 License
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
 *     callback = function(event)
 *         if event.type == "documentcomplete" then
 *             browser:write([[
 *                 <html><body style="background:transparent;color:white">
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
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

struct Rain;

namespace mshtml {

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
		ALLOW_LOCAL = 1 << 5, ///< Permit file:// URLs (modifier for VALIDATE_URLS).

		SANITIZE_ALL = BLOCK_SCRIPTS | BLOCK_EVENTS | FILTER_CSS | VALIDATE_URLS,
	};

	/**
	 * @brief Event types emitted by the WebBrowser control.
	 */
	enum EventType {
		EVENT_NONE = 0, ///< No event.
		EVENT_DOCUMENT_COMPLETE, ///< Document loading finished.
		EVENT_NAVIGATE_COMPLETE, ///< Navigation to a URL completed.
		EVENT_TITLE_CHANGE, ///< Document title changed.
		EVENT_EXTERNAL, ///< JS called window.external.notify(name, data).
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
		COLORREF colorKey; ///< Color used for transparency masking.
		long width, height; ///< Desired dimensions (before constraint and padding).
		long left, top; ///< Desired position relative to parent.
		Rain *rain; ///< Owning Rainmeter instance.
		std::string callbackKey; ///< Lua registry key for the callback function.
		std::string browserKey; ///< Lua registry key for the browser object (used to inject self).
		bool enabled; ///< Whether event processing is active.
		void *subclassData; ///< Opaque pointer to ParentSubclassData.
		bool silent; ///< Suppress script error dialogs.

		// Layout constraint and appearance
		bool insideSkin; ///< If true, control is clipped to parent window bounds.
		int padLeft; ///< Left padding (positive shrinks from left edge).
		int padTop; ///< Top padding (positive shrinks from top edge).
		int padWidth; ///< Width reduction.
		int padHeight; ///< Height reduction.
		int cornerRadius; ///< Radius for rounded corners (0 = no rounding).

		// Security
		uint32_t sanitizeFlags; ///< Bitmask of active SanitizeFlags. Default: SANITIZE_ALL.

		// Visibility
		bool hidden; ///< If true, window is created but kept hidden. Use show()/hide() to toggle.
	};

	/**
	 * @brief Global context for mshtml module, one per Rain instance.
	 */
	struct Context {
		std::unordered_map<int, Control> controls; ///< Active controls mapped by configId.
		Rain *rain; ///< Owning Rainmeter instance.
		int nextId; ///< Next available configId.
		HWND hiddenWindow; ///< Hidden message-only window for COM STA.
		std::queue<MshtmlEvent> eventBuffer; ///< Thread-safe event queue.
		std::mutex eventMutex; ///< Mutex protecting eventBuffer.
		bool hasPendingEvents; ///< Flag indicating non-empty buffer.
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

} // namespace mshtml
