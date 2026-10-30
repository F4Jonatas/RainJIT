<div align="center">

  # Fetch Module

  ### HTTP Client for RainJIT

  <br>

  <img src="../images/fetch-logo.png" alt="LOGO" height="200">
</div>



## Summary
<details>

<summary><ins>Table of contents</ins></summary>

- [Overview](#overview)
- [Features](#green_book-features)
- [Synchronous vs Asynchronous](#vs-understanding-synchronous-vs-asynchronous)
- [Quick Example](#jigsaw-quick-example)
- [Request Options](#request-options-object)
- [Synchronous API](#large_orange_diamond-api-fetch---sync)
- [Asynchronous API](#large_orange_diamond-api-fetchasync)
- [Request Methods - _async_](#diamond_shape_with_a_dot_inside-request-object-methods---async)
  - [`:send()`](#large_orange_diamond-method-requestsend)
  - [`:hasCompleted()`](#large_orange_diamond-method-requesthascompleted)
  - [`:cancel()`](#large_orange_diamond-method-requestcancel)
- [Response Object](#diamond_shape_with_a_dot_inside-response-object)
  - [`:text()` / `:bytes()`](#large_orange_diamond-methods-responsetext--responsebytes)
  - [`:headers()` / `:cookies()`](#large_orange_diamond-methods-responseheaders--responsecookies)
  - [`:xml()` / `:html()` / `:json()`](#large_orange_diamond-methods-responsexml--responsehtml--responsejson)
  - [`:save()`](#large_orange_diamond-method-responsesave)
- [`fetch.promiseAll`](#large_orange_diamond-api-fetchpromiseall)
  - [`promise()`](#large_orange_diamond-call-promise)
  - [`:send()`](#large_orange_diamond-method-promisesend)
  - [`:cancel()`](#large_orange_diamond-method-promisecancel)
- [Examples](#jigsaw-examples)
- [Performance](#zap-performance-tips)
- [Limitations](#warning-limitations)
- [Lifecycle](#loop-lifecycle-of-synchronous-and-asynchronous-requests)
- [HTTP Method Semantics](#globe_with_meridians-http-method-semantics)
- [License](#scroll-license)

</details>

<br>
<br>


## Overview

Fetch is the HTTP client for RainJIT. It brings to **Rainmeter** something that skin creators previously had to work around: background request execution, independent response handling, and full control over both the construction of a request and its returned content. Its API follows the patterns familiar to most developers—resembling Python’s `requests` library or JavaScript’s `fetch` API—making the creation of requests intuitive from the very first line of code.

The module offers two modes of operation. Synchronous functions—such as `fetch.get`, `fetch.post`, and others—send a request and return the response within the same line; this approach is ideal for short scripts or for data that is needed immediately. Conversely, the asynchronous function, `fetch.async`, returns control to the script instantly while the request is processed on a separate thread. This allows the skin to remain responsive during network operations, preventing the **Rainmeter** process from freezing while a request is in progress.

All standard HTTP methods are supported: GET, POST, PUT, PATCH, DELETE, HEAD, and OPTIONS. Requests can include custom headers, content bodies (in text or binary format), credentials for Basic authentication, and cookies. Furthermore, an options table allows for the configuration of the HTTP version, the handling of redirects, and the choice of whether the URL should be sent exactly as written or percent-encoded into an ASCII-compatible format.

The response is a lightweight object that provides comprehensive details about the outcome: whether the operation was successful, the HTTP status code, descriptions of any errors, as well as headers and cookies (with support for case-insensitive lookups). The response body is stored only once—as text or binary, depending on its content type—without being duplicated in memory. From this same object, you can read the raw response body, decode it using `response:json()`, `response:xml()`, or `response:html()`, or save it directly to a file using `response:save()`—making the downloading of images, compressed files, and other binary data a task that requires just a single line of code. Failures are indicated by specific negative status codes, ensuring that a network issue is never confused with a server-side error.

Behind the scenes, two network mechanisms are available. WinHTTP is the default: it is fast, predictable, and respects the current Windows user's proxy settings. WinINet can be selected for specific requests and shares the user's cookies, proxies, and sessions—which is useful for websites that reject generic clients. Secure connections negotiate TLS protocols (from 1.0 to 1.3), redirects never downgrade a connection from HTTPS to HTTP, and logs never expose keys, credentials, or query strings. Requests are terminated along with the skin to which they belong, ensuring that no processes remain running in the background when a skin is unloaded.

<br>
<br>


## :green_book: Features

- **Dual API**: Synchronous methods for simplicity, asynchronous for performance
- **Immediate Cancellation**: `:cancel()` aborts a blocked network call right away and guarantees the callback never runs
- **Lightweight Objects**: the request and the response are userdata that share one metatable (no table and no closures are created per response)
- **Binary Support**: Perfect for images, files, and any binary data
- **`fetch.promiseAll`**: run several requests and get **one** callback with all the responses, in order (like JavaScript's `Promise.allSettled`)
- **Flexible Timeouts**: Phase-specific timeouts (DNS, connect, send, receive) plus a total deadline
- **Modern TLS**: TLS 1.0 through 1.3 negotiation flags enabled
- **Identity Encoding by Default**: requests uncompressed responses (`Accept-Encoding: identity`) unless you set your own `Accept-Encoding` header — this avoids a known WinHTTP issue where automatic gzip/deflate decompression breaks chunked responses
- **Cookie Management**: Automatic cookie extraction from responses
- **JSON / XML / HTML Parsing**: `response:json()`, `response:xml()` and `response:html()` call the matching RainJIT module
- **Dual Driver**: WinHTTP (default) or WinINet (`driver = "wininet"`) — see [Request Options](#request-options-object)

---

<br>
<br>


## :vs: Understanding Synchronous vs Asynchronous

Synchronous methods are your **go-to tool for simple, fast, sequential operations**. They shine in initialization, configuration loading, quick API checks, and any scenario where code simplicity outweighs the need for parallelism. Keep them fast (<500ms), always handle errors, and don't hesitate to switch to async when dealing with slow or large operations.

<br>

Before we delve deeper into the subject, let's understand the main difference.

| Aspect         | Synchronous           | Asynchronous          |
| :--            | :--:                  | :--:                  |
| **Execution**  | Blocks until complete | Returns immediately   |
| **Threads**    | Runs in main thread   | Creates worker thread |
| **UI Impact**  | Can freeze if slow    | Never blocks UI       |
| **Complexity** | Simple, linear code   | Requires callbacks    |
| **Use Case**   | Quick operations      | Slow/long/simultaneous operations  |

---

<br>
<br>


## :jigsaw: Quick Example

### Sync Usage

```lua
local fetch = require("fetch")

-- Sync request equivalent to fetch.get()
local response = fetch("https://api.github.com/users/octocat")

if response.ok then
  print("Status:", response.status)
  print("Data:", response:text())
else
  print("Error:", response.error)
end
```

### Async Usage

```lua
fetch.async("https://api.github.com/users/octocat")
:send(function(response)
  if response.ok then
    print("Status:", response.status)
    print("Data:", response:text())
  else
    print("Error:", response.error)
  end
end)
```

---

<br>
<br>


## :book: Module `fetch`

### Request Options Object

The options table can be passed to any fetch function (`fetch.get`, `fetch.post`, `fetch.async`, etc.) to configure the HTTP request. Below is a detailed description of each available field.

<br>


### :diamond_shape_with_a_dot_inside: Property `method`

The HTTP method to use **for** `fetch.async`. [**You can specify any allowed method**](#globe_with_meridians-http-method-semantics).<br>
Values: (`"GET"`|`"POST"`|`"PUT"`|`"PATCH"`|`"DELETE"`|`"HEAD"`|`"OPTIONS"`)<br>
Default: `"GET"`.

> [!NOTE]
> `method` is only read by `fetch.async`. The synchronous helpers (`fetch.get`, `fetch.post`, ...) always use the method in their own name.


> [!NOTE]
> All timeout values are in milliseconds. A phase‑specific timeout that is zero or omitted falls back to `timeout`. If the resulting phase value is below 1000 ms, a built‑in default is used instead (WinHTTP only): DNS 5 s, connect 10 s, send 30 s, receive 60 s.<br><br>
> `timeout` itself is clamped to a minimum of 1000 ms (`timeout = 0` becomes 1000 ms, and a warning is written to the [**Rainmeter Log**](https://docs.rainmeter.net/manual/user-interface/about/#LogTab)). It also acts as a **total deadline** from the start of the request until the response headers arrive (DNS, connect, TLS, send and waiting for the response). When it expires, the blocked call is interrupted immediately and the request ends with status `-1` and `error = "Total operation timeout exceeded"`. Reading the body is **not** limited by it — large downloads are never cut off for taking long; there the receive timeout applies to every individual read.<br><br>
> The `body` field, when provided, is sent as‑is. For textual data, ensure proper encoding (e.g., UTF‑8) is used; binary data (e.g., from file reads) is handled transparently.<br><br>
> **Default headers (WinHTTP):** unless you provide your own, the request is sent with `User-Agent: RainJIT/1.0`, `Accept: */*` and `Accept-Encoding: identity`. Header names are compared case‑insensitively, so `user-agent` replaces the default `User-Agent` instead of being sent twice.<br><br>
> **URL handling (WinHTTP):** a URL without a scheme is treated as `http://`, and `//host/path` becomes `http://host/path`. Credentials in the URL (`https://user:pass@host/`) are sent as **Basic** authentication. An invalid URL returns status `-3`.<br><br>
> **Redirects (WinHTTP):** with `followRedirects = true` redirects are followed, but a secure (`https`) request is never silently downgraded to `http`.<br><br>
> **Proxy (WinHTTP):** the proxy configured for the current Windows user (including PAC scripts and auto‑detection) is used; older systems fall back to the machine‑wide WinHTTP proxy.<br><br>
> **Logging:** the debug log never contains credentials, query string or fragment of the URL (`https://api.example.com/data?key=SECRET` is logged as `https://api.example.com/data?...`).

<table>
  <tr>
    <td align="center" nowrap="nowrap">
      <h4>Field</h4>
    </td>
    <td align="center" nowrap="nowrap">
      <h4>Description</h4>
      <img width="900" height="1" alt="">
    </td>
  </tr>

  <tr>
    <td align="center" nowrap="nowrap">
      <h5><code>headers</code></h5>
    </td>
    <td rowspan="2">
      <p>A key–value table of HTTP headers to include in the request. Header names are case‑insensitive per the HTTP specification, but they are sent as provided. Header names are also matched case‑insensitively when the module decides whether to add its own defaults (<code>User-Agent</code>, <code>Accept</code>, <code>Accept-Encoding</code>).</p>
    </td>
  </tr>
  <tr>
    <td nowrap="nowrap">
      <b>Type:</b> <code>table</code>
      <br>
      <b>Default:</b> <code>{}</code>
      <br>
      <b>Example:</b> <pre>{ ["User-Agent"] = "MyApp/1.0", ["X-Custom"] = "value" }</pre>
    </td>
  </tr>

  <tr>
    <td align="center" nowrap="nowrap">
      <h5><code>body</code></h5>
    </td>
    <td rowspan="2">
      <p>Request body data. Can be a plain text string or binary data (e.g., file contents).<br>Used only with methods that support a body (<code>POST</code>, <code>PUT</code>, <code>PATCH</code>). Ignored by the <code>wininet</code> driver.</p>
    </td>
  </tr>
  <tr>
    <td nowrap="nowrap">
      <b>Type:</b> <code>string</code>
      <br>
      <b>Default:</b> <code>nil</code>
    </td>
  </tr>

  <tr>
    <td align="center" nowrap="nowrap">
      <h5><code>timeout</code></h5>
    </td>
    <td rowspan="2">
      <p>Overall request timeout in milliseconds. If a phase‑specific timeout (see below) is not set, this value is used as a fallback for that phase, and it is also used as the total deadline of the request. The minimum effective value is 1000 ms.</p>
    </td>
  </tr>
  <tr>
    <td nowrap="nowrap">
      <b>Type:</b> <code>integer</code>
      <br>
      <b>Default:</b> <code>30000</code>
    </td>
  </tr>

  <tr>
    <td align="center" nowrap="nowrap">
      <h5><code>dnsTimeout</code></h5>
    </td>
    <td rowspan="2">
      <p>Timeout for DNS resolution in milliseconds.<br>If zero or omitted, falls back to the value of timeout.</p>
    </td>
  </tr>
  <tr>
    <td nowrap="nowrap">
      <b>Type:</b> <code>integer</code>
      <br>
      <b>Default:</b> <code>0</code> (uses <code>timeout</code>)
    </td>
  </tr>

  <tr>
    <td align="center" nowrap="nowrap">
      <h5><code>connectTimeout</code></h5>
    </td>
    <td rowspan="2">
      <p>Timeout for establishing the TCP connection (and the TLS handshake for HTTPS) in milliseconds. Falls back to <code>timeout</code> if zero.</p>
    </td>
  </tr>
  <tr>
    <td nowrap="nowrap">
      <b>Type:</b> <code>integer</code>
      <br>
      <b>Default:</b> <code>0</code> (uses <code>timeout</code>)
    </td>
  </tr>

  <tr>
    <td align="center" nowrap="nowrap">
      <h5><code>sendTimeout</code></h5>
    </td>
    <td rowspan="2">
      <p>Timeout for sending the request data in milliseconds. Falls back to <code>timeout</code> if zero.</p>
    </td>
  </tr>
  <tr>
    <td nowrap="nowrap">
      <b>Type:</b> <code>integer</code>
      <br>
      <b>Default:</b> <code>0</code>(uses <code>timeout</code>)
    </td>
  </tr>

  <tr>
    <td align="center" nowrap="nowrap">
      <h5><code>receiveTimeout</code></h5>
    </td>
    <td rowspan="2">
      <p>Timeout for receiving the response data in milliseconds. Falls back to <code>timeout</code> if zero.</p>
    </td>
  </tr>
  <tr>
    <td nowrap="nowrap">
      <b>Type:</b> <code>integer</code>
      <br>
      <b>Default:</b> <code>0</code> (uses <code>timeout</code>)
    </td>
  </tr>

  <tr>
    <td align="center" nowrap="nowrap">
      <h5><code>followRedirects</code></h5>
    </td>
    <td rowspan="2">
      <p>If <code>true</code>, the client automatically follows HTTP redirects (3xx status codes). If <code>false</code>, the original redirect response is returned as‑is.</p>
    </td>
  </tr>
  <tr>
    <td nowrap="nowrap">
      <b>Type:</b> <code>boolean</code>
      <br>
      <b>Default:</b> <code>true</code>
    </td>
  </tr>

  <tr>
    <td align="center" nowrap="nowrap">
      <h5><code>driver</code></h5>
    </td>
    <td rowspan="2">
      <p>Selects the underlying HTTP stack for this request. <code>"wininet"</code> inherits the IE/Edge session context (cookies, proxy, cached credentials) from the current Windows user profile, which some sites (e.g. Cloudflare‑protected endpoints) accept more readily than a plain WinHTTP client.</p>
      <p><strong>WinINet caveats:</strong> only <code>GET</code> is supported — other methods log a warning and are still sent as <code>GET</code>, and <code>body</code> is ignored; the default <code>User-Agent</code> is a Chrome browser string (override it with a <code>User-Agent</code> header); phase‑specific timeouts (<code>dnsTimeout</code>, <code>connectTimeout</code>, <code>sendTimeout</code>, <code>receiveTimeout</code>) are not supported and are ignored with a warning — only <code>timeout</code> is honoured.</p>
      <p>An unrecognized value logs a warning and falls back to <code>"winhttp"</code>. The driver is fixed for the lifetime of a single request — it is <u>not</u> switched automatically at runtime.</p>
    </td>
  </tr>
  <tr>
    <td nowrap="nowrap">
      <b>Type:</b> <code>string</code>
      <br>
      <b>Default:</b> <code>"winhttp"</code>
      <br>
      <b>Allowed Values:</b>
      <ul>
        <li><code>winhttp</code></li>
        <li><code>wininet</code></li>
      </ul>
    </td>
  </tr>

  <tr>
    <td align="center" nowrap="nowrap">
      <h5><code>httpVersion</code></h5>
    </td>
    <td rowspan="2">
      <p>Controls which HTTP protocol version to use for the request. Which can resolve issues with chunked encoding on certain APIs (like <a href="https://open-meteo.com/"><b>open-meteo.com</b></a>).</p>
      <p><strong>Why this matters:</strong> Some APIs return responses with <code>Transfer-Encoding: chunked</code> which can cause problems with certain HTTP stacks. Forcing HTTP/1.0 disables chunked encoding and ensures compatibility.</p>
    </td>
  </tr>
  <tr>
    <td nowrap="nowrap">
      <b>Type:</b> <code>string</code>
      <br>
      <b>Default:</b> <code>"1.1"</code>
      <br>
      <b>Allowed Values:</b>
      <ul>
        <li><code>1.0</code></li>
        <li><code>1.1</code></li>
      </ul>
    </td>
  </tr>

  <tr>
    <td align="center" nowrap="nowrap">
      <h5><code>onlyASCII</code></h5>
    </td>
    <td rowspan="2">
      <p>Controls how the URL is sent. With <code>false</code> (default) the URL is sent exactly as written, so APIs that expect real characters (<code>São Paulo</code>, <code>café</code>, spaces) receive them untouched.</p>
      <p>With <code>true</code>, spaces and every non‑ASCII character <b>after the host</b> are percent‑encoded as UTF‑8 (<code>S%C3%A3o%20Paulo</code>). Existing <code>%XX</code> sequences are left as they are, and the scheme, user info, host and port are never modified.</p>
    </td>
  </tr>
  <tr>
    <td nowrap="nowrap">
      <b>Type:</b> <code>boolean</code>
      <br>
      <b>Default:</b> <code>false</code>
    </td>
  </tr>
</table>

---

<br>
<br>

## :large_orange_diamond: API `fetch` - _**sync**_

Performs a blocking HTTP request and returns the response table directly.

- Executes on the main Lua thread
- Blocks until completion
- Returns a response object
- Simpler control flow

#### Signature

```lua
-- Equivalent to fetch.get()
fetch(url [, options]) → response

-- HTTP Methods
fetch.get(url [, options]) → response
fetch.post(url [, options]) → response
fetch.put(url [, options]) → response
fetch.patch(url [, options]) → response
fetch.delete(url [, options]) → response
fetch.head(url [, options]) → response
fetch.options(url [, options]) → response
```

<br>
<br>


## :large_orange_diamond: API `fetch.async`

**Automatic Dispatch System**<br>
By default, all asynchronous requests are processed automatically. When a response is ready, the callback is executed automatically from **Rainmeter** message loop (not tied to the skin's `Update` cycle).

Returns a request object for control and monitoring. No network activity happens until `:send()` is called.


### Default Behavior

```lua
fetch.async("https://api.example.com/data"):send(function(self, response)
  -- Called AUTOMATICALLY when the response is ready
  print("Automatic response:", response:text())
end)
```

> [!NOTE]
> Dispatch is always automatic — there is no option to disable it. The callback fires as soon as the hidden notify window processes the completion message, on the next message loop iteration after the request finishes.<br>
> You do not have to keep the request in a variable: a running request is kept alive internally until its callback has run (or it is cancelled).

---

<br>
<br>


## :diamond_shape_with_a_dot_inside: Request Object Methods - _**async**_

The object returned by `fetch.async()` is a userdata with three methods: `:send()`, `:hasCompleted()` and `:cancel()`. There is no separate method to register the callback — it is the argument of `:send()`.

## :large_orange_diamond: Method `request:send()`

**Starts the asynchronous request** and, optionally, registers the function to call when it completes. Until `:send()` is invoked, no network activity occurs.

The callback receives two arguments: the request object itself (`self`) and the [response object](#diamond_shape_with_a_dot_inside-response-object).

> [!NOTE]
> The callback is optional. Without one, the request still runs, but its result is discarded.<br>
> Passing something that is not a function (and not `nil`) raises an error.<br>
> Calling `:send()` while the request is still active has no effect — **and the new callback is not registered either**. This also applies for a short moment after `:cancel()`, until the worker thread has finished winding down; to replace a request, create a new one with `fetch.async()` (see the [`:cancel()`](#large_orange_diamond-method-requestcancel) example).<br>
> After the request has finished, calling `:send()` again runs it a second time with the same options. The callback belongs to a single run, so pass it again: `req:send(callback)`. This also works from inside the callback itself.<br>
> The callback is **never** called for a cancelled request (see [`:cancel()`](#large_orange_diamond-method-requestcancel)).<br>
> Errors raised inside the callback are caught and logged as warnings; they do not break other requests.<br>
> `:send()` can be called from a coroutine.<br>
> The request runs in a separate worker thread, so the main Lua thread continues immediately.

```lua
-- @param (function|nil) callback: function(self, response) called when the request completes
-- @return (userdata) The request object (for chaining)

fetch.async("https://api.example.com/data"):send(function(self, response)
  if response.ok then
    print("Received:", response:text())
  else
    print("Error:", response.error)
  end
end)
```

<br>


## :large_orange_diamond: Method `request:hasCompleted()`

**Checks whether the request has finished** (either successfully or with an error).

> [!NOTE]
> The flag becomes `true` as soon as the worker thread has finished and stays `true`, even after the callback has run, until the next `:send()` starts a new run. It is `false` for a request that was never sent.<br>
> It does not return the response data. The response is only obtained through the callback passed to `:send()`; there is no separate "get response" or manual dispatch method.

```lua
local req = fetch.async("https://api.example.com/data")
req:send(function(self, resp) print("Done!", resp.status) end)

--- Elsewhere, e.g. inside Update()
-- @return (boolean) true once the request has finished

if req:hasCompleted() then
  print("Request finished")
end
```

<br>


## :large_orange_diamond: Method `request:cancel()`

**Cancels an ongoing request.** The network handles are closed immediately, which aborts a call that is blocked in DNS, connect, send or receive — it does not wait for a timeout.

> [!NOTE]
> The callback **never runs** for a cancelled request, whatever the network layer was doing at that moment (including a request that was already failing with an error such as `-1`).<br>
> The worker thread ends the request internally with status `-2` (`Cancelled`); that result is discarded and is not delivered to any callback.<br>
> Calling `:cancel()` on a request that was never sent, or whose callback has already been dispatched, does nothing.<br>
> Cancellation applies to `fetch.async` only. Synchronous requests block the calling thread and cannot be cancelled.<br>
> A cancelled request cannot be restarted right away: `:send()` is ignored until the worker thread has finished. Create a new request with `fetch.async()` instead.

```lua
local req = fetch.async("https://slow.example.com/bigfile")
req:send(function(self, response) print("never printed after :cancel()") end)

-- @return (userdata) The request object (for chaining)

req:cancel()
```

**Example — replace an in‑flight request:**

```lua
local current

function Search(term)
  -- Cancel the previous request (if any) so its callback never fires
  if current then current:cancel() end

  current = fetch.async("https://api.example.com/search?q=" .. term)
    :send(function(self, response)
      current = nil
      if response.ok then
        print("Results:", response:text())
      end
    end)
end
```

---

<br>
<br>


## :diamond_shape_with_a_dot_inside: Response Object

When a request completes (synchronously or asynchronously), the result is a **userdata** with the fields below. It is read-only: assigning to it (`response.x = 1`) raises an error, and `pairs(response)` does not list its fields.

> [!IMPORTANT]
> Special Status Codes (Negative Values)<br>
> When the request cannot be completed normally, the status field may contain one of the following custom error codes:
>
> | Code  | Meaning | Description |
> | :--:  | :-- | :-- |
> | `-1`  | **Network Error** | Generic network or HTTP error (e.g., connection refused, protocol error). Check `error` field for details. Also the fallback for failures that are not classified below. |
> | `-2`  | **Cancelled** | Internal final state of a request cancelled with `:cancel()`. It is **not** delivered to callbacks, because the callback never runs for a cancelled request. |
> | `-3`  | **Invalid URL** | URL format is invalid or could not be parsed. |
> | `-4`  | **No Internet** | No internet connection detected (Windows reports the machine as disconnected). |
> | `-5`  | **Thread Error** | Failed to create a worker thread for the request. |
> | `-6`  | **Aborted** | Operation aborted (E_ABORT) - usually indicates a connection issue or server closed connection prematurely. |
> | `-7`  | **Connection Lost** | Connection was lost during data transfer. |
> | `-8`  | **SSL/TLS Error** | SSL/TLS security failure (certificate issues, protocol mismatch). |
> | `-9`  | **Proxy Error** | Proxy configuration error or connection failure. |
> | `-10` | **DNS Timeout** | DNS resolution timed out. |
> | `-11` | **Connect Timeout** | Connection establishment timed out. |
> | `-12` | **Send Timeout** | Timeout while sending a request that has a `body` (WinHTTP). |
> | `-13` | **Receive Timeout** | Response data reception timed out. |
> | `-14` | **Chunked Error** | Error processing chunked encoding response. |
>
> The status is derived from the Windows error code of the failed call (never from the message text), by both drivers. Errors without a specific code still end up as `-1`, so always check `response.error` as well.<br><br>
> The `error` text of a negative status starts with the step that failed (e.g. `Send failed: `). Common network errors have a short fixed description (e.g. `Cannot connect to server. Server may be down.`); any other error uses the description from the Windows message table followed by the numeric code, e.g. `... (error 12044)`.<br><br>
> A request that exceeds `timeout` before the headers arrive ends as `-1` with `error = "Total operation timeout exceeded"`.<br><br>
> Any HTTP response (2xx, 3xx, 4xx, 5xx) is a completed request: `error` is empty and `status` holds the HTTP code. HEAD, `204` and `304` responses are normal and simply have an empty body. If the connection drops while the body is being read, the request fails (`-7`, `-13` or `-14`) instead of returning a silently truncated `200`.

> [!NOTE]
> The `ok` field is simply a shorthand for `status >= 200 and status < 300`.<br><br>
> For 2xx/3xx responses, `error` will be an empty string.<br><br>
> `ok`, `status`, and `error` are read-only fields — everything else described below (`:text()`, `:bytes()`, `:headers()`, `:cookies()`, `:xml()`, `:html()`, `:json()`, `:save()`) is a **method** and must be called with `:`, e.g. `response:text()`, not `response.text`.<br><br>
> Header and cookie names are normalized to lowercase internally. Lookups on the tables returned by `:headers()`/`:cookies()` are case-insensitive (`t["Content-Type"]` and `t["content-type"]` both work), but iterating with `pairs()` yields lowercase keys. If a header name appears more than once in the response, only the last occurrence is kept — duplicates are **not** combined into a single comma-separated value.<br><br>
> Cookies are parsed from `Set-Cookie` headers. Only the cookie name and value are stored; attributes like `Path`, `Expires`, etc. are ignored.


<table>
  <tr>
    <td align="center" nowrap="nowrap">
      <h4>Field</h4>
      <img width="400" height="1" alt="">
    </td>
    <td align="center" nowrap="nowrap">
      <h4>Description</h4>
    </td>
  </tr>

  <tr>
    <td align="center" nowrap="nowrap">
      <h5><code>ok</code></h5>
    </td>
    <td rowspan="2">
      <p><code>true</code> if the HTTP status code is in the range 200–299 (indicating success), <code>false</code> otherwise.</p>
    </td>
  </tr>
  <tr>
    <td nowrap="nowrap">
      <b>Type:</b> <code>boolean</code>
    </td>
  </tr>

  <tr>
    <td align="center" nowrap="nowrap">
      <h5><code>status</code></h5>
    </td>
    <td rowspan="2">
      <p>The HTTP status code returned by the server (e.g., 200, 404, 500). If the request failed before receiving a response, this may contain a negative error code (see below).</p>
    </td>
  </tr>
  <tr>
    <td nowrap="nowrap">
      <b>Type:</b> <code>integer</code>
    </td>
  </tr>

  <tr>
    <td align="center" nowrap="nowrap">
      <h5><code>error</code></h5>
    </td>
    <td rowspan="2">
      <p>An error message describing what went wrong. Empty string (<code>""</code>) if no error occurred.</p>
    </td>
  </tr>
  <tr>
    <td nowrap="nowrap">
      <b>Type:</b> <code>string</code>
    </td>
  </tr>

</table>

<br>


### :large_orange_diamond: Methods `response:text()` / `response:bytes()`

Both return the raw response body as a Lua string (Lua strings are byte arrays, so no encoding conversion actually happens between the two — use whichever name reads better for your use case: `:text()` for textual content, `:bytes()` for binary content).

Internally the payload is stored **once**: as text when the `Content-Type` is textual (`text/*`, JSON, XML, JavaScript, form data, ...) or, without a `Content-Type`, when the content looks like text; otherwise as binary. It is turned into a Lua string each time you call the method, so store the result in a variable if you need it more than once.

```lua
-- @return (string) the raw response body

local body = response:text()
-- local body = response:bytes()  -- identical result
```

<br>


### :large_orange_diamond: Methods `response:headers()` / `response:cookies()`

Return a key‑value Lua table — headers or cookies parsed from the response. Keys are lowercase and lookups are case‑insensitive.

```lua
-- @return (table) key-value pairs

local h = response:headers()
print(h["content-type"])

local c = response:cookies()
print(c.session)
```

<br>


### :large_orange_diamond: Methods `response:xml()` / `response:html()` / `response:json()`

Parse the response body using the corresponding `xml`, `html`, or `json` module. Each method requires the relevant module to be available via `require()`.

| Method | Equivalent to | On failure |
| :-- | :-- | :-- |
| `response:json()` | `json.decode( body )` | returns only `nil`; the reason is written to the **Rainmeter** log |
| `response:xml()`  | `xml.parse( body )`   | returns `nil, errorMessage` |
| `response:html()` | `html.parse( body )`  | returns `nil, errorMessage` |

```lua
local data = response:json()
if not data then
  print("JSON parse failed (see the Rainmeter log)")
end

local doc, err = response:xml()
if not doc then
  print("XML parse failed:", err)
end
```

<br>


### :large_orange_diamond: Method `response:save()`

**Saves the raw response body to a file.** Particularly useful for binary data (images, archives, PDFs) or when you need to persist the downloaded content.

> [!NOTE]
> The method creates any missing directories in the path automatically.<br>
> The file is written in binary mode – the exact bytes received from the server are stored.<br>
> If the response body is empty, the method returns `false` with an appropriate error message.<br>
> The path is resolved through **Rainmeter** variable/relative-path handling (`#@#`, `#CURRENTPATH#`, etc.).

```lua
-- @param (string) filePath: Filesystem path where the file should be saved
-- @return (boolean) success: true if saved successfully, false otherwise
-- @return (string|nil) error: Error message if saving failed, nil otherwise

local success, err = response:save("#@#image.jpg")

if success then
  print("File saved!")
else
  print("Save failed:", err)
end
```

---

<br>
<br>


## :large_orange_diamond: API `fetch.promiseAll`

Runs several asynchronous requests and calls **one** callback when **all** of them have completed. It behaves like [**JavaScript Promise.allSettled**](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise/allSettled): a failed request never stops the others, and every result — success or failure — is delivered.

```lua
-- @param (function) callback: called once with the list of all responses
-- @param (boolean|nil) autoSend: true (default) starts each request as soon as it is registered
-- @return (table) a callable promiseAll object

local promise = fetch.promiseAll( function( list )
  for index, response in ipairs( list ) do
    if response.ok then
      print( index, "OK", #response:text() )
    else
      print( index, "Failed", response.status, response.error )
    end
  end
end )

-- Register the requests
promise("https://api.example.com/users")
promise("https://api.example.com/posts", { timeout = 5000 })
promise("https://api.example.com/comments")
```

> [!NOTE]
> The list passed to the callback follows the order in which the requests were **registered**, not the order in which they completed. It has no holes, so `ipairs()` always walks all of it.<br>
> The items are the same [response objects](#diamond_shape_with_a_dot_inside-response-object) that `fetch.async` delivers, without any normalization. Failed requests are in the list too — check `response.ok` / `response.status`.<br>
> Every `promiseAll` has its own state, so several of them can run at the same time without mixing their results. The same object can be reused after its callback has run.

<br>


### :large_orange_diamond: Call `promise()`

Calling the object registers a new asynchronous request. It accepts the same `url` and `options` as [`fetch.async`](#large_orange_diamond-api-fetchasync) and returns the same object, so calls can be chained.

> [!NOTE]
> If the URL or the options are invalid, the error is raised right there and the request is **not** registered; the other requests are not affected.<br>
> Register all the requests in the same piece of code. A request registered after the previous ones have already finished starts a **new** run, with its own callback call. If you need to register requests at different moments, create one `promiseAll` for each group.

<br>


### :large_orange_diamond: Method `promise:send()`

Starts the registered requests that were not started yet. It is only needed with `autoSend = false`: the requests are kept until `:send()`, which starts them all at once. With the default (`true`) every request starts when it is registered, and `:send()` does nothing.

```lua
local promise = fetch.promiseAll( function( list ) print( #list ) end, false )

-- chainable
promise( "https://a.example.com" )
       ( "https://b.example.com" )

promise:send() -- both start now
```

<br>


### :large_orange_diamond: Method `promise:cancel()`

Cancels every request that is still pending and discards the results. The final callback is **not** called. The object can be used again afterwards.

```lua
-- @return the same promiseAll (chainable)
promise:cancel()
```

> [!NOTE]
> If the callback raises an error, the error is reported once in the **Rainmeter** log (`Callback error`) and the callback is never called a second time. The `promiseAll` is already cleaned at that point, so it stays usable.<br>
> When no request was registered, the callback is never called.

---

<br>
<br>


## :jigsaw: Examples

###  1. Parallel Requests with Auto-Dispatch

```lua
-- All callbacks fire automatically when each request completes

local urls = {
  users = "https://api.example.com/users",
  posts = "https://api.example.com/posts",
  comments = "https://api.example.com/comments"
}

for name, url in pairs(urls) do
  fetch.async(url):send(function(self, response)
    if response.ok then
      print(string.format("%s: %d bytes", name, #response:bytes()))
    else
      print(string.format("%s failed: %s", name, response.error))
    end
  end)
end
```

<br>


### 2. Custom Timeout Configuration

```lua
-- Fine-tuned timeouts for a slow API

local response = fetch.get("https://slow-api.example.com/data", {
  dnsTimeout = 3000,        -- 3s for DNS
  connectTimeout = 5000,    -- 5s for connection
  sendTimeout = 10000,      -- 10s to send request
  receiveTimeout = 30000,   -- 30s to receive response
  timeout = 45000           -- Overall 45s limit
})
```

<br>


### 3. File Upload with Progress Simulation

```lua
function uploadFile(url, filepath, filename)
  -- Read file
  local file = io.open(filepath, "rb")
  if not file then return nil, "Cannot open file" end
  local data = file:read("*a")
  file:close()

  -- Create boundary for multipart
  local boundary = "----WebKitFormBoundary" .. os.time()
  local body = "--" .. boundary .. "\r\n"
  body = body .. 'Content-Disposition: form-data; name="file"; filename="' .. filename .. '"\r\n'
  body = body .. "Content-Type: application/octet-stream\r\n\r\n"
  body = body .. data .. "\r\n"
  body = body .. "--" .. boundary .. "--\r\n"

  local request = fetch.async(url, {
    method = "POST",
    headers = {
      ["Content-Type"] = "multipart/form-data; boundary=" .. boundary
    },
    body = body
  })

  request:send(function(self, response)
    if response.ok then
      print("Upload complete!")
    else
      print("Upload failed:", response.error)
    end
  end)

  return true
end

-- Usage
uploadFile("https://httpbin.org/post", "document.pdf", "document.pdf")
```

<br>


### 4. Robust Error Handling

```lua
function safeFetch(url, options)
  options = options or {}
  options.timeout = options.timeout or 15000

  local response = fetch.get(url, options)

  if response.ok then
    return response, nil
  end

  -- Map status codes to user-friendly messages
  local errors = {
    [404] = "Resource not found",
    [403] = "Access denied",
    [429] = "Rate limit exceeded",
    [500] = "Server error",
    [502] = "Bad gateway",
    [503] = "Service unavailable",
    [-1] = "Network error",
    [-8] = "SSL/TLS error",
    [-9] = "Proxy error"
  }

  local message = (
    errors[response.status]
    or
    string.format("HTTP %d: %s", response.status, response.error)
  )

  -- For negative statuses, response.error carries the details
  if response.status < 0 and response.error ~= "" then
    message = message .. " (" .. response.error .. ")"
  end

  return nil, message
end

-- Usage
local data, err = safeFetch("https://api.example.com/data")
if err then
  print("Request failed:", err)
else
  print("Success:", data:text())
end
```

<br>


### 5. Session Handling with Cookies

```lua
-- Login and use session cookie
local function apiRequest(endpoint)
  local loginResp = fetch.post("https://api.example.com/login", {
    body = "username=admin&password=secret",
    headers = {
      ["Content-Type"] = "application/x-www-form-urlencoded"
    }
  })

  if not loginResp.ok then
    return nil, "Login failed"
  end

  -- Session cookie is automatically extracted!
  local sessionId = loginResp:cookies().session

  -- Use session for authenticated request
  return fetch.get("https://api.example.com/" .. endpoint, {
    headers = {
      ["Cookie"] = "session=" .. sessionId
    }
  })
end
```

---

<br>
<br>


## :zap: Performance Tips

- Set Appropriate Timeouts: Match timeouts to your specific use case
- Batch Requests: Use async methods for multiple simultaneous requests
- Monitor Memory: Large responses are kept entirely in memory
- Cheap Responses: the response is a single userdata, so it is fine to create many; call `:text()` once and keep the string in a variable
- Cancel What You Replace: when a new request supersedes an old one (search boxes, refresh buttons), call `:cancel()` on the old one so it stops immediately and its callback never runs
- Prefer Async for Slow Endpoints: a synchronous request blocks **Rainmeter** until it finishes (up to the configured timeout)
- No Connection Reuse: every request opens its own session and connection, so avoid unnecessary bursts of requests

---

<br>
<br>


## :warning: Limitations

- **Thread per Request**: Each async request creates a dedicated thread
- **Memory Constraints**: Full response is buffered in memory (once: as text or as binary, never both)
- **No Streaming**: All data must be received before processing
- **HTTP/1.0 & HTTP/1.1**: the request version is selectable via `httpVersion`; HTTP/2 and HTTP/3 are not enabled by the module
- **No WebSockets**: Not implemented
- **Windows Only**: Relies on WinHTTP/WinINet APIs
- **Sync Requests Are Not Cancellable**: only `fetch.async` requests can be cancelled
- **No Automatic Driver Fallback**: WinHTTP (default) and WinINet (`driver = "wininet"`) are two independent, explicitly-selected drivers — a request never switches drivers mid-flight on failure. WinINet uses the system's IE/Edge proxy configuration, only supports `GET` and ignores `body`.

---

<br>
<br>


## :loop: Lifecycle of Synchronous and Asynchronous Requests

```mermaid
sequenceDiagram
    participant Lua as Lua Script
    participant Fetch as Fetch Module (C++)
    participant Worker as Worker Thread
    participant Driver as WinHTTP / WinINet API
    participant Rainmeter as Rainmeter (main thread)
    participant WndProc as Hidden Window (WndProc)

    Note over Lua,Fetch: Synchronous Request (fetch.get / fetch.post / ...)
    Lua->>Fetch: fetch.get(url, options)
    Fetch->>Fetch: Parses options (incl. driver = "winhttp" | "wininet")
    Fetch->>Fetch: Registers temporary context
    Fetch->>Driver: Executes request on the CALLING thread (no worker thread, not cancellable)
    Driver-->>Fetch: Response received
    Fetch->>Fetch: Removes context from registry
    Fetch->>Lua: Returns response object (blocking)
    Note over Lua: Script continues

    Note over Lua,Fetch: Asynchronous Request (fetch.async)
    Lua->>Fetch: fetch.async(url, options)
    Fetch->>Fetch: Creates context, registers in ContextRegistry
    Fetch->>Lua: Returns request object (userdata) immediately

    Lua->>Fetch: :send(callback)
    Fetch->>Fetch: Registers callback, keeps the request alive while it runs
    Fetch->>Worker: Spawns detached worker thread

    Worker->>Driver: Executes request using the configured driver
    Driver-->>Worker: Response received (or error)
    Worker->>Worker: Stores result in the context

    Worker->>WndProc: PostMessage(WM_FETCH_COMPLETE, ctxId)
    Note over WndProc: Message enters Windows queue

    Rainmeter->>Rainmeter: Processes message loop
    Rainmeter->>WndProc: DispatchMessage() calls WndProc
    WndProc->>Fetch: Looks up context by ctxId

    alt Request was cancelled (:cancel())
        Note over Fetch: Callback is skipped.<br/>Result (-2) is discarded silently
    else Not cancelled
        Fetch->>Lua: Invokes callback(self, response) given to :send()
    end

    alt Response Successful
        Lua-->>Fetch: Processes data
    else Error Occurred
        Note over Lua: Check response.status / response.error:<br/>-1 (generic network error, most failures)<br/>-5 (thread error)<br/>-6 (ABORTED)<br/>-7 (CONNECTION_LOST)<br/>-8 (SSL_ERROR)<br/>-9 (PROXY_ERROR)<br/>-10, -11, -13 (DNS / connect / receive)<br/>-14 (CHUNKED_ERROR)
    end

    Note over Lua,Driver: Cancellation path (fetch.async only)
    Lua->>Fetch: request:cancel()
    Fetch->>Driver: Closes the open session/connect/request handles
    Driver-->>Worker: Blocked call aborts immediately
    Worker->>Worker: Ends with status -2, no callback
```

---

<br>
<br>


## :globe_with_meridians: HTTP Method Semantics

This section summarizes the intended semantics of standard HTTP methods to help you choose the appropriate one.

- **GET**

  - Retrieves a representation of a resource
  - Must not have side effects
  - Idempotent
  - Cacheable
  - No request body

  _Typical use: Retrieving a document, image, or data from an API._

<br>


- **POST**

  - Submits data for processing
  - May create new resources
  - Not idempotent
  - Allows request body

  _Typical use: Submitting a form, uploading a file, or creating a new item in an API._

<br>


- **PUT**

  - Replaces an entire resource
  - Idempotent
  - Requires full representation

  _Typical use: Fully updating an existing resource (e.g., replacing a file or a user record)._

<br>


- **PATCH**

  - Partially modifies a resource
  - Not necessarily idempotent
  - Sends only changed fields

  _Typical use: Updating specific fields of a resource (e.g., changing only the email of a user profile)._

<br>


- **DELETE**

  - Removes a resource
  - Idempotent (in theory)

  _Typical use: Removing an item, file, or record from an API._

<br>


- **HEAD**

  - Same as GET but without response body
  - Used for metadata inspection

  _Typical use: Checking if a resource exists, reading its metadata (such as Content-Length or Last-Modified) without downloading the resource._

<br>


- **OPTIONS**

  - Returns supported methods
  - Used in CORS and capability discovery

  _Typical use: Discovering which HTTP methods (GET, POST, etc.) are allowed by a server for a specific URL, often used in CORS (Cross-Origin Resource Sharing) scenarios._

---

<br>
<br>


## :scroll: License

Licensed under the **GPL v2.0 License**.<br>

---

<br>
<br>


<div align="center">
  For more information, visit the <b><a href="../../README.md">RainJIT documentation</a></b>
</div>

<br>
<br>
