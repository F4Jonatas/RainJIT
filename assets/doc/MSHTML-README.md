
<div align="center">

  # Module MSHTML

  ### A legacy WebBrowser within Rainmeter<br>focused on performance and low resource consumption.

  <br>
  <br>


  <img src="../images/mshtml-logo.png" alt="LOGO" height="200">

</div>



## Summary

<details>

<summary><ins>Table of contents</ins></summary>

- [Overview](#overview)
- [Philosophy](#philosophy)
- [Architecture](#architecture)
- [The IE7 Trident Engine](#the-ie7-trident-engine)
- [API Reference](#api-reference)
- [Event System](#event-system)
- [Transparency](#transparency)
- [Layout & Padding](#layout--padding)
- [Limitations](#limitations)
- [Known Bugs & Fixes (v1.x)](#known-bugs--fixes-v1x)
- [License](#license)

</details>


<br>
<br>


## Overview

**MSHTML** is a native plugin module for **RainJIT**, the Lua scripting layer for **Rainmeter**. It exposes a simple Lua API around the legacy Windows WebBrowser ActiveX control, allowing a theme to render real-time HTML/CSS/JavaScript within a regular **Rainmeter** window—no external process, no Edge WebView2 runtime, no Chromium overhead.

```lua
local mshtml = require("mshtml")

local browser = mshtml.create{
    url    = "about:blank",
    width  = 400,
    height = 300,
    left   = 0,
    top    = 0,
    transparent  = true,
    colorKey     = 0xFF00FF,   -- magenta becomes transparent
    insideSkin   = true,
    silent       = true,
    cornerRadius = 10,
    callback = function(ev)
        if ev.type == "documentcomplete" then
            browser:loadHTML([[
                <html>
                <body style="background:transparent; color:white; font-family:sans-serif">
                    <h2>Hello from Lua!</h2>
                </body>
                </html>
            ]])
        end
    end
}
```

---

## Philosophy

The module was designed around a single constraint: **do as little as possible
with as few resources as possible.**

Rainmeter skins are decorative, always-on-top overlays.  They share the desktop
with dozens of other processes.  A browser control in that context should behave
like a widget, not a web application:

- **No background process.**  Everything runs inside the Rainmeter process via
  COM in-process activation.  The WebBrowser control is an in-proc server
  (`Shell.Explorer` ActiveX), so there is no extra process in Task Manager.

- **No layout engine of our own.**  HTML/CSS layout is delegated entirely to
  Trident.  The module only moves the popup window; it never parses markup.

- **No polling.**  Events (DocumentComplete, NavigateComplete, TitleChange) are
  queued by an `IDispatch` sink running on the same STA thread and drained
  during Rainmeter's normal `Update` cycle.  Zero background threads.

- **No dependencies beyond what Windows ships.**  ATL, OLE, and MSHTML are part
  of every Windows installation since Windows 98.  There is no third-party SDK
  to distribute or update.

- **Minimal Lua surface.**  The Lua API exposes only the operations a skin
  actually needs: navigate, write HTML, run a script snippet, resize, toggle
  transparency, and quit.  Anything more belongs in JavaScript on the page.

The trade-off is intentional: you get a fast, zero-cost idle footprint at the
cost of a legacy rendering engine.  If you need modern web APIs — Fetch,
WebSockets, ES2020+, CSS Grid — use a different control.  This module targets
*simple* use-cases: local HTML dashboards, transparent overlays, iframe-style
embedded content rendered from Lua-generated markup.

---

## Architecture

```
Rainmeter skin window (HWND parent)
│
├── RainJIT Lua state
│     └── mshtml module (package.preload)
│           └── mshtml.create() → browser object (Lua table)
│                 ├── navigate(), loadHTML(), execScript(), ...
│                 └── quit()
│
└── Popup window (HWND, WS_POPUP | WS_VISIBLE)   ← created by mshtml
      └── AtlAxWin class  (hosts Shell.Explorer ActiveX)
            └── IWebBrowser2 / MSHTML / Trident
                  └── CEventSink (IDispatch)
                        └── → eventBuffer (std::queue, mutex-protected)
                              └── ProcessMessages() → Lua callback
```

### COM Threading Model

COM is initialized in **apartment-threaded (STA)** mode on the first
`mshtml.create()` call in a given Rainmeter instance.  This matches the
threading model of `IWebBrowser2` and is required for `DWebBrowserEvents2` to
fire correctly.  All COM calls happen on the Rainmeter skin thread — there is
no worker thread.

### Event Delivery

`CEventSink::Invoke()` is called by Trident on the skin thread (same STA).  It
pushes a `MshtmlEvent` struct into a `std::queue` protected by `std::mutex`.
`ProcessMessages()`, called from Rainmeter's `Update` cycle, drains the queue
and calls the Lua callback.  Because everything runs on the skin thread, no
cross-thread Lua calls are ever made.

### Window Lifecycle

Each browser occupies a `WS_POPUP` window (not a child window).  This is
necessary because Rainmeter's skin window uses `WS_EX_LAYERED` with
`UpdateLayeredWindow`, and child windows cannot be layered independently — they
inherit the parent's alpha.  The popup floats above the parent and tracks its
position via a `SetWindowSubclass` hook on `WM_WINDOWPOSCHANGED`.

---

## The IE7 Trident Engine

`mshtml` uses **MSHTML** (also called **Trident**), the HTML/CSS rendering
engine that powered Internet Explorer.  Understanding its capabilities and
quirks is essential for writing skins that work reliably.

### What version of Trident do you get?

By default, `IWebBrowser2` renders in **IE7 compatibility mode** regardless of
the Windows version.  This is a deliberate Microsoft default — it prevents old
ActiveX hosts from breaking when Trident is updated.

To unlock a more modern rendering mode, the host process must declare a
`FEATURE_BROWSER_EMULATION` registry key under:

```
HKCU\Software\Microsoft\Internet Explorer\Main\FeatureControl\FEATURE_BROWSER_EMULATION
ProcessName.exe = <DWORD value>
```

Common values:

| Value  | IE version emulated |
|--------|---------------------|
| 7000   | IE7 (default)       |
| 8000   | IE8                 |
| 9000   | IE9                 |
| 10000  | IE10                |
| 11000  | IE11                |
| 11001  | IE11 (Edge mode)    |

Without this key, you get IE7.  With it set to `11001` (for `Rainmeter.exe`),
you get the closest Trident can offer to a modern browser — still not ES2020,
but close enough for most widget use cases.

### What IE7 Trident can do

- Full HTML4 / CSS2.1 layout (block, inline, float, positioned elements).
- JavaScript (JScript) with ES5-level support in IE9+ emulation mode.
- `XMLHttpRequest` (same-origin only — see Limitations).
- `localStorage` and `sessionStorage` (IE8+ emulation).
- SVG (partial, IE9+ emulation).
- Custom fonts via `@font-face` (IE9+).
- CSS transitions and basic animations (IE10+).
- `document.write()`, `innerHTML`, `execScript()` — all reliable.

### What IE7 Trident cannot do

- CSS Grid, CSS Custom Properties (variables), `calc()` with mixed units.
- ES6+ (`let`, `const`, arrow functions, Promises, `fetch`, template literals)
  — unless you polyfill them via a transpiled bundle.
- `WebSocket`, `WebRTC`, `WebGL`.
- Service Workers or any worker thread.
- Sandboxed `<iframe>` attributes.
- `<video>` / `<audio>` with modern codecs (H.264 may work via DirectShow).
- Content Security Policy headers (ignored).

### Transparency and Trident

Trident does not natively support per-pixel alpha blending when hosted in a
`WS_EX_LAYERED` window via `SetLayeredWindowAttributes`.  The module works
around this with **color-key transparency**: a specific color (default: magenta
`#FF00FF`) is declared as the transparent color, and the module injects
`background-color: transparent` into the document body via `IHTMLStyle` on
every `DocumentComplete` event.  Any element in the page whose background
renders to the key color will appear transparent in the final composited output.

> **Tip:** Pick a color that never appears intentionally in your HTML.  Pure
> magenta (`#FF00FF`) or pure cyan (`#00FFFF`) are common choices.

---

## API Reference

### `mshtml.create(config)` → browser | nil, error

Creates a new browser control.  Returns a browser object on success, or
`nil, errorMessage` on failure.  **Always capture both return values.**

```lua
local browser, err = mshtml.create({...})
if not browser then
    print("mshtml error: " .. tostring(err))
    return
end
```

**Config table fields:**

| Field          | Type     | Default       | Description |
|----------------|----------|---------------|-------------|
| `url`          | string   | "about:blank" | Initial URL to navigate to. |
| `width`        | number   | 800           | Desired width in pixels. |
| `height`       | number   | 600           | Desired height in pixels. |
| `left`         | number   | 0             | X offset relative to parent skin window. |
| `top`          | number   | 0             | Y offset relative to parent skin window. |
| `transparent`  | boolean  | false         | Enable color-key transparency. |
| `colorKey`     | number   | 0xFF00FF      | RGB color used as transparent key. |
| `silent`       | boolean  | true          | Suppress script error dialog boxes. |
| `insideSkin`   | boolean  | true          | Clip control to the parent skin bounds. |
| `padding`      | table    | `{0,0,0,0}`   | `{left, top, widthReduction, heightReduction}` |
| `cornerRadius` | number   | 0             | Rounded corner radius (pixels). |
| `callback`     | function | `nil`         | Event handler. See [Event System](#event-system). |

---

### `browser:navigate(url)`

Navigates to the given URL.  Navigation history is not recorded (`navNoHistory`
flag is set), so `back()` will not return to the previous page.

---

### `browser:loadHTML(html)`

Replaces the current document with the provided HTML string.  Must be called
from within the `documentcomplete` callback (after the document is open) for
reliable results.

```lua
callback = function(ev)
    if ev.type == "documentcomplete" then
        browser:loadHTML("<html><body>Hello</body></html>")
    end
end
```

---

### `browser:write(html)`

Appends HTML to the current document using `document.write()`.  The document
must already be in a writable state.

---

### `browser:writeline(html)`

Same as `write()`, but appends a newline (`document.writeln()`).

---

### `browser:execScript(script)`

Executes a JavaScript string in the context of the current document via
`IHTMLWindow2::execScript()`.  The script runs synchronously.

```lua
browser:execScript("document.getElementById('count').innerText = '42';")
```

---

### `browser:resize(width, height)`

Updates the control's dimensions.  The new size is subject to `insideSkin`
clipping and padding reduction, same as the initial layout.

---

### `browser:setTransparent(enable [, colorKey])`

Enables or disables color-key transparency at runtime.  If `colorKey` is
provided it overrides the one set at creation time.

---

### `browser:getURL()` → string | nil

Returns the current location URL as a UTF-8 string, or `nil` if unavailable.

---

### `browser:back()` / `browser:forward()`

Navigate backward or forward in the session history.

---

### `browser:refresh()`

Reloads the current page.

---

### `browser:stop()`

Cancels any ongoing navigation or download.

---

### `browser:reposition()`

Forces the popup window to recalculate and reapply its position and size based
on the current parent window location.  Useful after the skin's position changes
programmatically.

---

### `browser:quit()`

Destroys the browser control, disconnects the event sink, removes the parent
subclass hook, and releases all COM interfaces.  The browser object becomes
inert after this call.  Also called automatically by the Lua `__gc` metamethod
when the browser table is garbage-collected.

---

## Event System

The `callback` function receives a single table argument:

| Field       | Type   | Present when                    |
|-------------|--------|---------------------------------|
| `type`      | string | Always.                         |
| `title`     | string | Only for `"titlechange"` events.|
| `timestamp` | number | Always. `GetTickCount64()` ms.  |

**Event types:**

| `type`              | Fires when                                        |
|---------------------|---------------------------------------------------|
| `"documentcomplete"`| The document (and all frames) have finished loading. |
| `"navigatecomplete"`| Top-level navigation to a new URL has committed. |
| `"titlechange"`     | The document `<title>` element changed.          |

Events are buffered on the COM sink thread and dispatched on the Lua thread
during `ProcessMessages()`, which is called every Rainmeter Update cycle.
Callbacks must complete quickly — they block the Update cycle for their duration.

---

## Transparency

Color-key transparency works at two layers:

1. **Window layer:** `SetLayeredWindowAttributes(hwndPopup, colorKey, 0, LWA_COLORKEY)`
   makes every pixel of the popup window that matches `colorKey` fully
   transparent to the desktop compositor.

2. **Document layer:** On `DocumentComplete`, the module injects
   `background-color: transparent` into the document's root element via
   `IHTMLStyle::put_backgroundColor`.  This lets Trident render its own
   background as the key color (inherited from the popup window's transparency),
   so only your content pixels are visible.

The parent skin window also gets `WS_EX_LAYERED` + `LWA_COLORKEY` applied,
ensuring that the portion of the skin window beneath the browser popup is also
transparent.

> **Note:** Pure per-pixel alpha (like a PNG with partial transparency) is not
> supported.  Only the exact key color is made transparent — anti-aliased edges
> will have a fringe.

---

## Layout & Padding

The popup window position is derived from the parent skin's screen rectangle:

```
finalX = parent.left + ctrl.left + padLeft
finalY = parent.top  + ctrl.top  + padTop
finalW = ctrl.width  - padWidth
finalH = ctrl.height - padHeight
```

If `insideSkin` is `true`, the rectangle is first clipped to the parent window
bounds before padding is applied.  The popup then tracks the parent via a
`SetWindowSubclass` hook on `WM_WINDOWPOSCHANGED` and `WM_MOVE`.

Rounded corners are applied via `SetWindowRgn` with a rounded rectangle region
on every reposition.

---

## Limitations

These are inherent constraints of the approach, not implementation bugs:

**Rendering engine**
- Trident is frozen at IE11 level.  No updates, no security patches for new web
  standards.  Treat it as a local-content renderer, not a general-purpose browser.
- Cross-origin `XMLHttpRequest` is blocked (same-origin policy enforced by
  Trident).  Load remote resources only through `navigate()` or via `<script
  src>` tags in your HTML.

**Transparency**
- No per-pixel alpha.  Color-key only.  Anti-aliased text edges will alias
  against the key color.
- Only one color key per skin instance.  All browser controls in the same skin
  share the parent window's color key.

**Events**
- Only three events are surfaced (`documentcomplete`, `navigatecomplete`,
  `titlechange`).  Mouse events, scroll events, and JS `postMessage` are not
  bridged to Lua.  Use `execScript()` to query page state, or encode data in
  the URL via `navigate()` and intercept `NavigateComplete`.

**Control lifecycle**
- The `storedCtrl` pointer captured in Lua closures is stable only as long as
  the `ctx->controls` `unordered_map` does not rehash.  Creating more than ~8
  browser controls without the `reserve(64)` fix risks silent pointer
  invalidation.  See [Known Bugs & Fixes](#known-bugs--fixes-v1x).

**Windows version**
- Requires Windows 7 or later (ATL, `SetWindowSubclass`).
- On Windows 11 22H2+, `iexplore.exe` is retired, but the WebBrowser OCX
  (`ieframe.dll`) and MSHTML (`mshtml.dll`) remain present and functional.
  The control itself is not removed; only the IE shell was retired.
- Not compatible with Windows on ARM (WoA) 64-bit native processes unless the
  Rainmeter build targets x86 or x86-64.

**Threading**
- All Lua callbacks run on the Rainmeter skin thread.  Do not call blocking
  Win32 APIs or long-running Lua code from within a callback.

---

## Known Bugs & Fixes (v1.x)

### `create` returns `nil` — checklist

1. **Capture the error message:**
   ```lua
   local browser, err = mshtml.create({...})
   assert(browser, "mshtml.create failed: " .. tostring(err))
   ```

2. **Remove `binding.cpp` from the build.**  It defines duplicate symbols that
   conflict with `mshtml.cpp`.  The linker may silently resolve to the broken
   version, producing a stale binary where `l_create` references missing struct
   fields and always fails.

3. **Check `AtlAxWinInit` success.**  If the ATL DLL version does not match the
   compiler runtime, the `AtlAxWin` window class is never registered and
   `CreateWindowEx` fails with `ERROR_CLASS_NOT_REGISTERED`.  The error string
   returned by `create` will say `"CreateWindowEx failed. GetLastError=1407"`.

4. **Check the FEATURE_BROWSER_EMULATION registry key.**  Some Windows GPO
   settings lock down the `IWebBrowser2` interface for non-IE processes.

### Dangling `storedCtrl` after multiple `create()` calls

Every Lua method closure captures a raw `Control*` into the `ctx->controls`
`unordered_map`.  When a second (or later) `mshtml.create()` triggers a map
rehash, the pointer becomes dangling.  Methods on the first browser object
silently no-op or crash.

**Fix** — add this line immediately after the `Context` is first constructed
in `l_create` (just after `ctx->hiddenWindow = CreateHiddenWindow()`):

```cpp
ctx->controls.reserve(64);
```

This pre-allocates 64 buckets and prevents rehashing for the typical number of
controls a skin will create.

### `l_resize` ignored (original `binding.cpp`)

The original `binding.cpp::l_resize` passed a `RECT*` to
`IOleObject::SetExtent()`, which expects `SIZEL*` (`{cx, cy}`).  Because
`RECT::left` and `RECT::top` are both 0, the extent was always set to
`(0, 0)` — effectively ignored.  The fixed version in the updated `binding.cpp`
uses `SetWindowPos` on the popup window directly, which is the correct approach
for a non-embedded control.

---

## License

GPL v2.0.  See `LICENSE` file in the repository root.