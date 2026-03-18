<div align="center">

# Hotkey Module for RainJIT

<br>

</div>



## Overview
The **Hotkey module** for **RainJIT** is a low-level hotkey detection system that leverages [**Windows hooks**](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowshookexw). It enables **Lua scripts** to define key combinations and execute corresponding callback functions upon key press or release. Utilizing a single `WH_KEYBOARD_LL` hook and a thread-safe event buffer, this module delivers fast and reliable hotkey events for your **Rainmeter** skins.

<br>
<br>


# :green_book: Features

- **Global hotkeys**: work even when Rainmeter is not in focus.
- **Full modifier support**: Ctrl, Alt, Shift, Win, and their left/right variants.
- **Flexible trigger modes**: fire on key press, release, or both.
- **Focus‑aware**: optionally require the skin window to have focus.
- **All keys mode**: capture every keystroke for advanced use cases.
- **Multiple combinations per callback**: associate several key combos with a single Lua function.
- **Thread‑safe event queue**: safe communication between the hook thread and the main Lua thread.
- **Automatic cleanup**: resources are freed when skins are unloaded or objects are garbage‑collected.
- **Comprehensive event table**: provides detailed information about the key event, modifier states, lock states, and more.
- **Automatic lifecycle management**


<br>
<br>


# :book: Usage

## Basic Usage

```lua
local hotkey = require("hotkey")

local kb = hotkey.keyboard{
    vk = "F12",
    on = "press",
    focus = false,

    callback = function(event)
        print("F12 pressed globally!")
    end
}
```

<br>


## :large_orange_diamond: Method `hotkey.keyboard({`[`config`](#diamond_shape_with_a_dot_inside-configuration-table)`})`

This method creates a new hotkey configuration and returns a keyboard object that provides methods for control.

> [!NOTE]
All methods of the returned object will return false if the hotkey no longer exists (e.g., after being removed).

<br>


### :diamond_shape_with_a_dot_inside: Configuration Table

> [!NOTE]
> The `vk` field accepts several formats (`string`|`table`|`"all"`) and **<ins>is required</ins>**.<br>
> Supported formats:<br>
>
> ```lua
> -- 1. Single key.
> -- VK_ prefix is optional.
> vk = "F12"
> vk = "VK_ESCAPE"
> vk = "A"
> vk = "SPACE"
>
> -- 2. Key combination
> -- Join keys using "+" (order does not matter).
> vk = "CTRL+SHIFT+F5"
> vk = "VK_F12+VK_ALT"
> vk = "LWIN+R"
>
> -- 3. Multiple combinations
> -- The callback fires when any combination matches.
> vk = {
>   "F12",
>   "CTRL+SHIFT+F5",
>   "ALT+SPACE"
> }
>
> -- 4. Special value "all"
> -- Captures every key.
> vk = "all"
> ```

