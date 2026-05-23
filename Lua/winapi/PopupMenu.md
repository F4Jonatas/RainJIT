# popupmenu.lua

## Overview

`popupmenu.lua` is a lightweight LuaJIT module that provides a **safe, minimal and native Win32 popup menu abstraction** using the LuaJIT FFI.

The module is designed to create **true native Windows popup/context menus** that:

* Fully respect the system theme (light/dark)
* Follow native Windows layout, spacing, and DPI behavior
* Avoid owner-draw, custom painting, or message hooks
* Do not require a custom WndProc

The primary goal of this module is **stability, visual correctness, and native behavior**, not visual customization.

---

## Design Philosophy

This module intentionally avoids several common Win32 menu techniques:

* ❌ Owner-draw menus
* ❌ Custom fonts or font metrics
* ❌ Manual color manipulation
* ❌ Window subclassing or message hooks
* ❌ Undocumented theme manipulation APIs

These techniques often cause:

* Broken layout or alignment
* DPI scaling issues
* Theme inconsistencies
* Increased crash risk

Instead, `popupmenu.lua` relies entirely on **standard Win32 menu APIs**, ensuring that menus behave exactly like system menus created by native applications.

---

## Features

* Native popup menus via `CreatePopupMenu`
* UTF-8 safe labels (internally converted to UTF-16)
* Optional bitmap icons per item
* Separators
* Submenus
* Lua callback dispatching
* Automatic cleanup after menu display
* Bitmap and string caching for performance

---

## Requirements

* Windows
* LuaJIT
* FFI enabled

This module is **Windows-only** and depends on `user32.dll` and `kernel32.dll`.

---

## Basic Usage

```lua
local popup_menu = require("popupmenu")

local menu = popup_menu(hwnd)

menu:add("Open", function()
    print("Open clicked")
end, { icon = "open.bmp" })

menu:add("---")

menu:add("Exit", function()
    print("Exit clicked")
end)

menu:show()
```

---

## Module Interface

### `popup_menu(hwnd)`

Creates a new popup menu instance.

* **Parameters**:

  * `hwnd` (HWND or nil): Optional owner window handle. If omitted, the current foreground window is used.

* **Returns**:

  * A `Menu` object

---

## Menu Object

The `Menu` object represents a native Win32 popup menu.

### `Menu:add(label, callback, options)`

Adds a menu item or a separator.

* **Parameters**:

  * `label` (string): Item label. If set to `"---"`, a separator is inserted.
  * `callback` (function or nil): Lua function executed when the item is selected.
  * `options` (table, optional):

    * `icon` (string): Path to a bitmap file (16×16 recommended).
    * `separator` (boolean): If true, inserts a separator.

* **Returns**:

  * The menu instance (for chaining)

---

### `Menu:sub(submenu, label)`

Adds a submenu to the current menu.

* **Parameters**:

  * `submenu` (Menu): Another `Menu` instance.
  * `label` (string): Submenu label.

* **Returns**:

  * The menu instance

---

### `Menu:show()`

Displays the popup menu at the current cursor position.

Behavior:

* Applies modern menu style flags
* Positions the menu at the mouse cursor
* Dispatches the selected item callback
* Automatically destroys the menu afterward

This function blocks until the menu is dismissed.

---

### `Menu:cleanup()`

Releases all resources associated with the menu instance.

Notes:

* This function is called automatically by `Menu:show()`
* Calling it manually is only required if the menu is never shown

---

## Global Functions

### `popup_menu.global_cleanup()`

Releases **all cached resources**, including:

* Loaded bitmap handles
* Stored callbacks

This should be called:

* On application shutdown
* Or when you are certain no more menus will be created

---

## Icon Handling

* Icons must be **bitmap files** (`.bmp`)
* Recommended size: **16×16 pixels**
* Icons are loaded using `LoadImageW`
* All loaded bitmaps are cached to avoid redundant loads

The module uses `MNS_CHECKORBMP` to ensure correct alignment of bitmap icons.

---

## UTF-8 Handling

All menu labels are assumed to be UTF-8 encoded Lua strings.

Internally, they are converted to UTF-16 using `MultiByteToWideChar` and cached using weak references for efficiency.

---

## Theme and Dark Mode Behavior

This module **does not force light or dark themes**.

* Menus automatically follow the system and application theme
* If Windows is in Dark Mode, menus will appear dark
* If Windows is in Light Mode, menus will appear light

This behavior is intentional and ensures:

* Maximum compatibility
* Native appearance
* No reliance on undocumented Windows APIs

---

## Limitations

By design, the following are **not supported**:

* Custom colors
* Custom fonts
* Owner-draw items
* Font or padding manipulation
* Forced dark mode on light systems

If full visual control is required, a custom-drawn menu implementation is recommended instead.

---

## License

This module is provided as-is, without warranty. Use at your own risk.

---

## Summary

`popupmenu.lua` is intended for developers who want:

* Native Windows menus
* Correct system behavior
* Minimal complexity
* High stability

If you need **native behavior**, this module is appropriate.
If you need **visual control**, consider a custom UI solution instead.
