<div align="center">

  # RainJIT TODO

</div>


## Update System
- [x] Compile the Plugin with **LuaJIT** without relying on external Lua DLLs
- [ ] Check modules exists before full Rainmeter initialization
  - [ ] Check if existis files
  - [ ] Download files and create folders, if necessery
- [ ] Update check - _Core (Runtime / DLL)_
  - [ ] Execute before full Rainmeter initialization
  - [ ] Integration with GitHub API
  - [ ] Endpoint: releases/latest
  - [ ] Version parsing - _(SemVer)_
  - [ ] Comparison with local version
  - [ ] HTTP request system
    - [ ] Reuse Fetch module
  - [ ] User feedback
  - [ ] Integration with `toast`/`dialog` module or a simple message box
  - [ ] Configuration persistence
    - [ ] Save preference in `Rainmeter.data`
    - [ ] Flags:
      - [ ] `auto-check` enabled
      - [ ] `last check` timestamp

---

<br>
<br>


## Module WebSocket

- [ ] Base implementation client/server
  - [ ] Connect / disconnect
  - [ ] Send / receive
- [ ] Asynchronous loop integrated with Rainmeter update cycle
- [ ] Lua callbacks:
  - [ ] `on_message`
  - [ ] `on_error`
  - [ ] `on_close`
- [ ] Automatic reconnection

---

<br>
<br>


## Module Dialog

- [ ] Simple modal dialog
- [ ] Types:
  - [ ] `info`
  - [ ] `warning`
  - [ ] `confirm`
- [ ] Response callback (Lua)

---

<br>
<br>


## Module Toast

- [ ] Base notification implementation
  - [ ] Plain text
  - [ ] Display duration
- [ ] Integration with Windows Notifications
- [ ] Lua API:
   - [ ] `show(message, duration, type)`

---

<br>
<br>


## Module Depot

- [ ] In-memory storage system - _(no physical files)_
- [ ] Encryption/Decryption
  - [ ] Define algorithm (e.g., AES-256)
  - [ ] Key management
- [ ] Compatibility with .env standards
  - [ ] dotenv-style parsing

---

<br>
<br>


## Module Fetch

- [ ] Persistent connection (keep-alive)
- [ ] Lua API inspired by JS/Python
- [ ] Connection state
  - [ ] `connected()`
- [ ] Progress callbacks
  - [ ] `download`
  - [ ] `upload`
- [ ] Optimization
  - [ ] Migrate from table → userdata

---

<br>
<br>


## Module Hotkey

- [ ] Implement hotkeys for cursor (mouse)
- [ ] Mouse support
  - [ ] `click`
  - [ ] `movement`
  - [ ] `scroll`

---

<br>
<br>