> [!IMPORTANT]
> Using `vk="all"` mode
> - Combinations are ignored.
> - [`event.keys`]((#diamond_shape_with_a_dot_inside-event-object)) contains only the triggering key.

<br>

> [!NOTE]
> The `on` field accepts (`"press"`|`"release"`|`"both"`) and **<ins>is not required</ins>**. Default: `"both"`.
>
> ```lua
> on = "press"
> ```

<br>

> [!NOTE]
> The `focus` field accept (`boolean`) and **<ins>is not required</ins>**. If `true`, the callback is only called when the config window has foreground focus. Default: `true`.
>
> ```lua
> focus = false
> ```

<br>

> [!NOTE]
> The `callback` field accept (`function`) and **<ins>is required</ins>**. The function to execute when the hotkey is triggered. It receives a single event table [**Event object**](#diamond_shape_with_a_dot_inside-event-object).
>
> ```lua
> callback = function(event)
>   print(event.vk)
> end
> ```

<br>

```lua
-- @usage hotkey.keyboard( config ) → object
-- @tparam (string|table|"all") vk
-- @tparam ("press"|"release"|"both") [on="both"]
-- @tparam (boolean) [focus=true]
-- @tparam (function) callback
-- @treturn (table) An instance that represents the shortcut key

local kb = hotkey.keyboard{
  vk = "F12",
  on = "press",
  focus = false,

  callback = function( event )
    print("A fan of keyboard shortcuts!")
  end
}
```

<br>


### :diamond_shape_with_a_dot_inside: `event` Object

The callback receives a single table with the following fields:

| Field         | Type      | Description                                                   |
| :--           | :--:      | :--:                                                          |
| `char`        | _string_  | UTF-8 character produced by the key (empty if non-printable). |
| `code`        | _number_  | Virtual key code of triggering key.                           |
| `type`        | _string_  | `"press"` or `"release"`.                                     |
| `keys`        | _table_   | Names of keys in the matched combination.                     |
| `capslock`    | _boolean_ | Caps Lock state.                                              |
| `numlock`     | _boolean_ | Num Lock state.                                               |
| `scrolllock`  | _boolean_ | Scroll Lock state.                                            |
| `ctrl`        | _boolean_ | Any Ctrl key pressed.                                         |
| `alt`         | _boolean_ | Any Alt key pressed.                                          |
| `shift`       | _boolean_ | Any Shift key pressed.                                        |
| `timestamp`   | _number_  | High-resolution timestamp (seconds since system start).       |
| `vk`          | _string_  | Name of triggering key (e.g. `"VK_F12"`).                     |
| `focus`       | _boolean_ | Whether the skin condif had focus at trigger time.            |

---

<br>



### :large_orange_diamond: Method `kb:enable()`

Re-enables a disabled hotkey.<br>
Returns `true` on success.

```lua
local success = kb:enable()
```

<br>


### :large_orange_diamond: Method `kb:disable()`
Temporarily disables the hotkey.<br>
Returns `true` on success.

```lua
local success = kb:disable()
```

<br>


### :large_orange_diamond: Method `kb:isEnabled()`

Returns whether the hotkey is currently active.

```lua
local enabled = kb:isEnabled()
```

<br>


### :large_orange_diamond: Method `kb:remove()`

Permanently removes the hotkey and releases associated resources.

```lua
local success = kb:remove()
```

---

<br>
<br>

## :jigsaw: Examples

### 1. Global Screenshot Shortcut

```lua
hotkey.keyboard{
  vk = "CTRL+SHIFT+S",
  on = "press",
  focus = false,

  callback = function(event)
    print("Screenshot shortcut triggered!")
  end
}
```

<br>

### 2. Multiple Combinations

```lua
hotkey.keyboard{
  vk = {
    "CTRL+1",
    "CTRL+NUMPAD1"
  },

  callback = function(event)
    print("Profile 1 selected")
  end
}
```

<br>

### 3. All Keys Logger

> [!WARNING]
> Use carefully — this captures every keystroke.

```lua
hotkey.keyboard{
  vk = "all",
  focus = false,

  callback = function(event)
    print(event.type, event.vk, event.char)
  end
}
```

---

<br>
<br>


## Supported Key Names

- Letters: `A`–`Z`
- Numbers: `0`–`9`
- Function keys: `F1`–`F24`
- Modifiers: `CTRL`, `SHIFT`, `ALT`, `LWIN`, `RWIN`
- Navigation: `LEFT`, `RIGHT`, `UP`, `DOWN`, `HOME`, `END`
- Media keys
- OEM keys
- Numpad keys
- Gamepad virtual keys (Windows 10+)
- Raw numeric codes (`"0x41"` or `"65"`)

<br>

<details>

<summary><ins>Complete Virtual-Key Codes</ins></summary>
<br>

| Name                               | Hex  |
| :--:                               | --:  |
| VK_LBUTTON                         | 0x01 |
| VK_RBUTTON                         | 0x02 |
| VK_CANCEL                          | 0x03 |
| VK_MBUTTON                         | 0x04 |
| VK_XBUTTON1                        | 0x05 |
| VK_XBUTTON2                        | 0x06 |
| VK_BACK                            | 0x08 |
| VK_TAB                             | 0x09 |
| VK_CLEAR                           | 0x0C |
| VK_RETURN                          | 0x0D |
| VK_SHIFT                           | 0x10 |
| VK_CONTROL                         | 0x11 |
| VK_MENU                            | 0x12 |
| VK_PAUSE                           | 0x13 |
| VK_CAPITAL                         | 0x14 |
| VK_ESCAPE                          | 0x1B |
| VK_SPACE                           | 0x20 |
| VK_PRIOR                           | 0x21 |
| VK_NEXT                            | 0x22 |
| VK_END                             | 0x23 |
| VK_HOME                            | 0x24 |
| VK_LEFT                            | 0x25 |
| VK_UP                              | 0x26 |
| VK_RIGHT                           | 0x27 |
| VK_DOWN                            | 0x28 |
| VK_SELECT                          | 0x29 |
| VK_PRINT                           | 0x2A |
| VK_EXECUTE                         | 0x2B |
| VK_SNAPSHOT                        | 0x2C |
| VK_INSERT                          | 0x2D |
| VK_DELETE                          | 0x2E |
| VK_HELP                            | 0x2F |
| 0                                  | 0x30 |
| 1                                  | 0x31 |
| 2                                  | 0x32 |
| 3                                  | 0x33 |
| 4                                  | 0x34 |
| 5                                  | 0x35 |
| 6                                  | 0x36 |
| 7                                  | 0x37 |
| 8                                  | 0x38 |
| 9                                  | 0x39 |
| A                                  | 0x41 |
| B                                  | 0x42 |
| C                                  | 0x43 |
| D                                  | 0x44 |
| E                                  | 0x45 |
| F                                  | 0x46 |
| G                                  | 0x47 |
| H                                  | 0x48 |
| I                                  | 0x49 |
| J                                  | 0x4A |
| K                                  | 0x4B |
| L                                  | 0x4C |
| M                                  | 0x4D |
| N                                  | 0x4E |
| O                                  | 0x4F |
| P                                  | 0x50 |
| Q                                  | 0x51 |
| R                                  | 0x52 |
| S                                  | 0x53 |
| T                                  | 0x54 |
| U                                  | 0x55 |
| V                                  | 0x56 |
| W                                  | 0x57 |
| X                                  | 0x58 |
| Y                                  | 0x59 |
| Z                                  | 0x5A |
| VK_LWIN                            | 0x5B |
| VK_RWIN                            | 0x5C |
| VK_APPS                            | 0x5D |
| VK_SLEEP                           | 0x5F |
| VK_NUMPAD0                         | 0x60 |
| VK_NUMPAD1                         | 0x61 |
| VK_NUMPAD2                         | 0x62 |
| VK_NUMPAD3                         | 0x63 |
| VK_NUMPAD4                         | 0x64 |
| VK_NUMPAD5                         | 0x65 |
| VK_NUMPAD6                         | 0x66 |
| VK_NUMPAD7                         | 0x67 |
| VK_NUMPAD8                         | 0x68 |
| VK_NUMPAD9                         | 0x69 |
| VK_MULTIPLY                        | 0x6A |
| VK_ADD                             | 0x6B |
| VK_SEPARATOR                       | 0x6C |
| VK_SUBTRACT                        | 0x6D |
| VK_DECIMAL                         | 0x6E |
| VK_DIVIDE                          | 0x6F |
| VK_F1                              | 0x70 |
| VK_F2                              | 0x71 |
| VK_F3                              | 0x72 |
| VK_F4                              | 0x73 |
| VK_F5                              | 0x74 |
| VK_F6                              | 0x75 |
| VK_F7                              | 0x76 |
| VK_F8                              | 0x77 |
| VK_F9                              | 0x78 |
| VK_F10                             | 0x79 |
| VK_F11                             | 0x7A |
| VK_F12                             | 0x7B |
| VK_F13                             | 0x7C |
| VK_F14                             | 0x7D |
| VK_F15                             | 0x7E |
| VK_F16                             | 0x7F |
| VK_F17                             | 0x80 |
| VK_F18                             | 0x81 |
| VK_F19                             | 0x82 |
| VK_F20                             | 0x83 |
| VK_F21                             | 0x84 |
| VK_F22                             | 0x85 |
| VK_F23                             | 0x86 |
| VK_F24                             | 0x87 |
| VK_NUMLOCK                         | 0x90 |
| VK_SCROLL                          | 0x91 |
| VK_LSHIFT                          | 0xA0 |
| VK_RSHIFT                          | 0xA1 |
| VK_LCONTROL                        | 0xA2 |
| VK_RCONTROL                        | 0xA3 |
| VK_LMENU                           | 0xA4 |
| VK_RMENU                           | 0xA5 |
| VK_BROWSER_BACK                    | 0xA6 |
| VK_BROWSER_FORWARD                 | 0xA7 |
| VK_BROWSER_REFRESH                 | 0xA8 |
| VK_BROWSER_STOP                    | 0xA9 |
| VK_BROWSER_SEARCH                  | 0xAA |
| VK_BROWSER_FAVORITES               | 0xAB |
| VK_BROWSER_HOME                    | 0xAC |
| VK_VOLUME_MUTE                     | 0xAD |
| VK_VOLUME_DOWN                     | 0xAE |
| VK_VOLUME_UP                       | 0xAF |
| VK_MEDIA_NEXT_TRACK                | 0xB0 |
| VK_MEDIA_PREV_TRACK                | 0xB1 |
| VK_MEDIA_STOP                      | 0xB2 |
| VK_MEDIA_PLAY_PAUSE                | 0xB3 |
| VK_LAUNCH_MAIL                     | 0xB4 |
| VK_LAUNCH_MEDIA_SELECT             | 0xB5 |
| VK_LAUNCH_APP1                     | 0xB6 |
| VK_LAUNCH_APP2                     | 0xB7 |
| VK_OEM_1                           | 0xBA |
| VK_OEM_PLUS                        | 0xBB |
| VK_OEM_COMMA                       | 0xBC |
| VK_OEM_MINUS                       | 0xBD |
| VK_OEM_PERIOD                      | 0xBE |
| VK_OEM_2                           | 0xBF |
| VK_OEM_3                           | 0xC0 |
| VK_OEM_4                           | 0xDB |
| VK_OEM_5                           | 0xDC |
| VK_OEM_6                           | 0xDD |
| VK_OEM_7                           | 0xDE |
| VK_OEM_8                           | 0xDF |
| VK_OEM_102                         | 0xE2 |
| VK_GAMEPAD_A                       | 0xC3 |
| VK_GAMEPAD_B                       | 0xC4 |
| VK_GAMEPAD_X                       | 0xC5 |
| VK_GAMEPAD_Y                       | 0xC6 |
| VK_GAMEPAD_RIGHT_SHOULDER          | 0xC7 |
| VK_GAMEPAD_LEFT_SHOULDER           | 0xC8 |
| VK_GAMEPAD_LEFT_TRIGGER            | 0xC9 |
| VK_GAMEPAD_RIGHT_TRIGGER           | 0xCA |
| VK_GAMEPAD_DPAD_UP                 | 0xCB |
| VK_GAMEPAD_DPAD_DOWN               | 0xCC |
| VK_GAMEPAD_DPAD_LEFT               | 0xCD |
| VK_GAMEPAD_DPAD_RIGHT              | 0xCE |
| VK_GAMEPAD_MENU                    | 0xCF |
| VK_GAMEPAD_VIEW                    | 0xD0 |
| VK_GAMEPAD_LEFT_THUMBSTICK_BUTTON  | 0xD1 |
| VK_GAMEPAD_RIGHT_THUMBSTICK_BUTTON | 0xD2 |

</details>

---

<br>
<br>

## Performance Notes

- Only one global hook is installed.
- All skins share the same hook instance.
- Matching cost scales with number of registered combinations.
- Avoid registering excessive `vk="all"` handlers.

---

<br>
<br>

## Limitations

1. **Auto-repeat behavior**<br>
Holding a key may generate repeated `"press"` events.

2. **Combination detection relies on real-time key state**<br>
Uses `GetAsyncKeyState`, so extremely rapid sequences may behave differently under high CPU load.

3. **Heavy callbacks can impact responsiveness**<br>
Keep Lua callbacks lightweight.

4. **Injected key events are ignored**<br>
Synthetic input generated by other software will not trigger callbacks.

5. **No built-in debounce mechanism**<br>
If needed, implement in Lua.

---

<br>
<br>

## Best Practices

- Keep callbacks fast.
- Use `focus=true` when possible.
- Prefer specific combinations instead of "all".
- Remove unused hotkeys with `:remove()`.

---

<br>
<br>

