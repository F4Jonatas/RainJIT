<div align="center">

  # WebSocket Module for RainJIT
  ### A local WebSocket server and a ws:// client for your Rainmeter, with no external dependencies.

  <br>
  <br>


  <img src="../images/websocket-logo.png" alt="LOGO" height="200">

</div>



## Overview
The **WebSocket module** for **RainJIT** lets **Lua scripts** run a local [**WebSocket**](https://datatracker.ietf.org/doc/html/rfc6455) server inside Rainmeter. Other apps (scripts, tools, browsers, mobile apps) connect to it and exchange messages with your skin in real time.

It is a self-contained **RFC 6455** implementation on plain **Winsock**: **no external dependencies**. Each server runs on its own background thread, and every callback is delivered on the Rainmeter thread, so your Lua code is never touched by two threads.

By default the server listens only on `127.0.0.1`, so only apps on the same machine can connect. Use the [`auth`](#diamond_shape_with_a_dot_inside-property-auth) option to require a password.

The same engine also works as a **client**: [`websocket.connect()`](#large_orange_diamond-method-websocketconnect) connects your skin to a `ws://` server (a device on your network, a local service, …), with custom headers, subprotocols and **automatic reconnection**.

<br>
<br>


## :green_book: Features

- **Local by default**: listens on `127.0.0.1`; other addresses are opt-in.
- **Password protection**: credential in a custom header, a query parameter, or the `Sec-WebSocket-Protocol` entry. Wrong or missing credentials get an HTTP `401` and the connection never opens.
- **Several credentials**: accept any of a list of secrets (e.g. one per client, revocable).
- **Automatic port fallback**: when the port is busy, the next ones are tried.
- **ws:// client**: connect to any WebSocket server, with custom request headers and subprotocols.
- **Automatic reconnection** (client): exponential backoff, optional retry limit and a `reconnecting` callback.
- **Non‑blocking connect**: `websocket.connect()` returns at once; the result arrives as `open` or `error`.
- **Publish / subscribe topics**: group connections and send to everyone subscribed.
- **Broadcast**: send to every open connection.
- **Text and binary messages**, including fragmented messages.
- **Ping / pong and idle timeout**: silent connections are checked and dropped.
- **Backpressure control**: slow clients cannot make the server grow without limit.
- **Per-connection user data**: attach any Lua value to a connection.
- **Thread‑safe event queue**: safe communication between the server thread and the main Lua thread.
- **Automatic cleanup**: servers and clients are stopped when skins are unloaded or objects are garbage‑collected.

<br>
<br>


## :jigsaw: Quick Example

### Server

```lua
local websocket = require("websocket")

local server, err = websocket.server({
  port = 8765,

  open = function(ws)
    print("connected:", ws:ip())
  end,

  message = function(ws, data, isBinary)
    ws:send("echo: " .. data)
  end,

  close = function(ws, code, reason)
    print("closed:", code, reason)
  end
})

if not server then
  print("could not create the server:", err)
  return
end

local ok, listenErr = server:listen()
if ok then
  print("listening on", server:url())   -- e.g. ws://127.0.0.1:8765
else
  print("could not listen:", listenErr)
end
```

<br>


### Client

```lua
local websocket = require("websocket")

local client, err = websocket.connect({
  url = "ws://192.168.0.10:3000",
  headers = { ["X-App-Key"] = "my-password" },
  reconnect = true,

  open = function(c, protocol)
    print("connected to", c:url())
    c:send("hello")
  end,

  message = function(c, data, isBinary)
    print("received:", data)
  end,

  close = function(c, code, reason)
    print("closed:", code, reason)
  end,

  reconnecting = function(c, attempt, delay)
    print("retrying in " .. delay .. " ms (attempt " .. attempt .. ")")
  end,

  error = function(c, message)
    print("error:", message)
  end
})

if not client then
  print("invalid options:", err)
end
```

<br>


### Password Protected Server

```lua
local websocket = require("websocket")

local server = websocket.server({
  auth = {
    type = "header",
    name = "X-App-Key",
    secret = "a-long-random-password"
  },

  message = function(ws, data)
    print("authenticated client says:", data)
  end
})

server:listen()
```

---

<br>


## :large_orange_diamond: Method `websocket.server()`

Creates a server and returns a server object, or `nil, err` when an option is invalid (the error is also written to the Rainmeter log). The server does not listen until [`server:listen()`](#large_orange_diamond-method-serverlisten) is called.

```lua
-- @usage websocket.server(options)
-- @param (table) options
-- @field (string) [host="127.0.0.1"]
-- @field (number) [port=8765]
-- @field (number) [portTries=20]
-- @field (string) [path="/*"]
-- @field (table) [auth]
-- @field (number) [maxPayload=1048576]
-- @field (number) [maxBackpressure=1048576]
-- @field (number) [idleTimeout=120]
-- @field (boolean) [sendPings=true]
-- @field (number) [maxConnections=0]
-- @field (function) [open]
-- @field (function) [message]
-- @field (function) [close]
-- @field (function) [drain]
-- @field (function) [ping]
-- @field (function) [pong]
-- @field (function) [error]
-- @return (table|nil) The server object, or nil
-- @return (string) An error message
```

<br>


## :large_orange_diamond: Method `server:listen()`

Starts the server and waits (briefly) until it is listening or has failed.<br>
Returns the server on success, or `nil, err` on failure.

```lua
local ok, err = server:listen()
```

> [!IMPORTANT]
> A server is **single‑use**. After `server:stop()` or a failed `listen()` it cannot be started again — create a new one with `websocket.server()`.

<br>


## :large_orange_diamond: Method `server:stop()`

Closes every connection (clients receive close code `1001`) and the port.<br>
Pending events are discarded: **no more callbacks run after `stop()`**.<br>
Returns the server.

```lua
server:stop()
```

<br>


## :large_orange_diamond: Method `server:on()`

Registers (or replaces) the callback of an event after the server was created. Passing `nil` removes it.<br>
Event names are case‑insensitive. Returns the server.

```lua
server:on("message", function(ws, data, isBinary)
  print(data)
end)

server:on("message", nil) -- removes the handler
```

<br>


## :large_orange_diamond: Method `server:publish()`

Sends a message to **every connection subscribed to a topic**.<br>
Returns `false` if the server is not running.

```lua
-- @usage server:publish(topic, data [, binary])
server:publish("news", "hello subscribers")
```

<br>


## :large_orange_diamond: Method `server:broadcast()`

Sends a message to **every open connection**.<br>
Returns `false` if the server is not running.

```lua
-- @usage server:broadcast(data [, binary])
server:broadcast("server is going down in 10s")
```

<br>


## :large_orange_diamond: Method `server:connection()`

Returns the connection object with the given id, or `nil` if it is not open.

```lua
local ws = server:connection(3)
```

<br>


## :large_orange_diamond: Status Methods

| Method                | Returns                                                                                          |
| :--                   | :--                                                                                              |
| `server:host()`       | The configured listening address.                                                                |
| `server:port()`       | The port actually in use, or `nil` until the server is listening.                                |
| `server:url()`        | `"ws://127.0.0.1:8765"`, or `nil` until the server is listening.                                 |
| `server:running()`    | `true` while the server is listening.                                                            |
| `server:connections()`| Number of open connections.                                                                      |
| `server:dropped()`    | Number of `message`/`ping`/`pong`/`drain` events discarded because Lua could not keep up.        |

---

<br>
<br>


## :large_orange_diamond: Method `websocket.connect()`

Creates a client, starts connecting and returns the client object, or `nil, err` when an option is invalid (the error is also written to the Rainmeter log).<br>
It **returns immediately**: the TCP connect and the handshake run on the client thread, and the result is reported with the `open` callback (success) or the `error` callback (failure).

```lua
-- @usage websocket.connect(options)
-- @param (table) options
-- @field (string) url
-- @field (table) [headers]
-- @field (string) [protocol]
-- @field (table) [protocols]
-- @field (number) [connectTimeout=5000]
-- @field (boolean|table) [reconnect=false]
-- @field (number) [maxPayload=1048576]
-- @field (number) [maxBackpressure=1048576]
-- @field (number) [idleTimeout=0]
-- @field (boolean) [sendPings=true]
-- @field (function) [open]
-- @field (function) [message]
-- @field (function) [close]
-- @field (function) [drain]
-- @field (function) [ping]
-- @field (function) [pong]
-- @field (function) [error]
-- @field (function) [reconnecting]
-- @return (table|nil) The client object, or nil
-- @return (string) An error message
```

> [!IMPORTANT]
> A client is **single‑use**: after it closes for good (a `close`/`error` event with no reconnection following, or `client:close()`), create a new one with `websocket.connect()`. Use the [`reconnect`](#diamond_shape_with_a_dot_inside-property-reconnect) option to keep one connected automatically.

<br>


## :large_orange_diamond: Client Object

| Method                          | Description                                                                                                  |
| :--                             | :--                                                                                                          |
| `client:send(data [, binary])`  | Queues a message. Text by default, `true` for binary. Returns `true` if queued, `false` while the client is not open (connecting, reconnecting, closing or closed). |
| `client:close([code [, reason]])` | Starts a clean close (same codes as [`ws:close()`](#method-wsclose)). Returns `true` if accepted. While the client is still connecting or waiting to reconnect, it gives up **silently**: no callback runs. A requested close is never followed by a reconnection. |
| `client:on(event, callback)`    | Registers, replaces (or, with `nil`, removes) a callback. Event names are case‑insensitive. Returns the client. |
| `client:isOpen()`               | `true` while the handshake is done and the connection is not closing.                                         |
| `client:url()`                  | The URL given to `websocket.connect()`.                                                                       |

```lua
client:send("hello")
client:send(string.char(0, 1, 2), true)
client:close(1000, "bye")
```

> [!NOTE]
> `client:send()` returns `false` until `open` has fired. Send from the `open` callback (or after `client:isOpen()` is `true`).

<br>


## Client Options

### :diamond_shape_with_a_dot_inside: Property `url`

The `url` field accepts `string` and **is required**.<br>
Format: `ws://host[:port][/path][?query]`. The default port is `80`; the host can be a name, an IPv4 address or an IPv6 address in brackets (`ws://[::1]:3000/`).<br>
`wss://` is **not supported** (returns `nil, err`). Credentials in the URL (`user:password@`) and fragments (`#…`) are rejected: use [`headers`](#diamond_shape_with_a_dot_inside-property-headers) or the query string.

<br>


### :diamond_shape_with_a_dot_inside: Property `headers`

The `headers` field accepts `table` and **is not required**.<br>
Extra HTTP headers sent with the upgrade request, as `{ ["Name"] = "value" }`.

```lua
headers = { ["X-App-Key"] = "my-password", ["Origin"] = "http://localhost" }
```

- Names must be valid header tokens; values cannot contain control characters (no line breaks).
- The headers the handshake sets itself cannot be overridden: `Host`, `Upgrade`, `Connection`, `Content-Length` and `Sec-WebSocket-*`.

<br>


### :diamond_shape_with_a_dot_inside: Property `protocol` / `protocols`

`protocol` accepts a `string`, `protocols` a list of strings. Both are **optional** and can be combined.<br>
They are offered to the server in `Sec-WebSocket-Protocol`. The one the server chose is passed to the `open` callback (`nil` if none). A server that answers with a subprotocol that was not offered is rejected.

<br>


### :diamond_shape_with_a_dot_inside: Property `connectTimeout`

The `connectTimeout` field accepts `number` (milliseconds, `100`–`120000`) and **is not required**.<br>
Default: `5000`. It covers the TCP connect **and** the handshake together; when it runs out, the attempt fails with `error`.

<br>


### :diamond_shape_with_a_dot_inside: Property `reconnect`

The `reconnect` field accepts `boolean` or `table` and **is not required**.<br>
Default: `false` (the client does not reconnect). `true` enables it with the defaults below.

| Field      | Type     | Default | Description                                                                                         |
| :--        | :--:     | :--:    | :--                                                                                                 |
| `delay`    | _number_ | `1000`  | Milliseconds before the first retry (`100`–`3600000`). It **doubles** after every failed attempt.   |
| `maxDelay` | _number_ | `30000` | Upper bound of the delay (`100`–`3600000`). Must not be smaller than `delay`.                       |
| `maxTries` | _number_ | `0`     | Consecutive retries before giving up (`0`–`1000000`). `0` means never give up.                      |

```lua
reconnect = true
reconnect = { delay = 2000, maxDelay = 60000, maxTries = 10 }
```

How it behaves:
- It reconnects after a **failed attempt** (refused, timed out, rejected handshake) and after an **established connection ends** (dropped, closed by the server, idle timeout).
- Waits are `delay`, `2 × delay`, `4 × delay`, … up to `maxDelay`. After a connection opens successfully, the sequence starts again from `delay`.
- With `maxTries = N`, the client gives up after **N consecutive retries** that did not open. The last failure is reported with a final `error` and the client stops.
- It **never** reconnects after `client:close()`, after the skin is unloaded, or once it has given up.
- The `close` and `error` callbacks still run for every drop and failure; the client object stays valid and keeps being used. Messages sent while it is not open are **not** queued (`send` returns `false`).
- Use the [`reconnecting`](#client-events) callback to know when the next attempt is scheduled.

> [!NOTE]
> `reconnect` is only available on clients. Passing it to `websocket.server()` returns `nil, err`.

<br>


### :diamond_shape_with_a_dot_inside: Other client options

| Option            | Default   | Description                                                                                           |
| :--               | :--:      | :--                                                                                                   |
| `maxPayload`      | `1048576` | Largest message accepted from the server (bytes). A bigger one closes the connection with code `1009`. |
| `maxBackpressure` | `1048576` | Maximum data waiting to be sent. Above it, new messages are dropped until `drain` fires. `0` = unlimited. |
| `idleTimeout`     | `0`       | Seconds of silence after which the connection is dropped (`0`–`960`). `0` (default) never drops a quiet server. |
| `sendPings`       | `true`    | With `idleTimeout` set, ping a silent server at half of it, so a live but quiet server stays connected. |

`tls` is **not supported** (`nil, err`). `compression` is ignored, with a warning in the Rainmeter log.

<br>


### Client Events

Same rules as the server events: callbacks run on the Rainmeter thread, an error inside one is logged and does not stop the client, and `message`, `ping`, `pong`, `drain` and `reconnecting` are only delivered when a handler is registered. **Every callback receives the client object as its first argument.**

| Event          | Callback signature                         | When                                                                                    |
| :--            | :--                                        | :--                                                                                     |
| `open`         | `function(client, protocol)`               | The handshake succeeded. `protocol` is the subprotocol chosen by the server, or `nil`.   |
| `message`      | `function(client, data, isBinary)`         | A complete message arrived.                                                             |
| `close`        | `function(client, code, reason)`           | An established connection ended (see the close codes under Events).                            |
| `drain`        | `function(client)`                         | A slow connection that had messages dropped has caught up.                              |
| `ping`         | `function(client, payload)`                | The server sent a ping (the pong is answered automatically).                            |
| `pong`         | `function(client, payload)`                | The server answered a ping.                                                             |
| `error`        | `function(client, message)`                | A connection attempt failed (see below), or the client thread failed.                   |
| `reconnecting` | `function(client, attempt, delay)`         | A new attempt is scheduled: its number and the wait in milliseconds.                    |

Typical `error` messages: `Could not connect to host:port (error N)`, `Connecting to host:port timed out`, `The handshake timed out`, `The server rejected the connection: HTTP/1.1 401 Unauthorized`, `Could not resolve the host "…"`.

With `reconnect` enabled, `close` and `error` are followed by `reconnecting` while attempts remain. Without it (or after giving up), `close` / `error` are the **last** callback of that client.

---

<br>
<br>


## Server Options

### :diamond_shape_with_a_dot_inside: Property `host`

The `host` field accepts `string` and **is not required**.<br>
Address to listen on. Default: `"127.0.0.1"` (this machine only).<br>
`"localhost"` is converted to `"127.0.0.1"` (it could resolve to IPv6). Use `"0.0.0.0"` to accept connections from the network — see [Security Notes](#security-notes).

<br>


### :diamond_shape_with_a_dot_inside: Property `port`

The `port` field accepts `number` (`0`–`65535`) and **is not required**.<br>
Default: `8765`. With `0`, the system picks a free port (read it with `server:port()`).<br>
The port is bound **exclusively**: no other process can share it.

<br>


### :diamond_shape_with_a_dot_inside: Property `portTries`

The `portTries` field accepts `number` (`1`–`100`) and **is not required**.<br>
Default: `20`.

If the port is busy, the next ports (`port+1`, `port+2`, …) are tried, up to `portTries` ports in total. Always use `server:port()` or `server:url()` to know where the server really is.

<br>


### :diamond_shape_with_a_dot_inside: Property `path`

The `path` field accepts `string` and **is not required**.<br>
Default: `"/*"` (any path).<br>
Accepted forms: an exact path (`"/ws"`), a prefix (`"/chat/*"`) or `"/*"`. Other paths get HTTP `404`.

<br>


### :diamond_shape_with_a_dot_inside: Property `auth`

The `auth` field accepts `table` and **is not required**. Without it, anyone who can reach the port can connect.

| Field     | Type     | Description                                                                                  |
| :--       | :--:     | :--                                                                                          |
| `type`    | _string_ | **Required.** `"header"`, `"query"` or `"protocol"`.                                         |
| `name`    | _string_ | Header name or query parameter. **Required** for `header` and `query`, unused for `protocol`. |
| `secret`  | _string_ | The accepted password.                                                                       |
| `secrets` | _table_  | A list of accepted passwords. Use it with, or instead of, `secret`. At least one is required. |

```lua
auth = { type = "header",   name = "X-App-Key", secret = "my-password" }
auth = { type = "query",    name = "token",     secrets = { "client-a", "client-b" } }
auth = { type = "protocol", secret = "my-password" }
```

| `type`       | Where the client sends the password                | Browsers                                              |
| :--          | :--                                                | :--                                                   |
| `"header"`   | A custom request header (`X-App-Key: my-password`) | **Not possible**: the browser WebSocket API cannot set headers. Use it with Node, mobile apps, scripts, … |
| `"query"`    | A URL parameter (`ws://host:8765/?token=my-password`) | Works. The URL may end up in logs — prefer short‑lived or revocable secrets. |
| `"protocol"` | An entry of `Sec-WebSocket-Protocol` (`new WebSocket(url, ["my-password"])`) | Works.                         |

Details:
- The check happens **before** the connection becomes a WebSocket. A wrong or missing credential gets `401 Unauthorized`.
- Comparison runs in constant time, so response time does not reveal how much of a password matched.
- Header names and query parameter names are case‑insensitive. Query values are percent‑decoded (`+` is kept as `+`).
- For `"protocol"`, the secret may only contain letters, digits and ``! # $ % & ' * + - . ^ _ ` | ~``. If the client offers other subprotocols besides the secret (`["my-password", "chat"]`), the server answers with one of **those** (`"chat"`), so the secret is not echoed back. If the secret is the only entry, it has to be echoed (browsers require an answer).
- Secrets cannot be empty and cannot contain control characters.

<br>


### :diamond_shape_with_a_dot_inside: Property `maxPayload`

The `maxPayload` field accepts `number` (bytes, `1`–`2147483647`) and **is not required**.<br>
Default: `1048576` (1 MiB).<br>
Largest message a client may send. A bigger one closes the connection with code `1009`.

<br>


### :diamond_shape_with_a_dot_inside: Property `maxBackpressure`

The `maxBackpressure` field accepts `number` (bytes, `0`–`2147483647`) and **is not required**.<br>
Default: `1048576` (1 MiB). `0` means unlimited.<br>
Maximum amount of data waiting to be sent to one connection. While a slow client is above the limit, new messages for it are **dropped**; the `drain` event fires when it catches up.

<br>


### :diamond_shape_with_a_dot_inside: Property `idleTimeout`

The `idleTimeout` field accepts `number` (seconds, `0`–`960`) and **is not required**.<br>
Default: `120`. `0` disables it.<br>
A connection that sends nothing for this long is closed (code `1006`).

<br>


### :diamond_shape_with_a_dot_inside: Property `sendPings`

The `sendPings` field accepts `boolean` and **is not required**.<br>
Default: `true`.<br>
When `true`, a connection that has been silent for half of `idleTimeout` receives a ping; the pong keeps it alive. When `false`, only clients that send data themselves stay connected.

<br>


### :diamond_shape_with_a_dot_inside: Property `maxConnections`

The `maxConnections` field accepts `number` (`0`–`1000000`) and **is not required**.<br>
Default: `0` (unlimited).<br>
Beyond the limit, new connections get `503 Service Unavailable`.

<br>


### :diamond_shape_with_a_dot_inside: Unsupported options

| Option        | Behavior                                                                       |
| :--           | :--                                                                            |
| `tls`         | **Not supported** (ws only). `websocket.server()` returns `nil, err`.           |
| `compression` | Not supported. `true` is ignored, with a warning in the Rainmeter log.          |

---

<br>
<br>


## Events

Callbacks can be passed in the options table or registered later with [`server:on()`](#large_orange_diamond-method-serveron). They run on the Rainmeter thread, shortly after the event happens (not inside the network thread), so it is safe to call any Rainmeter/Lua function from them. An error inside a callback is written to the Rainmeter log and does not stop the server.

| Event     | Callback signature                         | When                                                                                           |
| :--       | :--                                        | :--                                                                                            |
| `open`    | `function(ws)`                             | A client connected and passed authentication.                                                  |
| `message` | `function(ws, data, isBinary)`             | A complete message arrived (fragments are joined). `data` is a Lua string.                     |
| `close`   | `function(ws, code, reason)`               | The connection ended. After this, `ws` is no longer open.                                      |
| `drain`   | `function(ws)`                             | A slow connection that had messages dropped has caught up.                                     |
| `ping`    | `function(ws, payload)`                    | The client sent a ping (the pong is answered automatically).                                   |
| `pong`    | `function(ws, payload)`                    | The client answered a ping.                                                                    |
| `error`   | `function(server, message)`                | The server thread failed. The server is stopped afterwards.                                    |

**Close codes** reported by `close`:

| Code   | Meaning                                                                   |
| :--:   | :--                                                                       |
| `1000` | Normal close, or the code sent by the client / chosen with `ws:close()`.  |
| `1001` | The server was stopped.                                                   |
| `1005` | The client closed without a code.                                         |
| `1006` | Abnormal: the connection dropped, timed out or failed without a close frame. |
| other  | Whatever the client sent (`1002`, `1007`, `1009`, `3000`–`4999`, …).      |

> [!NOTE]
> `message`, `ping`, `pong` and `drain` are only delivered when a handler is registered for them.
> Events are delivered in batches of up to 500 per window message, so a flood of messages never freezes Rainmeter. If Lua falls far behind (more than 100,000 queued events), further `message`/`ping`/`pong`/`drain` events are discarded and counted in `server:dropped()`. `open`, `close` and `error` are never discarded.

---

<br>
<br>


## :large_orange_diamond: Connection Object

Every callback receives a connection object `ws`. It stays valid while the connection is open; methods return `false` once it is closed.

### Method `ws:send()`

Queues a message for this connection. Text by default; pass `true` for a **binary** frame.<br>
Returns `true` if queued, `false` if the connection is closed.

```lua
-- @usage ws:send(data [, binary])
ws:send("hello")
ws:send(string.char(0, 1, 2), true)
```

> [!NOTE]
> Text messages must be valid UTF‑8, as the protocol requires. Use `binary = true` for arbitrary bytes.

<br>


### Method `ws:close()`

Starts a clean close: the close frame is sent, then the connection ends.<br>
Returns `true` if the close was queued, `false` if the connection is already closed.<br>
`isOpen()` becomes `false` right away.

```lua
-- @usage ws:close([code [, reason]])
ws:close()                    -- 1000
ws:close(4001, "not allowed") -- application codes: 3000-4999
```

Valid codes: `1000`, `1001`–`1003`, `1007`–`1014` and `3000`–`4999` (anything else raises an error). `reason` is truncated to 123 bytes.

<br>


### Method `ws:subscribe()` / `ws:unsubscribe()`

Adds or removes this connection from a topic. Returns `true` if queued.

```lua
ws:subscribe("news")
ws:unsubscribe("news")
```

<br>


### Method `ws:publish()`

Sends a message to every subscriber of a topic **except this connection**. Returns `true` if queued.

```lua
-- @usage ws:publish(topic, data [, binary])
ws:publish("chat", "hello everyone else")
```

<br>


### Information Methods

| Method          | Returns                                                       |
| :--             | :--                                                           |
| `ws:ip()`       | The client address, as text.                                  |
| `ws:id()`       | A number that identifies the connection inside this server.   |
| `ws:isOpen()`   | `true` while the connection is open.                          |

<br>


### Method `ws:setData()` / `ws:getData()`

Keeps any Lua value (a table, a name, …) with the connection. `setData(nil)` clears it.<br>
`setData` returns the connection; `getData` returns the value or `nil`.

```lua
open = function(ws)
  ws:setData({ name = "guest", joined = os.time() })
end,

message = function(ws, data)
  local user = ws:getData()
  print(user.name, data)
end
```

---

<br>
<br>


## Protocol Behavior

**Handshake responses:**

| Status | When                                                                              |
| :--:   | :--                                                                               |
| `101`  | Upgrade accepted.                                                                 |
| `400`  | Malformed request, or a bad `Sec-WebSocket-Key`.                                  |
| `401`  | Missing or wrong credential (see `auth`).                                         |
| `404`  | Path does not match `path`.                                                       |
| `426`  | Not a WebSocket upgrade, or a version other than `13`.                            |
| `431`  | Request headers larger than 8 KiB.                                                |
| `503`  | `maxConnections` reached.                                                         |

A client that does not finish the handshake within 10 seconds is dropped.

**Frames — the server closes the connection when the client:**

| Code   | Violation                                                                     |
| :--:   | :--                                                                           |
| `1002` | Sends an unmasked frame, uses RSV bits or reserved opcodes, breaks fragmentation rules, or sends an invalid control frame/close code. |
| `1007` | Sends text that is not valid UTF‑8.                                           |
| `1009` | Sends a message bigger than `maxPayload`.                                     |

**Client side:**

| Rule                                                                                                   |
| :--                                                                                                    |
| The handshake must answer `101`, with a valid `Sec-WebSocket-Accept`; any other status is reported as `The server rejected the connection: <status line>`. |
| The server must not select an extension (none is offered) or a subprotocol that was not offered.        |
| Frames sent by the client are **masked**, as the protocol requires; frames received from the server must **not** be masked (otherwise the connection is closed with `1002`). |
| The close codes `1002`, `1007` and `1009` apply to what the server sends, just as they do for clients of a server. |

---

<br>
<br>


## Security Notes

- **ws is not encrypted.** Anyone able to observe the traffic can read messages **and the password**. This is fine on `127.0.0.1`. If you listen on another address (`host = "0.0.0.0"`), traffic crosses the network in clear text — the module writes a warning to the Rainmeter log in that case.
- **A password embedded in a client app can be extracted.** Prefer one `secret` per client (using `secrets`) so a leaked one can be replaced.
- **Always set `auth` when `host` is not loopback.** The module warns when you do not.
- **Origin is not checked.** A web page opened in the user's browser can try to connect to `ws://127.0.0.1:<port>`. With `auth` set, it fails unless it knows the credential; without `auth`, any website could talk to your server. For `type = "header"`, browsers cannot send the header at all, which also blocks them.
- **The client speaks ws only.** Credentials sent in `headers` or in the URL query travel in clear text unless the network path is trusted. Do not use it to reach servers over the Internet.
- **The client does not verify who it talks to.** Anyone who can answer on that address can be the server. Only connect to addresses you trust.
- Other limits help against abuse: `maxPayload`, `maxConnections`, `idleTimeout`, the handshake timeout and the 8 KiB header limit.

---

<br>
<br>


## Performance Notes

- Each server uses **one thread** for all its connections; each client uses **one thread** for its connection.
- Name resolution (`getaddrinfo`) runs on the client thread, never on the Rainmeter thread.
- Callbacks run on the Rainmeter thread: keep them lightweight, especially `message` handlers on busy servers.
- Up to **2048** sockets (connected + handshaking) per server are accepted at the same time.
- Registering no handler for `message`, `ping`, `pong` or `drain` avoids queueing those events at all.

---

<br>
<br>


## Limitations

1. **ws only**<br>
There is no `wss` (TLS), for servers or clients. To expose the server beyond the local machine, put a TLS proxy in front of it. Clients cannot connect to `wss://` endpoints.

2. **No compression**<br>
`permessage-deflate` is not supported.

3. **Single‑use servers and clients**<br>
After `stop()` or a failed `listen()`, create a new server. A client that closed for good (or gave up reconnecting) must be replaced with a new `websocket.connect()`.

4. **Asynchronous by design**<br>
`ws:send()` only queues the message; `true` means "queued", not "delivered". Messages for a slow client can still be dropped (see `maxBackpressure`).

5. **Callbacks are not instant**<br>
Events reach Lua through the Rainmeter message loop, so a callback may run a few milliseconds after the event.

6. **Close handshake**<br>
After a close frame is sent, the other side has 3 seconds to hang up before the connection is dropped.

7. **`listen()` blocks briefly**<br>
It waits until the server thread reports success or failure (normally a few milliseconds, at most 10 seconds). `websocket.connect()` only waits for its thread to start.

8. **No message queue while disconnected**<br>
A client does not keep messages for later: `client:send()` returns `false` until the connection is open again.

9. **Reconnection has no jitter**<br>
Retries follow the fixed backoff sequence, the same for every client.

---

<br>
<br>


## Best Practices

- Keep `host` on `127.0.0.1` unless you really need the network.
- Always check the result of `websocket.server()` and `server:listen()`.
- Read the real address from `server:url()` when you depend on port fallback.
- Use `auth`, with one secret per client when possible.
- Use `ws:setData()` to keep per‑client state instead of global tables indexed by `ws:id()`.
- Call `server:stop()` when the skin no longer needs the server, and `client:close()` when it no longer needs the client.
- For a client, send from the `open` callback and enable `reconnect` for devices that restart or go to sleep.
- Handle `error` on clients: without `reconnect` it is the only way to learn that the connection could not be made.
- Keep callbacks fast; do heavy work outside them.

---

<br>
<br>


## License

Licensed under the **GPL v2.0 License**.

---

<br>
<br>


<div align="center">
  For more information, visit the <b><a href="../../README.md">RainJIT documentation</a></b>
</div>

<br>
<br>
