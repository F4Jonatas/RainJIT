---=========================================================================---
-- @file sunset.lua
-- @brief Dynamic day period helper for Rainmeter/Lua environments.
--
-- This module provides:
--   - Day phase detection
--   - Dynamic interpolation values
--   - Static color presets
--   - Sun position helpers
--   - Normalized time progress
--
-- Designed for lightweight UI animation systems.
--
-- License:
--   GPL-2.0
---=========================================================================---

---@module SunsetTime
local sunsetTime = {}


sunsetTime.colors = {
    night = {
        top    = "0B1026",
        bottom = "2B32B2",
        accent = "7C89FF"
    },

    dawn = {
        top    = "FF8A5B",
        bottom = "FFD49A",
        accent = "FFF2CC"
    },

    day = {
        top    = "56CCF2",
        bottom = "2F80ED",
        accent = "FFFFFF"
    },

    dusk = {
        top    = "FF512F",
        bottom = "DD2476",
        accent = "FFC09F"
    }
}

---=========================================================================---
-- Internal state
---=========================================================================---

local current = {
    hour     = 0,
    minute   = 0,
    second   = 0,
    progress = 0.0
}

---=========================================================================---
-- Time periods
---=========================================================================---

local periods = {
    night = { start = 0,  finish = 5  },
    dawn  = { start = 5,  finish = 8  },
    day   = { start = 8,  finish = 18 },
    dusk  = { start = 18, finish = 21 },
    night2 = { start = 21, finish = 24 }
}

---=========================================================================---
-- @brief Updates internal clock state.
---=========================================================================---

local function updateClock()
    local now = os.date("*t")

    current.hour   = now.hour
    current.minute = now.min
    current.second = now.sec

    current.progress =
        (current.hour * 3600 + current.minute * 60 + current.second) / 86400
end

---=========================================================================---
-- @brief Returns current hour.
--
-- @return integer
---=========================================================================---

function sunsetTime.getHour()
    updateClock()
    return current.hour
end

---=========================================================================---
-- @brief Returns formatted current time.
--
-- @return string
---=========================================================================---

function sunsetTime.getTime()
    updateClock()

    return string.format(
        "%02d:%02d:%02d",
        current.hour,
        current.minute,
        current.second
    )
end

---=========================================================================---
-- @brief Returns normalized daily progress.
--
-- Range:
--   0.0 -> midnight
--   0.5 -> noon
--   1.0 -> next midnight
--
-- @return number
---=========================================================================---

function sunsetTime.getProgress()
    updateClock()
    return current.progress
end

---=========================================================================---
-- @brief Returns current day phase.
--
-- Possible values:
--   night
--   dawn
--   day
--   dusk
--
-- @return string
---=========================================================================---

function sunsetTime.getPeriod()
    updateClock()

    local h = current.hour

    if h >= periods.night.start and h < periods.night.finish then
        return "night"
    end

    if h >= periods.dawn.start and h < periods.dawn.finish then
        return "dawn"
    end

    if h >= periods.day.start and h < periods.day.finish then
        return "day"
    end

    if h >= periods.dusk.start and h < periods.dusk.finish then
        return "dusk"
    end

    return "night"
end

---=========================================================================---
-- @brief Returns current color palette.
--
-- @return table
---=========================================================================---

function sunsetTime.getColors()
    local period = sunsetTime.getPeriod()
    return sunsetTime.colors[period]
end

---=========================================================================---
-- @brief Returns interpolated horizontal sun position.
--
-- @param width integer
-- @return integer
---=========================================================================---

function sunsetTime.getSunPositionX(width)
    updateClock()

    width = width or 400

    return math.floor(width * current.progress)
end

---=========================================================================---
-- @brief Returns interpolated vertical sun position.
--
-- Creates a simple sine-wave arc.
--
-- @param height integer
-- @return integer
---=========================================================================---

function sunsetTime.getSunPositionY(height)
    updateClock()

    height = height or 200

    local radians = current.progress * math.pi
    local offset  = math.sin(radians)

    return math.floor(height - (offset * height))
end

---=========================================================================---
-- @brief Returns opacity multiplier for stars.
--
-- Range:
--   0.0 -> invisible
--   1.0 -> fully visible
--
-- @return number
---=========================================================================---

function sunsetTime.getStarsOpacity()
    local period = sunsetTime.getPeriod()

    if period == "night" then
        return 1.0
    end

    if period == "dusk" then
        return 0.5
    end

    return 0.0
end

---=========================================================================---
-- @brief Returns true if current period is nighttime.
--
-- @return boolean
---=========================================================================---

function sunsetTime.isNight()
    local period = sunsetTime.getPeriod()

    return period == "night"
end

---=========================================================================---
-- @brief Returns true if current period is daytime.
--
-- @return boolean
---=========================================================================---

function sunsetTime.isDay()
    local period = sunsetTime.getPeriod()

    return period == "day"
end

---=========================================================================---
-- Module export
---=========================================================================---

return sunsetTime
