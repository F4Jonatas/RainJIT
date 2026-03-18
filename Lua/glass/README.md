<div align="center">

  # Module Glass

  <br>

</div>


A lightweight Lua module that applies modern Windows visual effects (Mica, Acrylic, Blur, Dark Mode, Rounded Corners, Shadow) to any window using FFI and native Windows APIs.

---

<br>
<br>


## :green_book: Features

> [!TIP]
> All effects can be combined, and the module automatically resets previous settings before applying new ones.

- **Mica & Mica Alt** – Windows 11 backdrop materials
- **Acrylic Blur** – Fluent Design acrylic effect (Windows 10 1803+)
- **Blur Behind** – Classic Aero-style blur
- **Transparent Gradient** – Simple transparency
- **Solid Color** – Opaque fill with optional opacity
- **Rounded Corners** – Control window corner rounding
- **Dark Mode** – Toggle immersive dark mode
- **Shadow** – Enable or disable the window shadow

---

<br>
<br>


## Requirements

- **Windows 10/11** (some effects require specific builds – see Compatibility)
- **LuaJIT** (or any Lua with `ffi` support)
- No external dependencies – the module uses `user32.dll`, `dwmapi.dll`, and `ntdll.dll` via FFI.

---

<br>
<br>


## :book: Usage

```lua
-- @usage glass( hwnd, options ) → nil
-- @param (number|userdata) hwnd - a valid Windows window handle (LuaJIT FFI `HWND` cdata).
-- @param (table) options – a table with the desired effects (all fields optional).

glass(hwnd, options)
```


### :jigsaw: Example


```lua
local glass = require("glass")

glass( rain.hwnd, {
  effect = "mica",
  corners = "round",
  dark = true,
  shadow = true
})
```

---

<br>
<br>


## :diamond_shape_with_a_dot_inside: Options

<table>
  <tr>
    <td align="center" nowrap="nowrap">
      <h4>Option</h4>
    </td>
    <td align="center" nowrap="nowrap">
      <h4>Description</h4>
      <img width="900" height="1" alt="">
    </td>
  </tr>

  <tr>
    <td align="center" nowrap="nowrap">
      <h5><code>effect</code></h5>
    </td>
    <td rowspan="2">
      Selects the background effect for the window.
      <ul>
        <li><b>mica / mica_alt</b> – Windows 11 backdrop materials (requires build ≥ 22000).</li>
        <li><b>blur</b> – Classic blur behind the window.</li>
        <li><b>acrylic</b> – Fluent Design acrylic blur (requires build ≥ 17134). Use <code>effect_opts</code> to set
          opacity and tint color.</li>
        <li><b>transparent</b> – Simple transparent gradient (no blur).</li>
        <li><b>solid</b> – Opaque fill; you can set opacity and color via <code>effect_opts</code>.</li>
      </ul>
    </td>
  </tr>
  <tr>
    <td nowrap="nowrap">
      <b>Type:</b> <code>string</code>
      <br>
      <b>Allowed Values:</b>
      <ul>
        <li><code>"mica"</code></li>
        <li><code>"mica_alt"</code></li>
        <li><code>"blur"</code></li>
        <li><code>"acrylic"</code></li>
        <li><code>"transparent"</code></li>
        <li><code>"solid"</code></li>
      </ul>
    </td>
  </tr>

  <tr>
    <td align="center" nowrap="nowrap">
      <h5><code>effect_opts</code></h5>
    </td>
    <td rowspan="2">
      Additional parameters for <code>"acrylic"</code> and <code>"solid"</code> effects.
      <ul>
        <li><b>opacity</b> – Alpha value. Default: <code>0</code> for acrylic, <code>255</code> for solid.</li>
        <li><b>color</b> – RGB tint color. Default: <code>0x000000</code> (black).</li>
      </ul>
    </td>
  </tr>
  <tr>
    <td nowrap="nowrap">
      <b>Type:</b> <code>table</code>
      <br>
    </td>
  </tr>

  <tr>
    <td align="center" nowrap="nowrap">
      <h5><code>corners</code></h5>
    </td>
    <td rowspan="2">
      Controls the rounding of window corners.
      <ul>
        <li><b>round</b> – Fully rounded corners.</li>
        <li><b>small</b> – Slightly rounded corners.</li>
        <li><b>none</b> – Sharp, square corners.</li>
        <li><b>default</b> – Restores the system default behavior.</li>
      </ul>
      <i>(Requires Windows 11 build ≥ 22000; may work on some Windows 10 builds with newer DWM.)</i>
    </td>
  </tr>
  <tr>
    <td nowrap="nowrap">
      <b>Type:</b> <code>string</code>
      <br>
      <b>Allowed Values:</b>
      <ul>
        <li><code>"round"</code></li>
        <li><code>"small"</code></li>
        <li><code>"none"</code></li>
        <li><code>"default"</code></li>
      </ul>
    </td>
  </tr>

  <tr>
    <td align="center" nowrap="nowrap">
      <h5><code>dark</code></h5>
    </td>
    <td rowspan="2">
      If <code>true</code>, enables immersive dark mode for the window title bar and borders.
      <br>
      If <code>false</code>, disables dark mode.
    </td>
  </tr>
  <tr>
    <td nowrap="nowrap">
      <b>Type:</b> <code>boolean</code>
    </td>
  </tr>

  <tr>
    <td align="center" nowrap="nowrap">
      <h5><code>shadow</code></h5>
    </td>
    <td rowspan="2">
      If <code>true</code>, the window shadow is enabled.
      <br>
      If <code>false</code>, the shadow is removed.
    </td>
  </tr>
  <tr>
    <td nowrap="nowrap">
      <b>Type:</b> <code>boolean</code>
    </td>
  </tr>
</table>

---

<br>
<br>


## Compatibility

> [!NOTE]
> Effects that are not supported on the current Windows version will be ignored. The module does not throw errors (except for `acrylic`, which explicitly asserts if the build is too old).

| Effect          | Minimum Windows Build | Notes                                           |
| --------------- | --------------------- | ----------------------------------------------- |
| Mica / Mica Alt | 22000 (Win11 21H2)    | Requires Windows 11                             |
| Acrylic         | 17134 (Win10 1803)    |                                                 |
| Blur            | Any Win10/Win11       |                                                 |
| Transparent     | Any Win10/Win11       |                                                 |
| Solid           | Any Win10/Win11       |                                                 |
| Rounded Corners | 22000 (Win11 21H2)    | Also works on some Win10 builds with newer DWM? |
| Dark Mode       | 17763 (Win10 1809)    | Attribute IDs 19 & 20                           |
| Shadow          | Any Win10/Win11       |                                                 |

---

<br>
<br>


## Known Issues

- **Battery Saver Mode** – On laptops, when battery saver is active, Windows may automatically disable some visual effects (especially acrylic and blur) to save power. The module will still attempt to apply them, but the OS may override the settings.
- **Per‑Monitor DPI** – Some effects may behave unexpectedly on systems with mixed DPI settings.

---

<br>
<br>


## How It Works

The module uses LuaJIT’s FFI to call three Windows API functions:

- `SetWindowCompositionAttribute` – for legacy accent effects (blur, acrylic, transparency).
- `DwmSetWindowAttribute` – for modern DWM attributes (Mica, dark mode, corners, shadow).
- `RtlGetVersion` – to check the Windows build number for compatibility.

Before applying new effects, `glass` always calls an internal `resetAll` function that disables all previously set attributes, ensuring a clean slate.

---

<br>
<br>