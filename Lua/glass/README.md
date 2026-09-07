<div align="center">

  # Module Glass

  <br>

</div>



## Overview

A lightweight Lua module that applies modern Windows visual effects (Mica, Acrylic, Blur, Dark Mode, Rounded Corners, Border, Shadow) to any window using FFI and native Windows APIs.

_**How It Works**_<br>
The module uses LuaJIT's FFI to call three Windows API functions:

- `SetWindowCompositionAttribute` – for legacy accent effects (blur, acrylic, transparency).
- `DwmSetWindowAttribute` – for modern DWM attributes (Mica, dark mode, corners, border color, shadow).
- `RtlGetVersion` – to check the Windows build number for compatibility.

Before applying new effects, an internal general reset function is always called to disable all previously defined attributes (including the border color), ensuring a clean initial state.

---

<br>
<br>


## :green_book: Features

> [!TIP]
> All effects can be combined, and the module automatically resets previous settings before applying new ones.

- **Mica & Mica Alt** – Windows 11 backdrop materials
- **Acrylic Blur** – Fluent Design acrylic effect (Windows 10 1803+)
- **Blur Behind** – Classic Aero-style blur
- **Transparent Gradient** – Simple transparency
- **Solid Color** – Opaque fill with optional opacity
- **Rounded Corners** – Control window corner rounding
- **Dark Mode** – Toggle immersive dark mode
- **Border** – Remove, restore, or set a custom color for the DWM border
- **Shadow** – Enable or disable the window shadow

<br>
<br>


## Requirements

- **Windows 10/11** (some effects require specific builds – see Compatibility)
- **LuaJIT** (or any Lua with `ffi` support)
- No external dependencies – the module uses `user32.dll`, `dwmapi.dll`, and `ntdll.dll` via FFI.

<br>
<br>



## :jigsaw: Example


```lua
local glass = require("glass")


-- @usage glass(hwnd, options)
-- @param (number|userdata) hwnd - a valid Windows window handle (LuaJIT FFI `HWND` cdata).
-- @param (table) options – a table with the desired effects (all fields optional).
-- @return (nil)

glass( rain.hwnd, {
  effect  = "mica",
  corners = "round",
  dark    = true,
  shadow  = true,
  border  = false  -- remove the DWM border
})


-- Custom border color
glass( rain.hwnd, {
  effect = "acrylic",
  border = 0xFF0000  -- red border (0xRRGGBB)
})
```

---

<br>
<br>


## :book: Usage

### :diamond_shape_with_a_dot_inside: Property `effect`

Selects the background effect for the window.
- `mica`|`mica_alt` – Windows 11 backdrop materials (requires build ≥ 22000).
- `blur` – Classic blur behind the window.
- `acrylic` – Fluent Design acrylic blur (requires build ≥ 17134). Use effect_opts to set opacity and tint color.
- `transparent` – Simple transparent gradient (no blur).
- `solid` – Opaque fill; you can set opacity and color via effect_opts.

Values: (`mica`|`mica_alt`|`blur`|`acrylic`|`transparent`|`solid`)<br>
Type: `string`

<br>


### :diamond_shape_with_a_dot_inside: Property `effect_opts`

Additional parameters for `acrylic` and `solid` effects.
- `opacity` – Alpha value. Default: 0 for acrylic, 255 for solid.
- `color`  – RGB tint color. Default: 0x000000 (black).

Values: (`opacity`|`color`)<br>
Type: `table`

<br>


### :diamond_shape_with_a_dot_inside: Property `corners`

Controls the rounding of window corners.
- `round` – Fully rounded corners.
- `small` – Slightly rounded corners.
- `none` – Sharp, square corners.
- `default` – Restores the system default behavior.

> [!IMPORTANT]
> Requires Windows 11 build ≥ 22000; may work on some Windows 10 builds with newer DWM.


Values: (`round`|`small`|`none`|`default`)<br>
Type: `string`

<br>


### :diamond_shape_with_a_dot_inside: Property `dark`

If `true`, enables immersive dark mode for the window title bar and borders.<br>
If `false`, disables dark mode.

Values: (`false`|`true`)<br>
Type: `boolean`

<br>


### :diamond_shape_with_a_dot_inside: Property `border`

Controls the DWM border drawn around the window frame.
- `false` – Removes the border entirely.
- `true` – Restores the system default border color.
- `0xRRGGBB` – Sets a custom border color (e.g. `0xFF0000` for red).

> [!IMPORTANT]
> Requires Windows 11 build ≥ 22000. Ignored silently on older builds.

Values: (`false`|`true`|[`Hexadecimal Colors`](https://htmlcolorcodes.com/))<br>
Type: (`boolean`|`number`)

<br>


### :diamond_shape_with_a_dot_inside: Property `shadow`

If `true`, the window shadow is enabled.<br>
If `false`, the shadow is removed.

Values: (`false`|`true`)<br>
Type: `boolean`

---

<br>
<br>


## Compatibility

> [!NOTE]
> Effects that are not supported on the current Windows version will be ignored. The module does not throw errors (except for `acrylic`, which explicitly asserts if the build is too old).

| Effect          | Minimum Windows Build | Notes                                           |
| --------------- | --------------------- | ----------------------------------------------- |
| Mica / Mica Alt | 22000 (Win11 21H2)    | Requires Windows 11                             |
| Acrylic         | 17134 (Win10 1803)    |                                                 |
| Blur            | Any Win10/Win11       |                                                 |
| Transparent     | Any Win10/Win11       |                                                 |
| Solid           | Any Win10/Win11       |                                                 |
| Rounded Corners | 22000 (Win11 21H2)    | Also works on some Win10 builds with newer DWM  |
| Dark Mode       | 17763 (Win10 1809)    | Attribute IDs 19 & 20                           |
| Border          | 22000 (Win11 21H2)    | `DWMWA_BORDER_COLOR` — ignored on older builds  |
| Shadow          | Any Win10/Win11       |                                                 |

---

<br>
<br>


## Known Issues

- **Battery Saver Mode** – On laptops, when battery saver is active, Windows may automatically disable some visual effects (especially acrylic and blur) to save power. The module will still attempt to apply them, but the OS may override the settings.
- **Per‑Monitor DPI** – Some effects may behave unexpectedly on systems with mixed DPI settings.

---

<br>
<br>


## :scroll: License

Licensed under the **GPL v2.0 License**.

---

<br>
<br>
