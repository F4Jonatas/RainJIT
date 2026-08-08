<div align="center">

  # Animate

  ### Easily create and control animations

</div>

<br>
<br>


A lightweight animation layer built on top of a tweening system, designed to interpolate values over time and apply them directly to meter instances.

This submodule aims to provide:

- Predictable animation lifecycle
- Minimal implicit behavior
- Clear separation of responsibilities

<br>

It divides the responsibilities into three distinct parts:

- **Interpolation** → handled by the tween module
- **Application** → handled by this module (meter updates)
- **Scheduling** → handled via a centralized update loop

The result is a clean, extensible animation system with explicit lifecycle control and predictable behavior.

<br>
<br>


## :green_book: Features

- Declarative animation API (`from → to`)
- Multiple easing functions (via tween module)
- Optional start delay (`:delay()`)
- Global scheduler (`updateAll`)
- Manual update mode
- Explicit lifecycle control
- Safe restart, reversal and cancellation
- Automatic removal of finished animations from the schedule
- Global check for scheduler completion (`finished()`)

<br>
<br>


## :jigsaw: Quick Example

Instead of directly mutating values every frame, animations are defined declaratively<br>
Then executed through a global scheduler:

```lua
local meter = require("meter")
local animate = require("meter.animate")

local image = meter("background")
local a = animate(image, 100, "outQuad")
  :from({ top = -255 })
  :to({ top = 0 })
  :delay(0.25)
  :create()


-- @param (number) au - accumulated updates (resets every ~9 quadrillion)
-- @param (number) dt - delta time (clamped to 0.0-1.0)
function rain:update(au, dt)
  animate.updateAll(dt)
end
```

---

<br>
<br>


## :book: Module `animate`

### :diamond_shape_with_a_dot_inside: Property `playState` _read-only_

Animations follow a strict lifecycle model, inspired by the Web Animations API

| States     | Description                         |
| :---       | :--:                                |
| `idle`     | Not started or reset                |
| `running`  | Actively updating                   |
| `paused`   | Temporarily halted                  |
| `finished` | Completed and removed from schedule |

<br>

### :diamond_shape_with_a_dot_inside: Property `meter`

Target meter instance

<br>

### :diamond_shape_with_a_dot_inside: Property `tween`

