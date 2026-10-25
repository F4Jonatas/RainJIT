<div align="center">

  # Module Trident

  ### A lightweight web browser integrated into Rainmeter, based on Internet Explorer 7 and designed to display simple pages with low resource consumption.

  <br>
  <br>


  <img src="../images/trident-logo.png" alt="LOGO" height="200">

</div>



## Summary

<details>

<summary><ins>Table of contents</ins></summary>

- [Overview](#overview)
- [Features](#green_book-features)
- [Quick Example](#jigsaw-quick-example)
- [Architecture](#architecture)
- [The IE7 Trident Engine](#the-ie7-trident-engine)
- [Path Resolution](#path-resolution)
- [API Reference](#api-reference)
- [Theme and Data Attributes](#theme-and-data-attributes)
- [Event System](#event-system)
- [JavaScript Bridge](#javascript-bridge)
- [Link Navigation](#link-navigation)
- [Transparency](#transparency)
- [Layout & Padding](#layout--padding)
- [Sanitization](#sanitization)
- [Security](#security)
- [Limitations](#limitations)
- [Source Layout](#source-layout)
- [License](#scroll-license)

</details>

<br>
<br>


## Overview

Trident is a lightweight embedded browser module for RainJIT built around the legacy Windows WebBrowser control. It allows Rainmeter skins to render real-time HTML, CSS, and JavaScript directly inside the Rainmeter process without external runtimes such as Chromium or WebView2.

The module is designed for simple desktop UI workloads where low memory usage, fast startup, and minimal system overhead matter more than modern web compatibility. All browser communication and event dispatching run on Rainmeter's existing STA thread without external processes or background worker threads.

Trident also includes optional HTML sanitization, JavaScript-to-Lua communication, navigation interception, a light/dark theme with custom attributes, and transparent layered window support, making it suitable for lightweight widgets, overlays, and interactive desktop components.

<br>
<br>


## :green_book: Features

- **JavaScript ⇄ Lua Bridge**: Post events from JS with `window.external.call(name, data)` and expose Lua functions to the page with `browser:bind(name, fn)`
- **Link Interception**: Return `false` from the `"navigate"` callback to cancel a navigation, or let it proceed
- **Navigation Policy**: Every navigation (links, `location=`, forms, redirects, frames) goes through a URL validator; `navigation = "locked"` freezes the top level
- **HTML Sanitization**: Gumbo-based pipeline with granular `allow_*` flags per control
- **Theme and Data Attributes**: `theme` (`light` / `dark` / Windows) and a `data` table written as `data-*` attributes on `<html>`, updated at run time with `browser:redraw()`
- **Color-Key Transparency**: `WS_EX_LAYERED` with the body painted in the key color on `DocumentComplete`
- **Rounded Corners**: Native `SetWindowRgn` via `cornerRadius` option
- **Show / Hide**: Toggle the popup without destroying it via `browser:show()` / `browser:hide()`
- **Immediate Events**: Dispatched through a hidden window message, without waiting for the Update tick
- **Layout Constraints**: Skin clipping, padding reduction, position tracking and z-order tracking via `SetWindowSubclass`
- **Path Resolution**: `url`, `navigate()`, and `execScript()` accept Rainmeter variables, `./` relative paths, `host:port`, and absolute paths
- **Live DOM Access**: `browser.document` and `browser.window` expose the COM objects of the page to Lua
- **Rich Event System**: Progress, errors, popup control, downloads, and command state
- **Process Hardening**: VBScript is blocked, ActiveX/downloads/credentials/cookies are denied by default, drops are filtered

<br>
<br>


## :jigsaw: Quick Example

```lua
local trident = require("webview.trident")

local browser, err = trident.create({
  url          = "./index.html", -- relative to skin directory
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
  theme        = "dark",                         -- or omit to follow Windows
  data         = { accent = "red", zoom = 1.5 }, -- <html data-accent="red" data-zoom="1.5">

  callback = function( browser, event )
    if event.type == "documentcomplete" then
      browser:write( [[
        <html>
          <body style="background:#FF00FF; color:white; font-family:sans-serif">
            <h2>Hello from Lua!</h2>
          </body>
        </html>
      ]] )
    end

    if event.type == "navigateerror" then
      print("Failed to load: " .. tostring( event.data ) .. " code: " .. tostring( event.statusCode ))
    end
  end
})

if not browser then
  print("trident error: " .. tostring( err ))
end
```

---

<br>
<br>


## Architecture

### COM Threading Model

COM is initialized using `OleInitialize` on the first `trident.create()` call in the process. This initializes both COM and the OLE subsystem in apartment-threaded (STA) mode, which is required for `IWebBrowser2`, `DWebBrowserEvents2`, and in-place activation to work correctly. All COM calls happen on the Rainmeter skin thread — there is no worker thread.

> **Note:** Using `CoInitializeEx` instead of `OleInitialize` causes periodic ~1 second freezes because MSHTML internally depends on OLE services that are only initialized by `OleInitialize`.

### Event Delivery

`EventSink::Invoke()` is called by Trident on the skin thread (same STA). Ordinary events are pushed into a bounded queue (at most 512 events and 2 MB of text per skin) and a message is posted to a hidden message-only window, which drains the queue and calls the Lua callback right away. `ProcessMessages()`, called from Rainmeter's Update cycle, drains the queue as a fallback. Because everything runs on the skin thread, no cross-thread Lua calls are ever made.

The `BeforeNavigate2`, `NewWindow2/3`, and `WindowClosing` events are dispatched **synchronously** (not via the queue) so that the Lua callback's return value can set the COM `Cancel` flag before MSHTML reads it. See [**Link Navigation**](#link-navigation).

### Re-entrancy

Lua callbacks run inside COM events and inside nested message loops (`write()`, `execScript()`). Calling `quit()` from such a callback does not free anything on the spot: the control is disarmed (every later method call is a no-op), its callbacks stop, and the real teardown runs from a posted message once no such frame is left on the stack. `navigate()`, `back()`, `forward()`, `refresh()`, `write()` and `redraw()` called from inside a browser event are queued the same way and run right after the event. `write()` then returns `true, "deferred"`.

### Window Lifecycle

Each browser occupies a `WS_POPUP` window owned by the skin window (not a child window). This is necessary because Rainmeter's skin window uses `WS_EX_LAYERED` with `UpdateLayeredWindow`, and child windows cannot be layered independently — they inherit the parent's alpha.

The popup follows the skin through a `SetWindowSubclass` hook (position, size, visibility). Its z-order is always **relative to the skin**: it is kept directly above the skin window and takes over the skin's topmost state, whatever the skin's z-position is (Normal, On Desktop, Topmost...) and whenever Rainmeter changes it. Clicking inside the browser brings the skin to the front without activating it, and the popup is placed above it in the same step, so the skin never covers the browser.

### JavaScript Bridge

`window.external` is provided through `IDocHostUIHandler::GetExternal()`, installed before the page is parsed and again on `DocumentComplete`. It enables bidirectional communication between the page and Lua. See [**JavaScript Bridge**](#javascript-bridge).

### Local Security Manager

An `IInternetSecurityManager` is provided to MSHTML via `IServiceProvider` on the browser site. It maps `file://` URLs under the skin directory to the local-machine zone and allows only a small set of actions for them, so HTML, CSS and scripts of the skin can reference local assets with relative paths (`./assets/img.png`, `../shared/style.css`). Dangerous actions (ActiveX, shell execute, Java, control downloads) are denied for every URL unless `allowActiveX = true`.

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
| :--:                | :--:   |
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
- JScript 5.x (roughly ES3), no `JSON` object, no `Array.prototype.forEach`, and no `window.eval` (`browser:execScript()` works around this).
- `document.write()`, `innerHTML`, `execScript()` — reliable.
- Attribute selectors such as `[data-theme="dark"]` — supported, which is what the theme feature relies on.
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

- **CSS Grid**, **CSS Custom Properties** (`var(--x)`), `light-dark()`, `prefers-color-scheme`, `calc()` with mixed units. For themes and per-skin values use [data attributes](#theme-and-data-attributes).
- **ES6+** unless polyfilled via Babel/TypeScript targeting ES5.
- **WebSocket**, **WebRTC**, **WebGL**.
- **Service Workers**, Web Workers, or any thread API.
- `<video>` / `<audio>` with modern codecs (H.264 may work via DirectShow; VP8/9 and AV1 will not).
- `FormData`, `FileReader`, `Blob` constructor, `URL.createObjectURL`.
- `MutationObserver` (only deprecated `DOMSubtreeModified` events are available).
- **VBScript** — always blocked by the module (see [**Security**](#security)).

---

<br>
<br>


## Path Resolution

All functions that accept a URL or file path — `url` in `create()`, `navigate()`, and `execScript()` (for `.js` files) — share the same resolution logic:

| Input form               | Example                     | Resolved as                                                         |
| :--:                     | :--:                        | :--:                                                                |
| Absolute URL with scheme | `https://example.com`       | Passed through unchanged                                            |
| `file://` URL            | `file:///C:/skins/foo.html` | Passed through unchanged                                            |
| `host:port`              | `127.0.0.1:8080/app`        | `http://127.0.0.1:8080/app`                                         |
| Drive or UNC path        | `C:\skins\foo.html`         | Converted to a `file:///` URL                                       |
| `./` relative path       | `./web/index.html`          | Expanded to `#CURRENTPATH#web\index.html`, then made absolute       |
| Rainmeter variable       | `#@#web\index.html`         | Variables expanded, then made absolute                              |
| Bare filename            | `index.html`                | Resolved relative to the skin like any Rainmeter path               |

Local paths are converted to properly percent-encoded `file:///` URLs. A trailing `?query` or `#fragment` is kept as URL syntax unless the whole string names an existing file. Non-ASCII characters are left as they are.

### Relative paths inside HTML and CSS

When navigating to a `file://` URL, MSHTML resolves relative references (`./img.png`, `../style.css`) relative to the document's directory automatically — this is standard browser behavior.

`browser:write(html)` always writes into an `about:blank` document, which has no base URL. Relative references inside written HTML therefore do not resolve against the skin folder: use absolute `file:///` URLs (permitted by `allow_local` or `sanitize = false`), or put the page in a file and use `navigate()`.

> **Security:** Local access is limited to the skin folder. See [**Security**](#security).

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

The control lives until `browser:quit()` or until the skin unloads; keeping the return value is optional.

**Config table fields:**

| Field              | Type     | Default         | Description                                                                                                                  |
| :--:               | :--:     | :--:            | :--:                                                                                                                         |
| `url`              | string   | `"about:blank"` | Initial URL. Accepts `./`, Rainmeter variables, `host:port` and absolute paths.                                              |
| `width`            | number   | `800`           | Desired width in pixels.                                                                                                     |
| `height`           | number   | `600`           | Desired height in pixels.                                                                                                    |
| `left`             | number   | `0`             | X offset relative to the parent skin window.                                                                                 |
| `top`              | number   | `0`             | Y offset relative to the parent skin window.                                                                                 |
| `transparent`      | boolean  | `false`         | Enable color-key transparency.                                                                                               |
| `colorKey`         | number   | `0xFF00FF`      | `0xRRGGBB` color used as the transparent key.                                                                                |
| `silent`           | boolean  | `true`          | Suppress script error dialog boxes.                                                                                          |
| `insideSkin`       | boolean  | `true`          | Clip the control to the parent skin bounds.                                                                                  |
| `hide`             | boolean  | `false`         | Create the window initially hidden.                                                                                          |
| `padding`          | table    | `{0,0,0,0}`     | `{left, top, widthReduction, heightReduction}`                                                                               |
| `cornerRadius`     | number   | `0`             | Rounded corner radius in pixels.                                                                                             |
| `contextMenu`      | boolean  | `true`          | `false` suppresses the browser context menu (the right click is forwarded to the skin).                                      |
| `theme`            | string   | Windows theme   | `"light"` or `"dark"`. See [**Theme and Data Attributes**](#theme-and-data-attributes).                                      |
| `data`             | table    | `nil`           | `{ name = value }`, written as `data-name="value"` on `<html>`. See [**Theme and Data Attributes**](#theme-and-data-attributes). |
| `sanitize`         | any      | `true`          | HTML sanitization mode. See [**Sanitization**](#sanitization).                                                               |
| `allowedHosts`     | table    | `nil`           | `{ "cdn.example.com", "*.example.com" }`: remote sub-resources of sanitized content may only load from these hosts (`{}` blocks all). |
| `reservedNames`    | table    | `nil`           | Extra `id` / `name` values dropped from sanitized HTML (e.g. your script's globals).                                         |
| `navigation`       | string   | `"free"`        | `"locked"` blocks every top-level navigation not requested by Lua. See [**Link Navigation**](#link-navigation).              |
| `cache`            | boolean  | `false`         | `true` uses the WinINet cache shared with Internet Explorer. By default navigations neither read nor write it.               |
| `external`         | boolean  | `true`          | `false` never exposes `window.external`.                                                                                     |
| `externalOrigins`  | table    | `nil`           | `{ "https://host[:port]" }`: extra remote origins allowed to use `window.external`.                                          |
| `allowDownloads`   | boolean  | `false`         | `true` does not cancel file downloads.                                                                                       |
| `allowActiveX`     | boolean  | `false`         | `true` stops denying ActiveX / shell execute / Java actions.                                                                 |
| `allowCredentials` | boolean  | `false`         | `true` allows silent NTLM/Kerberos logon.                                                                                    |
| `allowCookies`     | boolean  | `false`         | `true` allows first-party cookies (third-party cookies are always denied).                                                   |
| `callback`         | function | `nil`           | Event handler. See [**Event System**](#event-system).                                                                        |

---

### `browser:navigate(url)`

Navigates to the given URL. Accepts `./` relative paths, Rainmeter variables, `host:port` and absolute paths. The URL goes through the sanitizer's URL validation (unless `allow_urls` / `sanitize = false`) and through the unconditional navigation policy (VBScript and disallowed local files are always refused); refused navigations are logged.

```lua
browser:navigate("./pages/settings.html")
browser:navigate("#@#web\\index.html")
browser:navigate("https://example.com")
```

---

### `browser:write(html)` → true | true, "deferred" | nil, error

Replaces the page with HTML (`document.open()` + `document.write()` + `document.close()`). The HTML is passed through the sanitizer according to the control's `sanitize` option; with `sanitize = false` it is written as is, except that content mentioning VBScript is refused.

The content is always written into an `about:blank` document. If the page currently shown came from a URL or a file, the control first navigates to `about:blank` (writing into a remote document would keep its origin) and the HTML is written when it has loaded; in that case the call returns `true, "deferred"`. The same happens right after `create()`, before the first document exists.

The `documentcomplete` event caused by `write()` itself is not forwarded to the callback, so writing from inside a `documentcomplete` handler does not loop.

```lua
callback = function(browser, event)
  if event.type == "documentcomplete" and not browser.written then
    browser.written = true
    local ok, err = browser:write([[
      <html><head><meta http-equiv="X-UA-Compatible" content="IE=edge"></head>
      <body><h1>Hello</h1></body></html>
    ]])
    if not ok then print("write failed: " .. tostring(err)) end
  end
end
```

---

### `browser:execScript(script)` → value | nil, error

Runs JavaScript in the current document. The argument is the **body of a function**: use `return` to get a value back. If the argument is a single line ending in `.js`, it is treated as a file (`./`, Rainmeter variables and absolute paths are accepted) and the file's text is used as the body.

The result keeps its type: number, string, boolean, `nil` (`null` / `undefined`), or a COM proxy for objects. On failure it returns `nil, message`. It works in every document mode, including IE7/IE8 where `window.eval` does not exist.

```lua
-- Inline script (note the return)
local count = browser:execScript("return document.getElementsByTagName('li').length")

-- Script from file
browser:execScript("./js/init.js")
browser:execScript("#@#js\\app.js")

-- Return value types
-- JS: true/false       → Lua: boolean
-- JS: 42 / 3.14        → Lua: number
-- JS: "hello"          → Lua: string
-- JS: null / undefined → Lua: nil
-- JS: object           → Lua: COM proxy
```

> **Note:** `execScript` bypasses `sanitize` by design — Lua callers are considered trusted.

---

### `browser:bind(name, fn)`

Exposes a Lua function to the page as `window.external.<name>(...)`. Arguments and return value are converted between JavaScript and Lua. See [**JavaScript Bridge**](#javascript-bridge).

---

### `browser:redraw([data [, theme]])` → data, theme | nil, error

Re-applies the theme and the `data-*` attributes to the page. See [**Theme and Data Attributes**](#theme-and-data-attributes).

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

Enables or disables color-key transparency at runtime. If `colorKey` (`0xRRGGBB`) is provided it overrides the one set at creation time.

---

### `browser:setContextMenu(enable)`

Enables or disables the browser context menu at runtime.

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

Forces the popup window to recalculate and reapply its position, size and z-order based on the current parent window location. Useful after the skin's position changes programmatically.

---

### `browser.document` / `browser.window`

Live properties (no parentheses) that return the page's `document` and `window` as COM proxies, or `nil` when there is no document yet. Properties and methods are dispatched through COM automation:

```lua
local doc   = browser.document
local el    = doc.getElementById("app")   -- method  → proxy
local title = doc.title                   -- property → string
doc.title   = "New title"                 -- assignment works too
browser.window.scrollTo(0, 0)
```

---

### `browser.hwnd`

The window handle (light userdata) of the browser popup.

---

### `browser:quit()`

Destroys the browser control, disconnects the event sink, removes the subclass hooks, and releases all COM interfaces. The browser object becomes inert after this call: every method does nothing (or returns `nil, "The browser is closed"`). It is also called automatically when the browser object is garbage-collected and when the skin unloads. Called from inside a callback, it is deferred until the callback returns.

---

<br>
<br>


## Theme and Data Attributes

MSHTML supports neither CSS custom properties (`var(--x)`) nor `light-dark()`. Trident therefore exposes the theme and your own values as **attributes on the `<html>` element**, which the page can style with attribute selectors (supported even in IE7 mode).

```lua
local browser = trident.create({
  url   = "./index.html",
  theme = "dark",
  data  = { accent = "red", ["font-size"] = 14, compact = true },
})
```

The page receives:

```html
<html data-theme="dark" data-accent="red" data-font-size="14" data-compact="true">
```

```css
[data-theme="dark"]  body { background: #111; color: #eee; }
[data-theme="light"] body { background: #fff; color: #111; }
[data-accent="red"]  a    { color: red; }
[data-compact="true"] .row { padding: 2px; }
```

```js
// JavaScript can read them too
var theme = document.documentElement.getAttribute("data-theme");
```

### The `theme` option

| Value              | Effect                                                                                       |
| :--:               | :--:                                                                                         |
| `"light"`, `"dark"` | Fixed theme.                                                                                |
| omitted            | Follows the Windows app theme (*Settings → Personalization → Colors → Choose your default app mode*). Light when it cannot be read. |

### The `data` option

- Keys are strings. They are lowercased, an optional `data-` prefix is dropped, and only letters, digits, `_`, `.` and `-` are accepted (up to 64 characters, not starting with `.` or `-`).
- Values may be strings (up to 1024 characters), numbers or booleans. Everything is written as text.
- `theme` is reserved: use the `theme` option.
- At most 64 entries. Invalid entries are skipped and logged.

### When the attributes are written

As soon as the document exists (`NavigateComplete2`, so the first paint already matches the selectors), again on `DocumentComplete`, after `write()`, and on every `redraw()`.

### `browser:redraw([data [, theme]])`

| Call                       | Effect                                                                                                                              |
| :--:                       | :--:                                                                                                                                |
| `redraw()`                 | Reads the Windows theme again (when `theme` was not fixed) and writes `data-theme` and the data attributes again.                   |
| `redraw({ ... })`          | **Replaces** every data attribute by the ones in the table. The ones that are gone are removed from `<html>`. The theme is kept.    |
| `redraw({})`               | Removes every data attribute (the theme is kept).                                                                                   |
| `redraw(nil, "dark")`      | Changes the theme. `"light"`, `"dark"`, or `""` / `"auto"` to follow Windows again.                                                 |

It returns two values: a table with the data attributes now in effect (names without the `data-` prefix) and the theme in use (`"light"` or `"dark"`). With invalid arguments it returns `nil, message` and nothing changes. Called from inside a browser event, the page is updated right after the event.

```lua
-- follow the Windows theme when the user changes it
local data, theme = browser:redraw()
print(theme, data.accent)

-- new set of attributes: everything else is dropped
browser:redraw({ accent = "blue" })

-- force the dark theme
browser:redraw(nil, "dark")
```

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

> **Note:** The first argument `browser` is the same object returned by `trident.create()`. It is passed automatically — you do not need to capture it via closure.

**Common event table fields:**

| Field       | Type   | Present when                                                |
| :--:        | :--:   | :--:                                                        |
| `type`      | string | Always.                                                     |
| `timestamp` | number | Always (queued events). `GetTickCount64()` milliseconds.    |
| `data`      | string | Type-dependent, see below. Always a plain string.           |

**Full event reference:**

| `type`                 | Fires when                                                                                              | Extra fields                                       |
| :--:                   | :--:                                                                                                    | :--:                                               |
| `"documentcomplete"`   | The top-level document finished loading (frames are ignored).                                           | —                                                  |
| `"navigatecomplete"`   | Top-level navigation committed.                                                                         | —                                                  |
| `"titlechange"`        | `<title>` changed.                                                                                      | `title` (string)                                   |
| `"navigate"`           | A top-level navigation is about to happen. **Synchronous — return `false` to cancel.**                  | `data` (URL)                                       |
| `"statustextchange"`   | Status bar text changed.                                                                                | `data` (string)                                    |
| `"progresschange"`     | Download progress updated.                                                                              | `progress` (number), `progressMax` (number)        |
| `"downloadbegin"`      | Download or navigation started.                                                                         | —                                                  |
| `"downloadcomplete"`   | Download or navigation finished.                                                                        | —                                                  |
| `"navigateerror"`      | A top-level navigation failed.                                                                          | `data` (URL), `statusCode` (number)                |
| `"newwindow"`          | The page asked for a popup. **Synchronous — return `true` to allow, anything else cancels.**            | `url` (target, may be empty)                       |
| `"windowclosing"`      | `window.close()` called. **Synchronous — return `false` to cancel.**                                    | —                                                  |
| `"filedownload"`       | A file download was requested. Informational: it was already cancelled unless `allowDownloads = true`.  | —                                                  |
| `"commandstatechange"` | Back/forward (or other command) availability changed.                                                   | `command` (number), `enabled` (boolean)            |
| `"external"`           | JS called `window.external.call(name, data)`.                                                           | `name` (string), `data` (string, may be absent)    |

**Synchronous events** (`"navigate"`, `"newwindow"`, `"windowclosing"`) do not go through the queue: the callback's return value is read immediately to set the COM cancel flag. Errors raised by the callback are logged and the default action applies.

```lua
callback = function(browser, event)
  -- Popups are cancelled unless the callback returns true.
  -- An allowed popup is opened in the user's default browser (http, https and mailto only).
  if event.type == "newwindow" then
    return event.url:find("^https?://") ~= nil
  end

  -- Open external links in the system browser instead of inside the widget
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

The module exposes a `window.external` object to the page. It works in both directions.

### Page → Lua: `window.external.call(name, data)`

```js
// JavaScript (inside your HTML)
window.external.call("myEvent", JSON.stringify({ value: 42 }));
window.external.call("ready");              // one argument: an event without data
```

```lua
-- Lua callback
callback = function(browser, event)
  if event.type == "external" and event.name == "myEvent" then
    -- event.data is ALWAYS a string: decode it yourself, inside pcall
    local ok, t = pcall(json.decode, event.data)
    if ok then print(t.value) end -- 42
  end
end
```

| Parameter | Type   | Description                                                                                          |
| :--:      | :--:   | :--:                                                                                                 |
| `name`    | string | Delivered as `event.name`. The event type is always `"external"`: the page cannot spoof other types. |
| `data`    | any    | Converted to a string and delivered as `event.data` (up to 32 KB; `name` up to 256 characters).      |

> **Security:** Nothing coming from the page is ever compiled or evaluated as Lua. Decode JSON with a JSON library inside `pcall`.

### Lua → Page: `browser:bind(name, fn)`

```lua
browser:bind("getSkinValue", function(key)
  return SKIN:GetVariable(key)
end)
```

```js
var v = window.external.getSkinValue("Width");
```

Arguments and return values are converted between JavaScript and Lua. An error raised by `fn` reaches the page as a script error carrying the Lua message. The name `call` is reserved for the event channel and cannot be bound. Reading a bound member without calling it (`if (window.external.getSkinValue) ...`) yields a callable reference, so feature tests work.

### Who may use `window.external`

`window.external` is only exposed to `about:blank` documents, local files inside the skin folder, and origins listed in `externalOrigins`. The calling frame must be trusted too (a third-party iframe inside a trusted page cannot use it). `external = false` disables it completely. Origins are matched as parsed scheme/host/port: `"https://a.com"` equals `"https://A.com:443"`, never `"https://a.com.evil.com"` or `"https://a.com@evil.com"`.

---

<br>
<br>


## Link Navigation

Every navigation — clicked links, `location=`, forms, meta refresh, frames, and the **final** URL after redirects — goes through the URL validator. Navigations that fail it are cancelled (or dropped to `about:blank` for redirects) and logged.

For top-level navigations the module then fires a `"navigate"` event **synchronously** to the Lua callback, whose return value decides what happens:

| Return value              | Effect                                               |
| :--:                      | :--:                                                 |
| `false`                   | Navigation cancelled. Browser stays on current page. |
| `true`, `nil`, or nothing | Navigation proceeds normally inside the browser.     |

```lua
callback = function(browser, event)
  if event.type == "navigate" then
    os.execute('start "" "' .. event.data .. '"')
    return false
  end
end
```

`event.data` contains the target URL as a UTF-8 string. Frame navigations obey the validator but are not reported to Lua. The initial `about:blank` navigation does not fire a `"navigate"` event.

### Locking the top level

```lua
trident.create({ url = "./app.html", navigation = "locked" })
```

With `navigation = "locked"` neither the user nor the page can change the top-level document: only `navigate()`, `back()`, `forward()`, `refresh()` and the initial `url` can.

---

<br>
<br>


## Transparency

Color-key transparency works at two layers:

1. **Window layer:** `SetLayeredWindowAttributes(hwndPopup, colorKey, 0, LWA_COLORKEY)` makes every pixel of the popup window that matches `colorKey` fully transparent to the desktop compositor.

2. **Document layer:** On `DocumentComplete`, the module paints the document's `<body>` with the key color through `IHTMLStyle::put_backgroundColor` (an inline style, so it wins over the page's CSS). The layered window then punches that color out, and only your content pixels are visible.

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

If `insideSkin` is `true`, the rectangle is first clipped to the parent window bounds before padding is applied. The popup then tracks the parent via a `SetWindowSubclass` hook on `WM_WINDOWPOSCHANGED`, `WM_MOVE` and `WM_SIZE`, and stays directly above it in the z-order (see [**Window Lifecycle**](#window-lifecycle)).

Rounded corners are applied via `SetWindowRgn` with a rounded rectangle region on every reposition.

---

## Sanitization

HTML sanitization is applied to `write()` content before it reaches the browser, and its URL validation also applies to `navigate()` and `create{ url }`. It is a Gumbo-based pipeline controlled per control via the `sanitize` option. `<style>` elements are rebuilt rule by rule (no `@import`, no `filter` / `progid` / `behavior` / `expression`).

| Value   | Behavior                                                                              |
| :--:    | :--:                                                                                  |
| `true`  | `SANITIZE_ALL` — blocks scripts, events, dangerous CSS, overlays, DOM clobbering, and validates URLs (default). |
| `false` | `SANITIZE_NONE` — no filtering. Use when you control all HTML content.                |
| table   | Starts from `SANITIZE_ALL` and clears specific flags via string tokens.               |

**Allow tokens:**

```lua
sanitize = { "allow_scripts", "allow_events", "allow_style", "allow_css", "allow_urls", "allow_local", "allow_clobbering", "allow_overlay" }
```

| Token                | Effect                                                                                                                                         |
| :--:                 | :--:                                                                                                                                           |
| `"allow_scripts"`    | Permit `<script>`, `<iframe>`, `<object>`, `<embed>` and their children.                                                                      |
| `"allow_events"`     | Permit `on*` event handler attributes (`onclick`, `onload`, etc.).                                                                             |
| `"allow_style"`      | Pass the `style` attribute through entirely (implies `allow_css`).                                                                             |
| `"allow_css"`        | Keep the `style` attribute but skip dangerous CSS declaration filtering.                                                                       |
| `"allow_urls"`       | Skip URL validation on `href`/`src`/`action` attributes and on `navigate()` / `create{ url }`.                                                 |
| `"allow_local"`      | Permit `file://` URLs (only the skin folder is readable). Needed for local URLs when `sanitize` is not `false`.                                |
| `"allow_clobbering"` | Keep `id` / `name` values that shadow `document` / `window` / `form` members.                                                                  |
| `"allow_overlay"`    | Keep `position: fixed` / `sticky` and very large `z-index` values.                                                                             |

`//host` and `\\host\share` URLs are always rejected (SMB / NTLM leak).

> **Note:** `execScript()` bypasses sanitization entirely — Lua callers are considered trusted.

---

## Security

Besides the sanitizer, the module applies these protections regardless of the `sanitize` option:

- **VBScript is always blocked.** The sanitizer drops VBScript `<script>`s, the `language` attribute, `vbscript:` values and the `Content-Script-Type` meta; `write()` with `sanitize = false` is refused when the text mentions VBScript; `navigate()` / links to `vbscript:` URLs and to local files that mention VBScript (or `.vbs`, `.hta`, `.htc`, ...) are blocked; the VBScript engines cannot be instantiated in the process; and a document that still ends up with a VBScript `<script>` is replaced by `about:blank`.
- **Navigation policy** on every navigation (see [**Link Navigation**](#link-navigation)).
- **Popups** are cancelled unless the callback returns `true`; an allowed popup is opened in the default browser, and only for `http://`, `https://` and `mailto:` targets.
- **Downloads** are cancelled unless `allowDownloads = true` (the event `"filedownload"` still fires).
- **ActiveX, shell execute and Java** are denied for every URL unless `allowActiveX = true`, so `CreateObject("WScript.Shell")` fails. Skin files only receive a small allowlist.
- **Credentials and cookies:** no silent NTLM/Kerberos logon and no cookies unless `allowCredentials` / `allowCookies`; third-party cookies are never allowed.
- **Drag and drop:** dropping files or links onto the control is refused (text drops into inputs still work).
- **Cache isolation:** navigations neither read nor write the WinINet cache shared with Internet Explorer unless `cache = true`.
- **Process hardening:** Internet Feature Controls (no ActiveX install prompts, downloads need a gesture, zone elevation, `mk:` protocol, `user:pass@host` URLs, ...) are enabled process-wide on the first `create()`.
- **Bounded input:** the event queue, event texts, `window.external` names, arguments and error messages all have hard size limits.

> **Note:** The engine still runs inside the Rainmeter process and cannot be sandboxed from the module. Treat remote content accordingly.

---

## Limitations

These are inherent constraints of the approach, not implementation bugs:

**Rendering engine**
- Trident is frozen at IE11 level. No updates, no security patches for new web standards. Treat it as a local-content renderer, not a general-purpose browser.
- Cross-origin `XMLHttpRequest` is blocked (same-origin policy enforced by Trident).
- No CSS custom properties or `light-dark()`: use [data attributes](#theme-and-data-attributes).

**Transparency**
- No per-pixel alpha. Color-key only. Anti-aliased text edges will alias against the key color.

**Z-order**
- The browser is a separate top-level window owned by the skin. It is kept above the skin, but it cannot be placed between the skin and other windows, and it is always drawn over the skin's own meters.

**Windows version**
- Requires Windows 7 or later (ATL, `SetWindowSubclass`).
- On Windows 11 22H2+, `iexplore.exe` is retired, but `ieframe.dll` and `mshtml.dll` remain present and functional.
- Not compatible with Windows on ARM (WoA) 64-bit native processes unless the Rainmeter build targets x86 or x86-64.

**Threading**
- All Lua callbacks run on the Rainmeter skin thread. Do not call blocking Win32 APIs or long-running Lua code from within a callback.
- Calling `quit()`, `navigate()`, `write()` and similar from inside a callback is safe, but the effect is deferred until the callback returns.

**JavaScript bridge data**
- `event.data` of `"external"` events is always a string. Decode JSON with an explicit JSON library inside `pcall`.

---

<br>
<br>


## :scroll: License

Licensed under the **GPL v2.0 License**.

> [!NOTE]
> This module uses the legacy Internet Explorer/Trident browser engine provided by **Microsoft Windows**.
---

<br>
<br>


<div align="center">
  For more information, visit the <b><a href="../../README.md">RainJIT documentation</a></b>
</div>

<br>
<br>
