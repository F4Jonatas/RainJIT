/**
 * @file interval.hpp
 * @brief High-precision interval timer module for RainJIT.
 * @license GPL v2.0 License
 *
 * Provides Lua bindings for precise interval timers running in a
 * dedicated thread, independent of Rainmeter's update cycle.
 *
 * @module interval
 * @usage local interval = require("interval")
 *        local timer = interval(16.67, function(count, elapsed)
 *            -- 60 FPS animation
 *        end, false) -- once = false (repeat)
 *
 * @details
 * - Uses dedicated thread with QueryPerformanceCounter for microsecond precision
 * - Each timer is a Lua object with methods: stop, restart, pause, play, delete
 * - Properties: id, status, once
 * - Automatic cleanup on garbage collection
 * - Thread-safe operation
 *
 * @example
 * @code{.lua}
 * local interval = require("interval")
 *
 * -- Create 60 FPS timer
 * local animTimer = interval(16.67, function(count, elapsedMs)
 *     print("Frame:", count, "Elapsed:", elapsedMs, "ms")
 *     rain:setVar("Progress", (elapsedMs % 1000) / 1000)
 * end)
 *
 * -- Control the timer
 * animTimer:pause()   -- Pause animation
 * animTimer:play()    -- Resume
 * animTimer:stop()    -- Stop and reset
 * animTimer:restart() -- Stop and start again
 * animTimer:delete()  -- Remove completely
 *
 * -- Access properties
 * print("Timer ID:", animTimer.id)
 * print("Status:", animTimer.status)  -- "running", "paused", "stopped"
 * print("Once:", animTimer.once)      -- false (repeating)
 * @endcode
 */

#pragma once

#include <Windows.h>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <functional>
#include <unordered_map>
#include <memory>
#include <queue>
#include <lua.hpp>
#include <chrono>

struct Rain;

namespace interval {

/**
 * @brief Timer status enum.
 */
enum TimerStatus {
    TIMER_STOPPED,    ///< Timer created but not started
    TIMER_RUNNING,    ///< Timer actively running
    TIMER_PAUSED,     ///< Timer paused (retains elapsed time)
    TIMER_DELETED     ///< Timer marked for deletion
};

/**
 * @brief Timer information with full control.
 */
struct TimerInfo {
    int id;                     ///< Unique timer ID
    double intervalMs;          ///< Interval in milliseconds
    double elapsedMs;           ///< Elapsed time since last trigger
    double totalElapsedMs;      ///< Total elapsed since start
    TimerStatus status;         ///< Current status
    bool once;                  ///< Execute once or repeat
    int callCount;              ///< Number of times executed

    // Thread control
    std::thread worker;         ///< Worker thread
    std::atomic<bool> shouldStop; ///< Flag to stop thread
    std::atomic<bool> isRunning;  ///< Is thread currently running
    mutable std::mutex mutex;   ///< Mutex for synchronization (mutable for const functions)
    std::condition_variable condition; ///< Condition variable for waiting

    // Timing
    std::chrono::steady_clock::time_point startTime; ///< Start time for precise timing
		std::chrono::steady_clock::time_point lastFrameTime; ///< Last frame time for precise timing

    // Lua references
    int callbackRef;            ///< Lua registry reference to callback function
    int luaObjectRef;           ///< Lua registry reference to interval object

    // Rainmeter context
    Rain* rain;                 ///< Parent Rain instance
    HWND rainWindow;            ///< Rainmeter window handle
    std::wstring measureName;   ///< Current measure name for !CommandMeasure

    /**
     * @brief Default constructor.
     */
    TimerInfo()
        : id(0), intervalMs(0), elapsedMs(0), totalElapsedMs(0),
          status(TIMER_STOPPED), once(false), callCount(0),
          shouldStop(false), isRunning(false),
          callbackRef(LUA_NOREF), luaObjectRef(LUA_NOREF),
          rain(nullptr), rainWindow(nullptr),
					lastFrameTime(std::chrono::steady_clock::now())
					{}

    /**
     * @brief Destructor - ensures thread is stopped.
     */
    ~TimerInfo() {
        // Ensure thread is stopped
        if (worker.joinable()) {
            shouldStop = true;
            condition.notify_all();
            worker.join();
        }
    }

