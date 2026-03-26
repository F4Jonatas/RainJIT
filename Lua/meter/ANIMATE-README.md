# Animate (meter.animate)

A lightweight animation layer built on top of a tweening system, designed to interpolate values over time and apply them directly to meter instances.

This module separates concerns into three distinct parts:

- **Interpolation** → handled by the tween module
- **Application** → handled by this module (meter updates)
- **Scheduling** → handled via a centralized update loop

The result is a clean, extensible animation system with explicit lifecycle control and predictable behavior.

---

# Core Concept

Instead of directly mutating values every frame, animations are defined declaratively:

```lua

local animate = require("meter.animate")

local a = animate(meter, 1.0, "outQuad")
  :from({ top = -255 })
  :to({ top = 0 })
  :create()
```

Then executed through a global scheduler:

```lua
function Update()
  animate.updateAll(dt)
end
```


---

# Features

- Declarative animation API (`from → to`)
- Multiple easing functions (via tween module)
- Global scheduler (`updateAll`)
- Manual update mode
- Explicit lifecycle control
- Safe restart and cancellation
- Automatic removal of finished animations

---

# Installation

Ensure the following dependencies are available:

- `tween`
- `meter`
```lua
local animate = require("meter.animate")
```
---

# Basic Usage
```lua
local a = animate(meter, 1.0, "outQuad")
  :from({ x = 0 })
  :to({ x = 100 })
  :create()

function Update()
  animate.updateAll(1/60)
end
```
---

# Lifecycle

Animations follow a strict lifecycle model:

|State|Description|
|---|---|
|`idle`|Not started or reset|
|`running`|Actively updating|
|`paused`|Temporarily halted|
|`finished`|Completed and removed|

---

# API Reference

## Constructor
```lua
animate(meter, duration, easing)
```

|Parameter|Type|Description|
|---|---|---|
|meter|object|Target meter instance|
|duration|number|Duration in seconds|
|easing|string/function|Easing function|

---

## Builder Methods

### `:from(values)`

Defines initial values.
```lua
:from({ x = 0 })
```
---

### `:to(values)`

Defines target values.

```lua
:to({ x = 100 })
```
---

## Lifecycle Methods

### `:create([options])`

Initializes and starts the animation.

```lua
:create()
```

With manual mode:

```lua
:create({ manual = true })
```
---

### `:pause()`

Pauses the animation.

---

### `:play()`

Resumes a paused animation.

---

### `:reset()`

Resets animation to initial state without restarting.

---

### `:restart()`

Fully restarts the animation.

```lua
if a.playState == "finished" then
  a:restart()
end
```
---

### `:cancel()`

Stops the animation completely and removes it from the scheduler.

```lua
a:cancel()
```
---

## Update Methods

### Global Scheduler

```lua
animate.updateAll(dt)
```

Updates all active animations.

Must be called once per frame.

---

### Manual Update

```lua
a:update(dt)
```

Only works if created with:

```lua
:create({ manual = true })
```
---

# Manual vs Automatic Mode

## Automatic (default)

```lua
:create()
animate.updateAll(dt)
```
- Managed globally
- Recommended for most use cases

---

## Manual
```lua
:create({ manual = true })
a:update(dt)
```

- Full control over update timing
- Not affected by `updateAll`

---

# Important Rules

### 1. Do not mix modes

```lua
-- WRONG
animate.updateAll(dt)
a:update(dt)
```
---

### 2. Always call `create()`
```lua
-- WRONG
a:update(dt) -- without create
```
---

### 3. Restart after finish
```lua
if a.playState == "finished" then
  a:restart()
end
```
---

# Example: Loop Animation
```lua
function Update()
  animate.updateAll(dt)

  if a.playState == "finished" then
    a:restart()
  end
end
```
---

# Example: Manual Control
```lua
local a = animate(meter, 1)
  :from({ x = 0 })
  :to({ x = 100 })
  :create({ manual = true })

function Update()
  a:update(dt)
end
```
---

# Behavior Notes

- Finished animations are automatically removed from the scheduler
- `cancel()` removes the animation immediately
- `reset()` keeps the animation reusable
- `restart()` recreates internal state and resumes execution
- Values are mirrored into the animation instance (`a.x`, `a.top`, etc.)

---

# Design Goals

This module aims to provide:

- Predictable animation lifecycle
- Minimal implicit behavior
- Clear separation of responsibilities
- Compatibility with real-time systems (e.g., Rainmeter)

---

# Future Extensions

Potential extensions include:

- Looping (`repeat`)
- Yoyo (reverse animation)
- Delay support
- Timeline composition