Underlying tween instance created in [**`:create()`**](#large_orange_diamond-method-acreate) or [**`:restart()`**](#large_orange_diamond-method-arestart)

<br>

### :diamond_shape_with_a_dot_inside: Property `duration`

Animation duration

<br>

### :diamond_shape_with_a_dot_inside: Property `easing`

Easing function used by the tween

<br>

### :diamond_shape_with_a_dot_inside: Property `clock`

Elapsed time since the animation started (excludes delay)

<br>

### :diamond_shape_with_a_dot_inside: Property `delayTime`

Configured delay before the animation starts

<br>

### :diamond_shape_with_a_dot_inside: Property `delayClock`

Elapsed time during the delay phase

<br>

### :diamond_shape_with_a_dot_inside: Property `fromValues`

Initial property values, set via [**`:from()`**](#large_orange_diamond-method-afrom)

<br>

### :diamond_shape_with_a_dot_inside: Property `toValues`

Target property values, set via [**`:to()`**](#large_orange_diamond-method-ato)

<br>

### :diamond_shape_with_a_dot_inside: Property `manual`

If true, the animation is not updated by the global scheduler

<br>


### :large_orange_diamond: Method `animate()`

Create a new animation instance.

```lua
-- @param (table) meter - Target meter instance
-- @param (number) duration - Animation duration
-- @param (string|function) [easing="linear"] - Easing function
-- @return (table) Animation instance
local a = animate(meter, duration, easing)
```

<br>


### :large_orange_diamond: Method `a:from()`

Define initial values for the animation.<br>
These values will be used as the starting point when the animation<br>
is created or restarted. They are applied immediately after creation.

> [!IMPORTANT]
> It prioritizes the execution of available methods and, if none exist, sets the property value.<br>
> Preference is given to methods, as they are generally more comprehensive than simply defining values.<br>
> The methods must exist within the [**Module meter**](./METER-README.md) and its submodules; otherwise, the options defined on the meters will be applied.

> [!NOTE]
> The number will be automatically formatted to avoid exponential notation.<br>
> The interpolation (tween) library returns numbers in exponential notation because they are very large, and Rainmeter does not accept exponents.

```lua
-- @param (table) attributes - Table of property names and their starting values
-- @return (table) Animation instance
a:from({ x = 0 })
```

<br>


### :large_orange_diamond: Method `a:to()`

Defines the target values for the animation.<br>
These values represent the final state toward which the animation will interpolate.

> [!IMPORTANT]
> It prioritizes the execution of available methods and, if none exist, sets the property value.<br>
> Preference is given to methods, as they are generally more comprehensive than simply defining values.<br>
> The methods must exist within the [**Module meter**](./METER-README.md) and its submodules; otherwise, the options defined on the meters will be applied.

> [!NOTE]
> It is also possible to create custom functions to be used in the animation.

```lua
-- @param (table) attributes - Table of property names and their target values
-- @return (table) Animation instance
a:to({ x = 100 })
```

<br>

### :large_orange_diamond: Method `a:delay()`

Define delay before animation starts.<br>
The animation will wait for the specified amount of time before
beginning interpolation.

> [!NOTE]
> Delay time is affected by [**`:pause()`**](#large_orange_diamond-method-apause) and reset by [**`:cancel()`**](#large_orange_diamond-method-acancel), [**`:reset()`**](#large_orange_diamond-method-areset), and [**`:restart()`**](#large_orange_diamond-method-arestart).

```lua
-- @param (number) tick - Delay duration
-- @return (table) Animation instance
a:delay( 25 )
```

<br>


### :large_orange_diamond: Method `a:create()`

Initializes the tween and registers the animation into the global scheduler.<br>
If [**`:from()`**](#large_orange_diamond-method-afrom) or [**`:to()`**](#large_orange_diamond-method-ato) are missing, the animation is ignored.<br>


> #### Manual vs Automatic Mode
> Automatic (default)
> - Managed globally
> - Recommended for most use cases
>
> Manual
> - Full control over update timing
> - Not affected by [**`animate.updateAll()`**](#large_orange_diamond-method-animateupdateall)


```lua
-- @param (table) opts - Optional settings
-- @field (boolean) manual - If true, the animation will not be
--   added to the global scheduler and must be updated manually via
--   :update(). Defaults to false.
--
-- @return (table) Animation instance
a:create()

-- with manual mode
a:create({ manual = true })
```

<br>


### :large_orange_diamond: Method `a:pause()`

Pause the animation.<br>
Stops updating the animation while preserving current progress.<br>
The animation can be resumed later with [**`:play()`**](#large_orange_diamond-method-aplay).

```lua
-- @return (table) Animation instance
a:pause()
```

<br>


### :large_orange_diamond: Method `a:play()`

Resume a paused animation.<br>
Continues updating the animation from where it was paused.<br>
Has no effect if the animation is already running or finished.

```lua
-- @return (table) Animation instance
a:play()
```

<br>


### :large_orange_diamond: Method `a:reset()`

Reset the animation to its initial state.<br>
Rewinds the animation without restarting it.<br>
The animation will stay in the [**`"idle"`**](#diamond_shape_with_a_dot_inside-property-playstate-read-only) state and will not be updated until a new call to [**`:create()`**](#large_orange_diamond-method-acreate) or [**`:restart()`**](#large_orange_diamond-method-arestart) is made.

```lua
-- @return (table) Animation instance
a:reset()
```
<br>


### :large_orange_diamond: Method `a:restart()`

Restart the animation from the beginning.<br>
Reinitializes the internal tween, resets the clock, and re-registers the animation into the scheduler.<br>

If the animation has already finished, this is the correct way to run it again.<br>
Internally, this method:
1. Recreates the tween instance
2. Resets the internal clock
3. Sets [**`playState`**](#diamond_shape_with_a_dot_inside-property-playstate-read-only) to [**`"running"`**](#diamond_shape_with_a_dot_inside-property-playstate-read-only)
4. Adds the animation back to the active scheduler list

> [!IMPORTANT]
> This method is different from [**`:reset()`**](#large_orange_diamond-method-areset)<br>
> [**`:reset()`**](#large_orange_diamond-method-areset) → only rewinds values and sets state to [**`"idle"`**](#diamond_shape_with_a_dot_inside-property-playstate-read-only)<br>
> [**`:restart()`**](#large_orange_diamond-method-arestart) → fully restarts execution ([**`running`**](#diamond_shape_with_a_dot_inside-property-playstate-read-only))


```lua
if a.playState == "finished" then
  -- @return (table) Animation instance
  a:restart()
end
```

<br>


### :large_orange_diamond: Method `a:reverse()`

Reverse the animation direction by swapping from/to values.<br>
Exchanges the [**`fromValues`**](#diamond_shape_with_a_dot_inside-property-fromvalues) and [**`toValues`**](#diamond_shape_with_a_dot_inside-property-tovalues) tables, so the next execution (via [**`:create()`**](#large_orange_diamond-method-acreate) or [**`:restart()`**](#large_orange_diamond-method-arestart)) will play in reverse.<br>

> [!IMPORTANT]
> If the animation is currently running, this method does not affect the active tween. To reverse during playback, cancel :cancel() first, then recreate or restart.

```lua
local a = animate(meter, 1.0)
  :from({ x = 0 })
  :to({ x = 100 })
  :create()

-- when finished...

-- @return (table) Animation instance
a:reverse() -- now goes from 100 to 0
```

<br>


### :large_orange_diamond: Method `a:cancel()`

Cancel the animation and return to the initial state.<br>
Stops the animation immediately, removes it from the scheduler, and restores all animated values to their initial state.<br>

This method fully interrupts the current execution.

#### Behavior
- Sets [**`playState`**](#diamond_shape_with_a_dot_inside-property-playstate-read-only) to [**`"idle"`**](#diamond_shape_with_a_dot_inside-property-playstate-read-only)
- Resets internal clock to `0`
- Restores values using the tween's initial state
- Removes the animation from the active scheduler list

#### When to use
- When you want to completely stop an animation
- When you want to discard current progress
- Before reconfiguring ([**`:from()`**](#large_orange_diamond-method-afrom) / [**`:to()`**](#large_orange_diamond-method-ato))

> [!NOTE]
> Unlike [**`:reset()`**](#large_orange_diamond-method-areset), this method guarantees that the animation will no longer be updated automatically.

```lua
-- @return (table) Animation instance

--- Basic cancel
local a = animate(meter, 100)
  :from({ x = 0 })
  :to({ x = 100 })
  :create()

-- later
a:cancel()


--- Cancel inside update loop
function rain:update(au, dt)
  animate.updateAll(dt)

  if a.toValues.x > 50 then
    a:cancel()
  end
end
```

<br>


### :large_orange_diamond: Method `animate.updateAll()`

Update all active animations.<br>
This function must be called once per frame.<br>
It iterates through all active animations and updates them.<br>
Finished animations are automatically removed.<br>

```lua
-- @param (number) dt - Delta time (time since last frame)
animate.updateAll(dt)
```

<br>


### :large_orange_diamond: Method `animate.finished()`

Check whether all active animations have finished.<br>
Iterates through the active scheduler list and returns `false` as soon as any non-manual animation is not in the [**`"finished"`**](#diamond_shape_with_a_dot_inside-property-playstate-read-only) state. Manual animations (`manual = true`) are ignored in this check, since they are not advanced by the scheduler.

```lua
-- @return (boolean) `true` if every non-manual animation has finished, `false` otherwise.
animate.finished()

if animate.finished() then
  print("All animations done")
end
```

---

<br>
<br>



# Future Extensions

Potential extensions include:

- Automatic looping (`repeat`)
- Automatic yoyo (continuous back-and-forth, beyond the current
  one-shot [**`:reverse()`**](#large_orange_diamond-method-areverse))
- Timeline composition

---

<br>
<br>


## :scroll: License

<a href="../../assets/images/logo-gpl-v2.png">
  <img src="../../assets/images/logo-gpl-v2.png" alt="LOGO-GPL-V2" width="150" height="150" align="right">
</a>

The **RainJIT** Plugin is licensed under the [**GPL v2.0 license**](../../LICENSE).<br>
This project also relies on external libraries that may use different open-source licenses.<br>
If you are contributing documentation or changes to the source code, please ensure that your contributions comply with the project's licensing guidelines.

<br>

---