    /**
     * @brief Get status as string.
     * @return Status string representation.
     */
    std::string getStatusString() const {
        switch(status) {
            case TIMER_STOPPED: return "stopped";
            case TIMER_RUNNING: return "running";
            case TIMER_PAUSED:  return "paused";
            case TIMER_DELETED: return "deleted";
            default: return "unknown";
        }
    }

    /**
     * @brief Reset timer completely.
     */
    void reset() {
        elapsedMs = 0.0;
        totalElapsedMs = 0.0;
        callCount = 0;
        startTime = std::chrono::steady_clock::now();
				lastFrameTime = startTime;  // ⬅️ RESETAR também
    }
};

/**
 * @brief Interval manager structure.
 */
struct IntervalManager {
    // Internal data
    mutable std::mutex mtx;  ///< Mutex for thread-safe operations (mutable for const methods)

    std::unordered_map<int, std::unique_ptr<TimerInfo>> timers; ///< Active timers

    // Parent context
    Rain* rain;                     ///< Parent Rain instance
    HWND rainWindow;                ///< Rainmeter window handle
    std::wstring currentMeasureName; ///< Current measure name

    /**
     * @brief Constructor.
     * @param rain Parent Rain instance.
     */
    IntervalManager(Rain* rain);

    /**
     * @brief Destructor - cleans up all timers.
     */
    ~IntervalManager();

    // Disable copy
    IntervalManager(const IntervalManager&) = delete;
    IntervalManager& operator=(const IntervalManager&) = delete;

    /**
     * @brief Create a new timer.
     * @param intervalMs Interval in milliseconds.
     * @param callbackRef Lua registry reference to callback function.
     * @param once true=execute once, false=repeat.
     * @return Timer ID (>0) or 0 on error.
     */
    int createTimer(double intervalMs, int callbackRef, bool once);

    /**
     * @brief Get timer by ID.
     * @param id Timer ID.
     * @return Pointer to TimerInfo or nullptr.
     */
    TimerInfo* getTimer(int id);

    /**
     * @brief Start a timer.
     * @param id Timer ID.
     * @return true on success, false on error.
     */
    bool startTimer(int id);

    /**
     * @brief Stop a timer (reset).
     * @param id Timer ID.
     * @return true on success, false on error.
     */
    bool stopTimer(int id);

    /**
     * @brief Pause a timer.
     * @param id Timer ID.
     * @return true on success, false on error.
     */
    bool pauseTimer(int id);

    /**
     * @brief Restart a timer (stop + start).
     * @param id Timer ID.
     * @return true on success, false on error.
     */
    bool restartTimer(int id);

    /**
     * @brief Delete a timer.
     * @param id Timer ID.
     * @return true on success, false on error.
     */
    bool deleteTimer(int id);

    /**
     * @brief Set Lua object reference for timer.
     * @param id Timer ID.
     * @param luaRef Lua registry reference.
     */
    void setTimerLuaObjectRef(int id, int luaRef);

    /**
     * @brief Get number of active timers.
     * @return Count of active timers.
     */
    size_t timerCount() const {
        std::lock_guard<std::mutex> lock(mtx);  // ✅ mtx é mutable
        return timers.size();
    }
};

/**
 * @brief Global context map: Rain* → IntervalManager*
 */
extern std::unordered_map<Rain*, IntervalManager*> g_managers;

/**
 * @brief Get or create interval manager for Rain instance.
 * @param rain Parent Rain instance.
 * @return Pointer to IntervalManager.
 */
IntervalManager* GetManager(Rain* rain);

/**
 * @brief Remove manager for Rain instance.
 * @param rain Parent Rain instance.
 */
void RemoveManager(Rain* rain);

/**
 * @brief Lua module entry point (called by require("interval"))
 * @param L Lua state.
 * @return Number of return values (1 - module table).
 */
extern "C" int luaopen_interval(lua_State* L);

/**
 * @brief Register interval module in Lua package.preload.
 * @param L Lua state.
 * @param rain Parent Rain instance.
 */
void RegisterModule(lua_State* L, Rain* rain);

/**
 * @brief Cleanup all interval resources for Rain instance.
 * @param rain Parent Rain instance.
 */
void Cleanup(Rain* rain);

} // namespace interval
