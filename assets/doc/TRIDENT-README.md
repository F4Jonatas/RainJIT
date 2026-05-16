<div align="center">

  # Module Trident

  ### A legacy WebBrowser within Rainmeter<br>focused on performance and low resource consumption.

  <br>
  <br>


  <img src="../images/trident-logo.png" alt="LOGO" height="200">

</div>



## Summary

<details>

<summary><ins>Table of contents</ins></summary>

- [Overview](#overview)
- [Quick Example](#jigsaw-quick-example)
- [Architecture](#architecture)
- [The IE7 Trident Engine](#the-ie7-trident-engine)
- [Path Resolution](#path-resolution)
- [API Reference](#api-reference)
- [Event System](#event-system)
- [JavaScript Bridge](#javascript-bridge)
- [Link Navigation](#link-navigation)
- [Transparency](#transparency)
- [Layout & Padding](#layout--padding)
- [Sanitization](#sanitization)
- [Limitations](#limitations)
- [License](#scroll-license)

</details>


<br>
<br>


<br>
<br>


## Overview

Trident is a lightweight embedded browser module for RainJIT built around the legacy Windows WebBrowser control. It allows Rainmeter skins to render real-time HTML, CSS, and JavaScript directly inside the Rainmeter process without external runtimes such as Chromium or WebView2.

The module is designed for simple desktop UI workloads where low memory usage, fast startup, and minimal system overhead matter more than modern web compatibility. All browser communication and event dispatching run on Rainmeter's existing STA thread without external processes or background worker threads.

Trident also includes optional HTML sanitization, JavaScript-to-Lua communication, navigation interception, and transparent layered window support, making it suitable for lightweight widgets, overlays, and interactive desktop components.


<br>
<br>

## :green_book: Features

- **JavaScript → Lua Bridge**: Post named events from JS via `window.external.notify(name, data)`
- **Link Interception**: Return `false` in the callback to cancel navigation, or let it proceed
- **HTML Sanitization**: Gumbo-based pipeline with granular `allow_*` flags per control
- **Color-Key Transparency**: `WS_EX_LAYERED` with automatic CSS injection on `DocumentComplete`
- **Rounded Corners**: Native `SetWindowRgn` via `cornerRadius` option
- **Show / Hide**: Toggle the popup without destroying it via `browser:show()` / `browser:hide()`
- **Immediate Events**: Dispatched via `PostMessage` without waiting for the Update tick
- **Layout Constraints**: Skin clipping, padding reduction and position tracking via `SetWindowSubclass`
- **Path Resolution**: `url`, `navigate()`, and `execScript()` all accept Rainmeter variables, `./` relative paths, and absolute paths
- **Local Resource Access**: Controlled via `allow_local` sanitize flag — relative paths inside HTML/CSS resolve automatically when permitted
- **Rich Event System**: Full `DWebBrowserEvents2` coverage including progress, errors, popup control, and command state

<br>
<br>


## :jigsaw: Quick Example

```lua
local trident = require("webview.trident")

local browser = trident.create({
    url          = "./index.html",   -- relative to skin directory
    width        = 400,
    height       = 300,
    left         = 0,
    top          = 0,
    transparent  = true,
    colorKey     = 0xFF00FF,
    insideSkin   = true,
    silent       = true,
    cornerRadius = 10,
    hide         = false,
    sanitize     = false,

    callback = function( browser, event )
        if event.type == "documentcomplete" then
            browser:write([[
                <html>
                <body style="background:transparent; color:white; font-family:sans-serif">
                    <h2>Hello from Lua!</h2>
                </body>
                </html>
            ]])
        end

        if event.type == "navigateerror" then
            print("Failed to load: " .. tostring(event.data) .. " code: " .. tostring(event.statusCode))
        end
    end
})
```

---


<br>
<br>


## Architecture

### COM Threading Model

COM is initialized using `OleInitialize` on the first `trident.create()` call in a given Rainmeter instance. This initializes both COM and the OLE subsystem in apartment-threaded (STA) mode, which is required for `IWebBrowser2`, `DWebBrowserEvents2`, and in-place activation to work correctly. All COM calls happen on the Rainmeter skin thread — there is no worker thread.

> **Note:** Using `CoInitializeEx` instead of `OleInitialize` causes periodic ~1 second freezes because MSHTML internally depends on OLE services that are only initialized by `OleInitialize`.

### Event Delivery

`EventSink::Invoke()` is called by Trident on the skin thread (same STA). It pushes a `MshtmlEvent` struct into a `std::queue` protected by `std::mutex`. `ProcessMessages()`, called from Rainmeter's Update cycle, drains the queue and calls the Lua callback. Because everything runs on the skin thread, no cross-thread Lua calls are ever made.

The `BeforeNavigate2`, `NewWindow2`, and `WindowClosing` events are dispatched **synchronously** (not via the queue) so that the Lua callback's return value can set the COM `Cancel` flag before MSHTML reads it. See [Link Navigation](#link-navigation).

### Window Lifecycle

Each browser occupies a `WS_POPUP` window (not a child window). This is necessary because Rainmeter's skin window uses `WS_EX_LAYERED` with `UpdateLayeredWindow`, and child windows cannot be layered independently — they inherit the parent's alpha. The popup floats above the parent and tracks its position via a `SetWindowSubclass` hook on `WM_WINDOWPOSCHANGED`.

### JavaScript Bridge

`IDocHostUIHandler::GetExternal()` is connected via `ICustomDoc::SetUIHandler` on every `DocumentComplete` event. This exposes an `IDispatch` object to JavaScript as `window.external`, enabling bidirectional communication between the page and Lua. See [JavaScript Bridge](#javascript-bridge).

### Local Security Manager

An `IInternetSecurityManager` is provided to MSHTML via `IServiceProvider` on the `WebBrowserSite`. When `allow_local` is active (or `sanitize = false`), the security manager returns `URLZONE_LOCAL_MACHINE` and `URLPOLICY_ALLOW` for `file://` URLs under the skin directory. This allows HTML and CSS files loaded via `navigate()` or `write()` to reference local assets with relative paths (`./assets/img.png`, `../shared/style.css`).

---

## The IE7 Trident Engine

Trident uses MSHTML (also called Trident), the HTML/CSS rendering engine that powered Internet Explorer. Understanding its capabilities and quirks is essential for writing skins that work reliably.

### What version of Trident do you get?

By default, `IWebBrowser2` renders in IE7 compatibility mode regardless of the Windows version. This is a deliberate Microsoft default — it prevents old ActiveX hosts from breaking.

To unlock modern rendering, you have two options:

#### 1. Per‑page (recommended for skins)

Include a `<meta>` tag at the very beginning of your `<head>`:

```html
<!doctype html>
<html>
  <head>
    <meta http-equiv="X-UA-Compatible" content="IE=edge">
    <meta http-equiv="content-type" content="text/html;charset=utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    …
  </head>
  …
</html>
```

- `IE=edge` tells Trident to use the **highest available** document mode (usually IE11 Edge mode, value `11001`).
- This works without any registry modification and affects only that page.
- Put it before any other element — the parser needs to see it early to switch modes.

#### 2. Process‑wide (fallback)

Declare a `FEATURE_BROWSER_EMULATION` registry key under:

```
HKCU\Software\Microsoft\Internet Explorer\Main\FeatureControl\FEATURE_BROWSER_EMULATION
ProcessName.exe = <DWORD value>
```

Common values:

| IE version emulated | Value  |
|---------------------|--------|
| IE7 (default)       | 7000   |
| IE8                 | 8000   |
| IE9                 | 9000   |
| IE10                | 10000  |
| IE11                | 11000  |
| IE11 (Edge mode)    | 11001  |

---

### What Trident can do — default vs. modern mode

#### A. In default IE7 mode

- HTML 4.01 / CSS 2.1 layout (block, inline, float, positioned elements).
- JScript 5.x (roughly ES3), no `JSON` object, no `Array.prototype.forEach`.
- `document.write()`, `innerHTML`, `execScript()` — reliable.
- No `XMLHttpRequest` (only legacy `ActiveXObject("Microsoft.XMLHTTP")`).
- No `localStorage` / `sessionStorage`.
- No SVG, no `@font-face`, no CSS transitions or animations.

#### B. With `IE=edge` (IE11 Edge mode)

In addition to everything above, you gain:

- Full ES5 JavaScript: `Array.prototype.forEach`, `Object.keys`, `Function.prototype.bind`, strict mode, `JSON.parse/stringify`, `Date.now()`, etc.
- `XMLHttpRequest` (same-origin only).
- `localStorage` and `sessionStorage`.
- Partial SVG support (basic shapes, paths, filters).
- Custom fonts via `@font-face` (TrueType/OpenType).
- CSS transitions and basic CSS animations.
- Basic CSS3: `border-radius`, `box-shadow`, `text-shadow`, `opacity`, `rgba()` colors.
- `querySelector` / `querySelectorAll`.
- `classList` API.

> **Note:** Even in Edge mode, Trident is **not** a modern evergreen browser. ES6+ features (`let`, `const`, arrow functions, Promises, `fetch`, template literals) still require a transpiled polyfill bundle.

---

### What Trident cannot do (even with `IE=edge`)

- **CSS Grid**, CSS Custom Properties (`--var`), `calc()` with mixed units.
- **ES6+** unless polyfilled via Babel/TypeScript targeting ES5.
- **WebSocket**, **WebRTC**, **WebGL**.
- **Service Workers**, Web Workers, or any thread API.
- `<video>` / `<audio>` with modern codecs (H.264 may work via DirectShow; VP8/9 and AV1 will not).
- `FormData`, `FileReader`, `Blob` constructor, `URL.createObjectURL`.
- `MutationObserver` (only deprecated `DOMSubtreeModified` events are available).

---

<br>
<br>


## Path Resolution

All functions that accept a URL or file path — `url` in `create()`, `navigate()`, and `execScript()` — share the same resolution logic:

| Input form | Example | Resolved as |
|---|---|---|
| Absolute URL with scheme | `https://example.com` | Passed through unchanged |
| `file://` URL | `file:///C:/skins/foo.html` | Passed through unchanged |
| `./` relative path | `./web/index.html` | Expanded to `#CURRENTPATH#web\index.html` then canonicalized |
| Rainmeter variable | `#@#web\index.html` | Variables expanded, then canonicalized |
| Bare filename | `index.html` | Resolved via `RmPathToAbsolute` |

Resolved local paths are automatically converted to `file:///` URLs for `navigate()` and `create()`. For `execScript()`, if the resolved path points to an existing file, the file is read and its contents are executed as the script.

### Relative paths inside HTML and CSS

When navigating to a `file://` URL, MSHTML resolves relative references (`./img.png`, `../style.css`) relative to the document's directory automatically — this is standard browser behavior.

When using `browser:write(html)`, the document has no origin URL. The module automatically injects a `<base href="file:///skinpath/">` tag into the `<head>` so that relative references inside the written HTML resolve correctly against the skin directory.

> **Security:** Local resource resolution respects `sanitizeFlags`. The `IInternetSecurityManager` only permits `file://` access under the skin directory when `allow_local` is set or `sanitize = false`.

---

<br>
<br>


## API Reference

### `trident.create(config)` → browser | nil, error

Creates a new browser control. Returns a browser object on success, or `nil, errorMessage` on failure. **Always capture both return values.**

```lua
local browser, err = trident.create({...})
if not browser then
    print("trident error: " .. tostring(err))
    return
end
```

**Config table fields:**

| Field          | Type     | Default         | Description |
|----------------|----------|-----------------|-------------|
| `url`          | string   | `"about:blank"` | Initial URL. Accepts `./`, Rainmeter variables, and absolute paths. |
| `width`        | number   | `800`           | Desired width in pixels. |
| `height`       | number   | `600`           | Desired height in pixels. |
| `left`         | number   | `0`             | X offset relative to parent skin window. |
| `top`          | number   | `0`             | Y offset relative to parent skin window. |
| `transparent`  | boolean  | `false`         | Enable color-key transparency. |
| `colorKey`     | number   | `0xFF00FF`      | RGB color used as transparent key. |
| `silent`       | boolean  | `true`          | Suppress script error dialog boxes. |
| `insideSkin`   | boolean  | `true`          | Clip control to the parent skin bounds. |
| `hide`         | boolean  | `false`         | Create the window initially hidden. |
| `padding`      | table    | `{0,0,0,0}`     | `{left, top, widthReduction, heightReduction}` |
| `cornerRadius` | number   | `0`             | Rounded corner radius in pixels. |
| `sanitize`     | any      | `true`          | HTML sanitization mode. See [Sanitization](#sanitization). |
| `callback`     | function | `nil`           | Event handler. See [Event System](#event-system). |

---

### `browser:navigate(url)`

Navigates to the given URL. Accepts `./` relative paths, Rainmeter variables, and absolute paths. When `VALIDATE_URLS` is active (default), the URL is validated before navigation. Navigation history is not recorded (`navNoHistory` flag), so `back()` will not return to the previous page.

```lua
browser:navigate("./pages/settings.html")
browser:navigate("#@#web\\index.html")
browser:navigate("https://example.com")
```

---

### `browser:write(html)`

Appends HTML to the current document using `document.write()`. The document must already be in a writable state (call after `documentcomplete` on `about:blank`).

A `<base href>` tag pointing to the skin directory is automatically injected into the `<head>` if one is not already present, so that relative paths inside the HTML resolve correctly.

```lua
callback = function(browser, event)
    if event.type == "documentcomplete" then
        browser:write([[
            <html><head><meta http-equiv="X-UA-Compatible" content="IE=edge"></head>
            <body>
                <img src="./assets/logo.png">
            </body></html>
        ]])
    end
end
```

---

### `browser:writeline(html)`

Same as `write()`, but appends a newline (`document.writeln()`).

---

### `browser:execScript(script)` → value | nil

Executes JavaScript in the context of the current document. If `script` is a path to an existing file (accepts `./`, Rainmeter variables, and absolute paths), the file is read and its contents are executed. Otherwise the string is executed directly.

Returns the script's result value. Booleans, numbers, `null`, and `undefined` are converted to their Lua equivalents. Strings are returned as-is.

```lua
-- Inline script
local count = browser:execScript("document.querySelectorAll('li').length")

-- Script from file
browser:execScript("./js/init.js")
browser:execScript("#@#js\\app.js")

-- Return value types
-- JS: true/false        → Lua: boolean
-- JS: 42 / 3.14        → Lua: integer / number
-- JS: "hello"          → Lua: string
-- JS: null / undefined → Lua: nil
```

> **Note:** `execScript` bypasses `sanitizeFlags` by design — Lua callers are considered trusted.

---

### `browser:show()` / `browser:hide()`

Shows or hides the browser popup window without destroying it. The control remains alive and all COM interfaces stay connected.

```lua
browser:hide()
-- ... later ...
browser:show()
```

---

### `browser:setTransparent(enable [, colorKey])`

Enables or disables color-key transparency at runtime. If `colorKey` is provided it overrides the one set at creation time.

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

Forces the popup window to recalculate and reapply its position and size based on the current parent window location. Useful after the skin's position changes programmatically.

---

### `browser:document()`

Returns a fresh table with document-level helpers: `write`, `writeln`, `getTitle`. Returns a new table on every call so the caller always gets the current COM document state.

```lua
local doc = browser:document()
doc:write("<p>hello</p>")
print(doc:getTitle())
```

---

### `browser:window()`

Returns a table with window-level helpers. Sub-method: `eval(script)`, which is an alias for `execScript`.

```lua
browser:window():eval("alert('hi')")
```

---

### `browser:quit()`

Destroys the browser control, disconnects the event sink, removes the parent subclass hook, and releases all COM interfaces. The browser object becomes inert after this call. Also called automatically by the Lua `__gc` metamethod when the browser table is garbage-collected.

---

<br>
<br>


## Event System

The `callback` function receives two arguments: the browser object itself and an event table.

```lua
callback = function(browser, event)
    print(event.type, event.data)
end
```

> **Note:** The first argument `browser` is the same object returned by `trident.create()`. It is injected automatically — you do not need to capture it via closure.

**Common event table fields:**

| Field       | Type   | Present when                             |
|-------------|--------|------------------------------------------|
| `type`      | string | Always.                                  |
| `timestamp` | number | Always. `GetTickCount64()` milliseconds. |
| `data`      | any    | Type-dependent. See table below. When the value is a valid Lua table literal, it is automatically decoded — otherwise it remains a string. |

**Full event reference:**

| `type`                  | Fires when | Extra fields |
|-------------------------|------------|--------------|
| `"documentcomplete"`    | Document and all frames finished loading. | — |
| `"navigatecomplete"`    | Top-level navigation committed. | — |
| `"titlechange"`         | `<title>` element changed. | `title` (string) |
| `"navigate"`            | Link clicked or `window.location` changed. **Synchronous — return `false` to cancel.** | `data` (URL string) |
| `"statustextchange"`    | Status bar text changed. | `data` (string) |
| `"progresschange"`      | Download progress updated. | `progress` (number), `progressMax` (number) |
| `"downloadbegin"`       | Download or navigation started. | — |
| `"downloadcomplete"`    | Download or navigation finished. | — |
| `"navigateerror"`       | Navigation failed. | `data` (URL string), `statusCode` (number) |
| `"newwindow"`           | Popup window requested. **Synchronous — return `false` to cancel.** | — |
| `"windowclosing"`       | `window.close()` called. **Synchronous — return `false` to cancel.** | — |
| `"commandstatechange"`  | Back/forward availability changed. | `command` (number), `enabled` (boolean) |
| *custom name*           | JS called `window.external.notify(name, data)`. | `data` (string or table) |

**Cancelable events** (`"navigate"`, `"newwindow"`, `"windowclosing"`) are dispatched **synchronously** — they do not go through the event queue. The Lua callback's return value is read immediately to set the COM cancel flag.

```lua
callback = function(browser, event)
    -- Block all popups
    if event.type == "newwindow" then
        return false
    end

    -- Open external links in system browser
    if event.type == "navigate" and event.data:find("^https?://") then
        os.execute('start "" "' .. event.data .. '"')
        return false
    end

    -- Track progress
    if event.type == "progresschange" then
        print(event.progress .. "/" .. event.progressMax)
    end

    -- Handle navigation errors
    if event.type == "navigateerror" then
        print("Error " .. event.statusCode .. " loading " .. event.data)
    end
end
```

---

<br>
<br>


## JavaScript Bridge

The module exposes a `window.external` object to JavaScript via `IDocHostUIHandler::GetExternal()`, registered through `ICustomDoc::SetUIHandler` on every `DocumentComplete`. This allows JavaScript running inside the browser to post named events with a string payload back to Lua.

```js
// JavaScript (inside your HTML)
window.external.notify("myEvent", JSON.stringify({ value: 42 }));
```

```lua
-- Lua callback
callback = function(browser, event)
    if event.type == "myEvent" then
        -- event.data is automatically decoded to a table when possible
        print(event.data.value)  -- 42
    end
end
```

**Signature:** `window.external.notify(name, data)`

| Parameter | Type   | Description                                              |
|-----------|--------|----------------------------------------------------------|
| `name`    | string | Becomes `event.type` in the Lua callback.                |
| `data`    | any    | Coerced to string. Decoded to a Lua table automatically if the value is valid Lua syntax — otherwise kept as string. |

> **Tip:** Use `JSON.stringify()` on the JS side. Simple JSON objects (`{"key":"value"}`) are valid Lua table syntax and will be decoded automatically. Arrays (`[1,2,3]`) are not decoded and remain strings — use a JSON library for those.

---

<br>
<br>


## Link Navigation

By default, when the user clicks a link (or JavaScript changes `window.location`), the browser navigates internally. The module intercepts every navigation via `BeforeNavigate2` and fires a `"navigate"` event **synchronously** to the Lua callback. The callback's return value decides what happens:

| Return value              | Effect |
|---------------------------|--------|
| `false`                   | Navigation cancelled. Browser stays on current page. |
| `true`, `nil`, or nothing | Navigation proceeds normally inside the browser. |

```lua
callback = function(browser, event)
    if event.type == "navigate" then
        os.execute('start "" "' .. event.data .. '"')
        return false
    end
end
```

`event.data` contains the target URL as a UTF-8 string.

> **Note:** The initial `about:blank` navigation is always allowed and does not fire a `"navigate"` event.

---

<br>
<br>


## Transparency

Color-key transparency works at two layers:

1. **Window layer:** `SetLayeredWindowAttributes(hwndPopup, colorKey, 0, LWA_COLORKEY)` makes every pixel of the popup window that matches `colorKey` fully transparent to the desktop compositor.

2. **Document layer:** On `DocumentComplete`, the module injects `background-color: transparent` into the document's `<body>` element via `IHTMLStyle::put_backgroundColor`. This lets Trident render its own background as the key color, so only your content pixels are visible.

The parent skin window also gets `WS_EX_LAYERED` + `LWA_COLORKEY` applied, ensuring that the portion of the skin window beneath the browser popup is also transparent.

> **Note:** Pure per-pixel alpha (like a PNG with partial transparency) is not supported. Only the exact key color is made transparent — anti-aliased edges will have a fringe.

> **Tip:** Pick a color that never appears intentionally in your HTML. Pure magenta (`#FF00FF`) or pure cyan (`#00FFFF`) are common choices.

---

## Layout & Padding

The popup window position is derived from the parent skin's screen rectangle:

```
finalX = parent.left + ctrl.left + padLeft
finalY = parent.top  + ctrl.top  + padTop
finalW = ctrl.width  - padWidth
finalH = ctrl.height - padHeight
```

If `insideSkin` is `true`, the rectangle is first clipped to the parent window bounds before padding is applied. The popup then tracks the parent via a `SetWindowSubclass` hook on `WM_WINDOWPOSCHANGED` and `WM_MOVE`.

Rounded corners are applied via `SetWindowRgn` with a rounded rectangle region on every reposition.

---

## Sanitization

HTML sanitization is applied to `write()` content before it reaches the browser. It is a Gumbo-based pipeline controlled per control via the `sanitize` option.

| Value   | Behavior |
|---------|----------|
| `true`  | `SANITIZE_ALL` — blocks scripts, events, dangerous CSS, and validates URLs (default). |
| `false` | `SANITIZE_NONE` — no filtering. Use when you control all HTML content. |
| table   | Starts from `SANITIZE_ALL` and clears specific flags via string tokens. |

**Allow tokens:**

```lua
sanitize = { "allow_scripts", "allow_events", "allow_style", "allow_css", "allow_urls", "allow_local" }
```

| Token              | Effect |
|--------------------|--------|
| `"allow_scripts"`  | Permit `<script>`, `<iframe>`, `<object>`, `<embed>` and their children. |
| `"allow_events"`   | Permit `on*` event handler attributes (`onclick`, `onload`, etc.). |
| `"allow_style"`    | Pass `style` attribute through entirely (implies `allow_css`). |
| `"allow_css"`      | Keep `style` attribute but skip dangerous CSS declaration filtering. |
| `"allow_urls"`     | Skip URL validation on `href`/`src`/`action` attributes. |
| `"allow_local"`    | Permit `file://` URLs and enable local resource loading from the skin directory. Required for `./` relative paths inside HTML/CSS to work when `sanitize` is not `false`. |

> **Note:** `execScript()` bypasses sanitization entirely — Lua callers are considered trusted.

---

## Limitations

These are inherent constraints of the approach, not implementation bugs:

**Rendering engine**
- Trident is frozen at IE11 level. No updates, no security patches for new web standards. Treat it as a local-content renderer, not a general-purpose browser.
- Cross-origin `XMLHttpRequest` is blocked (same-origin policy enforced by Trident).

**Transparency**
- No per-pixel alpha. Color-key only. Anti-aliased text edges will alias against the key color.
- Only one color key per skin instance. All browser controls in the same skin share the parent window's color key.

**Control lifecycle**
- The `storedCtrl` pointer captured in Lua closures is stable only as long as the `ctx->controls` `unordered_map` does not rehash. Creating more than ~8 browser controls without the `reserve(64)` fix risks silent pointer invalidation.

**Windows version**
- Requires Windows 7 or later (ATL, `SetWindowSubclass`).
- On Windows 11 22H2+, `iexplore.exe` is retired, but `ieframe.dll` and `mshtml.dll` remain present and functional.
- Not compatible with Windows on ARM (WoA) 64-bit native processes unless the Rainmeter build targets x86 or x86-64.

**Threading**
- All Lua callbacks run on the Rainmeter skin thread. Do not call blocking Win32 APIs or long-running Lua code from within a callback.

**JavaScript bridge data**
- `event.data` is automatically decoded to a Lua table only for simple JSON objects. JSON arrays and nested structures remain as strings and require an explicit JSON decoder.

---

<br>
<br>


## :scroll: License

GPL v2.0. See `LICENSE` file in the repository root.

<br>
