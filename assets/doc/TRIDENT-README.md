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

**Trident** is a lightweight embedded browser module for **RainJIT** built around the legacy Windows WebBrowser control. It allows **Rainmeter** skins to render real-time HTML, CSS, and JavaScript directly inside the **Rainmeter** process without external runtimes such as [**Chromium**](https://github.com/chromium/chromium/tree/main) or [**WebView2**](https://github.com/MicrosoftEdge/WebView2Browser).

The module is designed for simple desktop UI workloads where low memory usage, fast startup, and minimal system overhead matter more than modern web compatibility. All browser communication and event dispatching run on **Rainmeter** existing STA thread without external processes or background worker threads.

**Trident** also includes optional HTML sanitization, JavaScript-to-Lua communication, navigation interception, a light/dark theme with custom attributes, and transparent layered window support, making it suitable for lightweight widgets, overlays, and interactive desktop components.

<br>
<br>


## :green_book: Features

- **JavaScript ⇄ Lua Bridge**: Post events from JS with `window.external.call(name, data)` and expose Lua functions to the page with `browser:bind(name, fn)`
- **Link Interception**: Return `false` from the `"navigate"` callback to cancel a navigation, or let it proceed
- **Navigation Policy**: Every navigation (links, `location=`, forms, redirects, frames) goes through a URL validator; `navigation = "locked"` freezes the top level. `javascript:` and `vbscript:` URLs are **always** refused, whatever the flags
- **HTML Sanitization**: [**Gumbo-based**](https://github.com/google/gumbo-parser) pipeline with granular `allow_*` flags per control
- **Theme and Data Attributes**: `theme` (`light` / `dark` / Windows) and a `data` table written as `data-*` attributes on `<html>`, updated at run time with `browser:redraw()`
- **Rounded Corners**: Native `SetWindowRgn` via `cornerRadius` option
- **Show / Hide**: Toggle the popup without destroying it via `browser:show()` / `browser:hide()`
- **Immediate Events**: Dispatched through a hidden window message, without waiting for the [**Update tick**](https://docs.rainmeter.net/manual/skins/rainmeter-section/#Update)
- **Path Resolution**: `url`, `navigate()`, and `execScript()` accept **Rainmeter** variables, `./` relative paths, `host:port`, and absolute paths
- **Live DOM Access**: `browser.document` and `browser.window` expose the COM objects of the page to Lua
- **Process Hardening**: VBScript and `javascript:` URLs are blocked, ActiveX/downloads/credentials/cookies are denied by default, drops are filtered

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
      browser:write([[
        <html>
          <body style="background:#FF00FF; color:white; font-family:sans-serif">
            <h2>Hello from Lua!</h2>
          </body>
        </html>
      ]])
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


## :book: Module `webview.trident`

### :large_orange_diamond: Method `trident.create()`

Creates a new browser control. Returns a browser object on success, or `nil`, `errorMessage` on failure. **Always capture both return values.**

> [!NOTE]
> The control lives until `browser:quit()` or until the skin unloads; keeping the return value is optional.

```lua
-- @usage trident.create(config)
-- @param (table) config - Creation options.
-- @return (table|nil) Browser object, or `nil` on failure.
-- @return (string|nil) Error message, present only when the first value is `nil`.
-- @raise bad argument #1 to 'create' (table expected, got <type>) when config is not a table.
-- @see https://github.com/F4Jonatas/RainJIT/blob/main/assets/doc/TRIDENT-README.md#diamond_shape_with_a_dot_inside-property-create-fileds

local browser, err = trident.create({...})
if not browser then
  print("trident error: " .. tostring(err))
  return
end
```

<br>


### :large_orange_diamond: Method `browser:navigate()`

Navigates to the given URL. Accepts `./` relative paths, **Rainmeter** variables, `host:port` and absolute paths. The URL goes through the sanitizer's URL validation (unless `allow_urls` / `sanitize = false`) and through the unconditional navigation policy: **`javascript:` URLs (and `jscript:`, `ecmascript:`, `livescript:`, `mocha:`), `vbscript:` URLs, UNC paths and disallowed local files are always refused**, whatever the flags. Refused navigations are only logged (see [**Security**](#security)); the page stays as it is.

```lua
-- @usage browser:navigate(url)
-- @param (string) url - Address to load: `./` relative path, Rainmeter variable, `host:port`, absolute path or URL.
-- @return nothing. A refused or unresolvable URL is only logged. Called from inside a browser event, the navigation runs right after the event.
-- @raise bad argument #1 to 'navigate' (string expected, got <type>) when `url` is not a string (numbers are converted).

browser:navigate("./pages/settings.html")
browser:navigate("#@#web\\index.html")
browser:navigate("https://example.com")
```

<br>


### :large_orange_diamond: Method `browser:write()`

Replaces the page with HTML (`document.open()` + `document.write()` + `document.close()`). The HTML is passed through the sanitizer according to the control's `sanitize` option; with `sanitize = false` it is written as is, except that content mentioning VBScript is refused.

The content is always written into an `about:blank` document. If the page currently shown came from a URL or a file, the control first navigates to `about:blank` (writing into a remote document would keep its origin) and the HTML is written when it has loaded; in that case the call returns `true, "deferred"`. The same happens right after `create()`, before the first document exists.

The `documentcomplete` event caused by `write()` itself is not forwarded to the callback, so writing from inside a `documentcomplete` handler does not loop.

```lua
-- @usage browser:write(html)
-- @param (string) html - HTML document or fragment. Filtered according to the `sanitize` option.
-- @return (boolean|nil) `true` when written or queued; `nil` on failure.
-- @return (string|nil) `"deferred"` when the HTML will be written once `about:blank` has loaded; the error message when the first value is `nil` (`"The browser is closed"`, `"VBScript is blocked"`, ...).
-- @raise `bad argument #1 to 'write' (string expected, got <type>)` when `html` is not a string (numbers are converted).

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

<br>


### :large_orange_diamond: Method `browser:execScript()`

Runs JavaScript in the current document. The argument is the **body of a function**: use `return` to get a value back. If the argument is a single line ending in `.js`, it is treated as a file (`./`, **Rainmeter** variables and absolute paths are accepted) and the file's text is used as the body.

The result keeps its type: number, string, boolean, `nil` (`null` / `undefined`), or a COM proxy for objects. On failure it returns `nil, message`. It works in every document mode, including IE7/IE8 where `window.eval` does not exist.

```lua
-- @usage browser:execScript(script)
-- @param (string) script - Body of a JavaScript function (use `return`), or a single line ending in `.js` naming a file (`./`, Rainmeter variables and absolute paths).
-- @return (string|number|boolean|table|nil) The value returned by the script: string, number, boolean, a COM proxy for objects, `nil` for `null` / `undefined`.
-- @return (string|nil) Error message, only when the call failed (`"The browser is closed"`, `"Script file not found"`, `"Failed to read script file"` or the script's own error); the first value is then `nil`.
-- @raise `bad argument #1 to 'execScript' (string expected, got <type>)` when `script` is not a string (numbers are converted).

-- Inline script (note the return)
local count = browser:execScript("return document.getElementsByTagName('li').length")

-- Script from file
browser:execScript("./js/init.js")
browser:execScript("#@#js\\app.js")

-- Return value types
-- JS: true or false     → Lua: boolean
-- JS: 42 or 3.14        → Lua: number
-- JS: "hello"           → Lua: string
-- JS: null or undefined → Lua: nil
-- JS: object            → Lua: COM proxy
```

> **Note:** `execScript` bypasses `sanitize` by design — Lua callers are considered trusted.

<br>


### :large_orange_diamond: Method `browser:bind()`

Exposes a Lua function to the page as `window.external.<name>(...)`. Arguments and return value are converted between JavaScript and Lua.


> [!NOTE]
> An error raised by `fn` reaches the page as a script error carrying the Lua message. The name `call` is reserved for the event channel and cannot be bound. Reading a bound member without calling it (`if (window.external.getSkinValue) ...`) yields a callable reference, so feature tests work.

> [!IMPORTANT]
> **Who may use `window.external`**
> `window.external` is only exposed to `about:blank` documents, local files inside the skin folder, and origins listed in `externalOrigins`. The calling frame must be trusted too (a third-party iframe inside a trusted page cannot use it). `external = false` disables it completely. Origins are matched as parsed scheme/host/port: `"https://a.com"` equals `"https://A.com:443"`, never `"https://a.com.evil.com"` or `"https://a.com@evil.com"`.

```lua
-- @usage browser:bind(name, fn)
-- @param (string) name - Member name exposed to the page as `window.external.<name>`. It must not be empty and cannot be `call` (reserved) or another reserved name.
-- @param (function) fn - Lua function. It receives the JavaScript arguments converted to Lua and its return value is converted back to JavaScript. An error raised inside `fn` reaches the page as a script error.
-- @return nothing
-- @raise `Browser not ready or externalDispatch missing` when the browser was closed or `external = false`.
-- @raise `Failed to bind method '<name>' (empty or reserved name, or too many names)` when the name is refused.
-- @raise `bad argument ... (string expected ...)` / `(function expected ...)` when `name` or `fn` have the wrong type.

browser:bind("getSkinValue", function(key)
  return SKIN:GetVariable(key)
end)
```

```js
var v = window.external.getSkinValue("Width");
```

<br>

### JavaScript Bridge

The module exposes a `window.external` object to the page. It works in both directions.

> [!IMPORTANT]
> **Security:** Nothing coming from the page is ever compiled or evaluated as Lua. Decode JSON with a JSON library inside `pcall`.

#### Page → Lua: `window.external.call(name, data)`

```js
// JavaScript (inside your HTML)
window.external.call("myEvent", JSON.stringify({ value: 42 }));
window.external.call("ready");              // one argument: an event without data
```

```lua
-- @see https://github.com/F4Jonatas/RainJIT/blob/main/assets/doc/JSON-README.md
local JSON = require("json")

-- Lua callback
callback = function(browser, event)
  if event.type == "external" and event.name == "myEvent" then
    -- event.data is ALWAYS a string: decode it yourself, inside pcall
    local ok, t = pcall(JSON.parse, event.data)
    if ok then print(t.value) end -- 42
  end
end
```

| Parameter | Type   | Description                                                                                          |
| :--:      | :--:   | :--:                                                                                                 |
| `name`    | string | Delivered as `event.name`. The event type is always `"external"`: the page cannot spoof other types. |
| `data`    | any    | Converted to a string and delivered as `event.data` (up to 32 KB; `name` up to 256 characters).      |


<br>


### :large_orange_diamond: Method `browser:redraw()`

Re-applies the theme and the `data-*` attributes to the page.

```lua
-- @usage browser:redraw([data [, theme]])
-- @param (table|nil) data - Replaces every `data-*` attribute by the ones in the table (`{}` removes them all; the theme is kept). `nil` keeps the current ones.
-- @param (string|nil) theme - `"light"`, `"dark"`, or `""` / `"auto"` to follow the Windows theme. `nil` keeps the current choice.
-- @return (table|nil) The data attributes now in effect (names without the `data-` prefix), or `nil` when an argument is invalid.
-- @return (string) The theme in use (`"light"` or `"dark"`), or the error message when the first value is `nil`.
-- @raise nothing: invalid arguments are reported as `nil, message` and nothing changes.

-- follow the Windows theme when the user changes it
local data, theme = browser:redraw()
print(theme, data.accent)

-- new set of attributes: everything else is dropped and the theme is kept
browser:redraw({ accent = "blue" })

-- force the dark theme; the attributes are kept
browser:redraw(nil, "dark")
```

<br>


### :large_orange_diamond: Method `browser:show() · browser:hide()`

Shows or hides the browser popup window without destroying it. The control remains alive and all COM interfaces stay connected.

```lua
-- @usage browser:show()
-- @usage browser:hide()
-- @return nothing
-- @raise nothing

browser:hide()
-- ... later ...
browser:show()
```

<br>


### :large_orange_diamond: Method `browser:quit()`

Destroys the browser control, disconnects the event sink, removes the subclass hooks, and releases all COM interfaces. The browser object becomes inert after this call: every method does nothing (or returns `nil, "The browser is closed"`). It is also called automatically when the browser object is garbage-collected and when the skin unloads. Called from inside a callback, it is deferred until the callback returns.

```lua
-- @usage browser:quit()
-- @return nothing
-- @raise nothing
```

<br>


### :large_orange_diamond: Method `browser:setTransparent()`

Enables or disables color-key transparency at runtime. If `colorKey` (`0xRRGGBB`) is provided it overrides the one set at creation time.

```lua
-- @usage browser:setTransparent(enable [, colorKey])
-- @param (boolean) enable - `true` enables the transparency, `false` disables it (any other value follows Lua truthiness).
-- @param (number|nil) colorKey - `0xRRGGBB` key color. Omitted or not a number: the current key color is kept.
-- @return nothing
-- @raise nothing
```
<br>


### :large_orange_diamond: Method `browser:setContextMenu()`

Enables or disables the browser context menu at runtime.

```lua
-- @usage browser:setContextMenu(enable)
-- @param (boolean) enable - `true` shows the browser context menu, `false` suppresses it (the right click is forwarded to the skin). Other types are ignored.
-- @return nothing
-- @raise nothing
```
<br>


### :large_orange_diamond: Method `browser:getURL()`

Returns the current location URL as a UTF-8 string, or `nil` if unavailable.

```lua
-- @usage browser:getURL()
-- @return (string|nil) Current location URL (UTF-8), or `nil` when the browser is closed or has no location.
-- @raise nothing
```

<br>


### :large_orange_diamond: Method `browser:back() · browser:forward()`

Navigate backward or forward in the session history.

```lua
-- @usage browser:back()
-- @usage browser:forward()
-- @return nothing. Called from inside a browser event, the navigation runs right after the event.
-- @raise nothing
```

<br>


### :large_orange_diamond: Method `browser:refresh()`

Reloads the current page.

```lua
-- @usage browser:refresh()
-- @return nothing. Called from inside a browser event, the reload runs right after the event.
-- @raise nothing
```

<br>


### :large_orange_diamond: Method `browser:stop()`

Cancels any ongoing navigation or download.

```lua
-- @usage browser:stop()
-- @return nothing
-- @raise nothing
```

<br>


### :large_orange_diamond: Method `browser:reposition()`

Forces the popup window to recalculate and reapply its position, size and z-order based on the current parent window location. Useful after the skin's position changes programmatically.

```lua
-- @usage browser:reposition()
-- @return nothing
-- @raise nothing
```

<br>


### :diamond_shape_with_a_dot_inside: Property Create fileds

> [!IMPORTANT]
> **Transparency — Color-key**<br>
> Pure per-pixel alpha (like a PNG with partial transparency) is not supported. Only the exact key color is made transparent — anti-aliased edges will have a fringe.<br><br>
> **Tip:** Pick a color that never appears intentionally in your HTML. Pure magenta (`#FF00FF`) or pure cyan (`#00FFFF`) are common choices.

| Field              | Type     | Default         | Description                                                                                                                  |
| :--:               | :--:     | :--:            | :--:                                                                                                                         |
| `url`              | string   | `"about:blank"` | Initial URL. Accepts `./`, **Rainmeter** variables, `host:port` and absolute paths.                                          |
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

<br>


### :diamond_shape_with_a_dot_inside: Property `browser.document` · `browser.window`

Live properties (no parentheses) that return the page's `document` and `window` as COM proxies, or `nil` when there is no document yet. Properties and methods are dispatched through COM automation:

```lua
-- @field document (table|nil) COM proxy of the page's `document`; `nil` when there is no document yet.
-- @field window   (table|nil) COM proxy of the page's `window`; `nil` when there is no document yet.

local doc   = browser.document
local el    = doc.getElementById("app")   -- method  → proxy
local title = doc.title                   -- property → string
doc.title   = "New title"                 -- assignment works too
browser.window.scrollTo(0, 0)
```

<br>


### :diamond_shape_with_a_dot_inside: Property `browser.hwnd`

The window handle (light userdata) of the browser popup.

```lua
-- @field hwnd (userdata) HWND of the popup window.
```

---

<br>
<br>


## Theme and Data Attributes

MSHTML supports neither CSS custom properties (`var(--x)`) nor `light-dark()`. **Trident** therefore exposes the theme and your own values as **attributes on the `<html>` element**, which the page can style with attribute selectors (supported even in IE7 mode).

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

> [!IMPORTANT]
> `javascript:` URLs (and `jscript:`, `ecmascript:`, `livescript:`, `mocha:`) and `vbscript:` URLs are **always** refused, independently of `sanitize` and `allow_urls`. The check looks at the URL scheme only, so `https://site.com/?q=javascript:x` is fine. A link such as `<a href="javascript:void(0)">` therefore does nothing: use `onclick` (with `allow_events` when the HTML is sanitized) instead. `execScript()` is not affected.

### Locking the top level

```lua
trident.create({ url = "./app.html", navigation = "locked" })
```

With `navigation = "locked"` neither the user nor the page can change the top-level document: only `navigate()`, `back()`, `forward()`, `refresh()` and the initial `url` can.

---

<br>
<br>


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
| **Rainmeter** variable   | `#@#web\index.html`         | Variables expanded, then made absolute                              |
| Bare filename            | `index.html`                | Resolved relative to the skin like any **Rainmeter** path           |

Local paths are converted to properly percent-encoded `file:///` URLs. A trailing `?query` or `#fragment` is kept as URL syntax unless the whole string names an existing file. Non-ASCII characters are left as they are.

### Relative paths inside HTML and CSS

When navigating to a `file://` URL, MSHTML resolves relative references (`./img.png`, `../style.css`) relative to the document's directory automatically — this is standard browser behavior.

`browser:write(html)` always writes into an `about:blank` document, which has no base URL. Relative references inside written HTML therefore do not resolve against the skin folder: use absolute `file:///` URLs (permitted by `allow_local` or `sanitize = false`), or put the page in a file and use `navigate()`.

> **Security:** Local access is limited to the skin folder. See [**Security**](#security).

---

<br>
<br>


## Sanitization

HTML sanitization is applied to `write()` content before it reaches the browser, and its URL validation also applies to `navigate()` and `create{ url }`. It is a Gumbo-based pipeline controlled per control via the `sanitize` option. `<style>` elements are rebuilt rule by rule (no `@import`, no `filter` / `progid` / `behavior` / `expression`).

| Value   | Behavior                                                                              |
| :--:    | :--:                                                                                  |
| `true`  | `SANITIZE_ALL` — blocks scripts, events, dangerous CSS, overlays, DOM clobbering, and validates URLs (default). |
| `false` | `SANITIZE_NONE` — no filtering. Use when you control all HTML content. VBScript, `javascript:` URLs and UNC paths are still refused. |
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
| `"allow_urls"`       | Skip URL validation on `href`/`src`/`action` attributes and on `navigate()` / `create{ url }`. `javascript:`, `vbscript:` and UNC URLs are still refused. |
| `"allow_local"`      | Permit `file://` URLs (only the skin folder is readable). Needed for local URLs when `sanitize` is not `false`.                                |
| `"allow_clobbering"` | Keep `id` / `name` values that shadow `document` / `window` / `form` members.                                                                  |
| `"allow_overlay"`    | Keep `position: fixed` / `sticky` and very large `z-index` values.                                                                             |

`javascript:` / `vbscript:` URLs and `//host` / `\\host\share` URLs are always rejected (the latter leak NTLM hashes over SMB).

> **Note:** `execScript()` bypasses sanitization entirely — Lua callers are considered trusted.

---

<br>
<br>


## Security

Besides the sanitizer, the module applies these protections regardless of the `sanitize` option:

- **VBScript is always blocked.** The sanitizer drops VBScript `<script>`s, the `language` attribute, `vbscript:` values and the `Content-Script-Type` meta; `write()` with `sanitize = false` is refused when the text mentions VBScript; `navigate()` / links to `vbscript:` URLs and to local files that mention VBScript (or `.vbs`, `.hta`, `.htc`, ...) are blocked; the VBScript engines cannot be instantiated in the process; and a document that still ends up with a VBScript `<script>` is replaced by `about:blank`.
- **`javascript:` URLs are always blocked.** `javascript:`, `jscript:`, `ecmascript:`, `livescript:` and `mocha:` are refused in `navigate()`, `create{ url }`, clicked links, `location=`, forms, frames and redirects, whatever `sanitize` or `allow_urls` say. The URL is normalized first (entities, whitespace and control characters, `\` as `/`), so `java\tscript:` and `&#106;avascript:` are caught; only the scheme is checked. Refusals are logged as `[SECURITY] ... blocked`.
- **Navigation policy** on every navigation (see [**Link Navigation**](#link-navigation)).
- **Popups** are cancelled unless the callback returns `true`; an allowed popup is opened in the default browser, and only for `http://`, `https://` and `mailto:` targets.
- **Downloads** are cancelled unless `allowDownloads = true` (the event `"filedownload"` still fires).
- **ActiveX, shell execute and Java** are denied for every URL unless `allowActiveX = true`, so `CreateObject("WScript.Shell")` fails. Skin files only receive a small allowlist.
- **Credentials and cookies:** no silent NTLM/Kerberos logon and no cookies unless `allowCredentials` / `allowCookies`; third-party cookies are never allowed.
- **Drag and drop:** dropping files or links onto the control is refused (text drops into inputs still work).
- **Cache isolation:** navigations neither read nor write the WinINet cache shared with Internet Explorer unless `cache = true`.
- **Process hardening:** Internet Feature Controls (no ActiveX install prompts, downloads need a gesture, zone elevation, `mk:` protocol, `user:pass@host` URLs, ...) are enabled process-wide on the first `create()`.
- **Bounded input:** the event queue, event texts, `window.external` names, arguments and error messages all have hard size limits.

> **Note:** The engine still runs inside the **Rainmeter** process and cannot be sandboxed from the module. Treat remote content accordingly.

---

<br>
<br>


## The IE7 Trident Engine

**Trident** uses MSHTML (also called **Trident**), the HTML/CSS rendering engine that powered Internet Explorer. Understanding its capabilities and quirks is essential for writing skins that work reliably.<br>
By default, renders in IE7 compatibility mode regardless of the Windows version. This is a deliberate Microsoft default — it prevents old ActiveX hosts from breaking.

To unlock slightly more modern rendering, you have two options:

### 1. Per‑page (recommended for skins)

Include a `<meta>` tag at the very beginning of your `<head>`

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

- `IE=edge` tells **Trident** to use the **highest available** document mode (usually IE11 Edge mode, value `11001`).
- This works without any [**registry**](https://learn.microsoft.com/en-us/troubleshoot/windows-server/performance/windows-registry-advanced-users) modification and affects only that page.
- Put it before any other element — the parser needs to see it early to switch modes.

### 2. Process‑wide (fallback)

Declare a `FEATURE_BROWSER_EMULATION` [**registry**](https://learn.microsoft.com/en-us/troubleshoot/windows-server/performance/windows-registry-advanced-users) key under

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

<br>
<br>


## Limitations

These are inherent constraints of the approach, not implementation bugs:

**Rendering engine**
- **Trident** is frozen at IE11 level. No updates, no security patches for new web standards. Treat it as a local-content renderer, not a general-purpose browser.
- Cross-origin `XMLHttpRequest` is blocked (same-origin policy enforced by **Trident**).
- No CSS custom properties or `light-dark()`: use [data attributes](#theme-and-data-attributes).

**Transparency**
- No per-pixel alpha. Color-key only. Anti-aliased text edges will alias against the key color.

**Z-order**
- The browser is a separate top-level window owned by the skin. It is kept above the skin, but it cannot be placed between the skin and other windows, and it is always drawn over the skin's own meters.

**Windows version**
- Requires Windows 7 or later (ATL, `SetWindowSubclass`).
- On Windows 11 22H2+, `iexplore.exe` is retired, but `ieframe.dll` and `mshtml.dll` remain present and functional.
- Not compatible with Windows on ARM (WoA) 64-bit native processes unless the **Rainmeter** build targets x86 or x86-64.

**Threading**
- All Lua callbacks run on the **Rainmeter** skin thread. Do not call blocking Win32 APIs or long-running Lua code from within a callback.
- Calling `quit()`, `navigate()`, `write()` and similar from inside a callback is safe, but the effect is deferred until the callback returns.

**JavaScript bridge data**
- `event.data` of `"external"` events is always a string. Decode JSON with an explicit JSON library inside `pcall`.

---

<br>
<br>


## :scroll: License

Licensed under the **GPL v2.0 License**.

> [!NOTE]
> This module uses [**Gumbo HTML Parser**](https://github.com/google/gumbo-parser), distributed under the **Apache License 2.0**.<br>
> **Copyright 2010 Google Inc.**<br>
> The Gumbo source code and its original license are available in the accompanying third-party license notices.<br>

> [!NOTE]
> This module uses the legacy Internet Explorer/**Trident** browser engine provided by **Microsoft Windows**. It is not distributed with the module.

---

<br>
<br>


<div align="center">
  For more information, visit the <b><a href="../../README.md">**RainJIT** documentation</a></b>
</div>

<br>
<br>
