/**
 * @file interval.cpp
 * @brief Interval timer implementation using WM_COPYDATA
 * @license GPL v2.0 License
 */

#include "interval.hpp"
#include <Includes/rain.hpp>
#include <utils/strings.hpp>
#include <algorithm>
#include <sstream>
#include <cstdio>

namespace interval {

// Static map for global management
std::unordered_map<Rain*, IntervalManager*> g_managers;
std::atomic<int> g_nextTimerId(1);

// Forward declarations
static void TimerWorker(TimerInfo* timer);
static void CreateIntervalObject(lua_State* L, IntervalManager* manager, int timerId);
static void SendBangToRainmeter(const std::wstring& bang);

/**
 * @brief IntervalManager constructor.
 */
IntervalManager::IntervalManager(Rain* rain)
	: rain(rain), rainWindow(nullptr) {

	// Get Rainmeter main window handle
	rainWindow = FindWindow(L"DummyRainWClass", NULL);

	// Get current measure name for !CommandMeasure
	const wchar_t* measureName = RmGetMeasureName(rain->rm);
	if (measureName) {
		currentMeasureName = measureName;
	}
}

/**
 * @brief IntervalManager destructor.
 */
IntervalManager::~IntervalManager() {
	// Stop all timers
	std::lock_guard<std::mutex> lock(mtx);

	for (auto& pair : timers) {
		TimerInfo* timer = pair.second.get();
		if (timer) {
			timer->shouldStop = true;
			timer->condition.notify_all();

			// Wait for thread to finish
			if (timer->worker.joinable()) {
				timer->worker.join();
			}

			// Free Lua references
			if (timer->callbackRef != LUA_NOREF && rain && rain->L) {
				luaL_unref(rain->L, LUA_REGISTRYINDEX, timer->callbackRef);
			}
			if (timer->luaObjectRef != LUA_NOREF && rain && rain->L) {
				luaL_unref(rain->L, LUA_REGISTRYINDEX, timer->luaObjectRef);
			}
		}
	}

	timers.clear();
}

/**
 * @brief Create a new timer.
 */
int IntervalManager::createTimer(double intervalMs, int callbackRef, bool once) {
	if (intervalMs < 1.0 || callbackRef == LUA_NOREF) return 0;

	std::lock_guard<std::mutex> lock(mtx);

	int timerId = g_nextTimerId++;
	auto timer = std::make_unique<TimerInfo>();

	timer->id = timerId;
	timer->intervalMs = intervalMs;
	timer->once = once;
	timer->callbackRef = callbackRef;
	timer->rain = rain;
	timer->rainWindow = rainWindow;
	timer->measureName = currentMeasureName;
	timer->status = TIMER_STOPPED;
	timer->shouldStop = false;
	timer->isRunning = false;

	// Store timer
	timers[timerId] = std::move(timer);
	return timerId;
}

/**
 * @brief Get timer by ID.
 */
TimerInfo* IntervalManager::getTimer(int id) {
	std::lock_guard<std::mutex> lock(mtx);

	auto it = timers.find(id);
	return (it != timers.end()) ? it->second.get() : nullptr;
}

/**
 * @brief Start a timer.
 */
bool IntervalManager::startTimer(int id) {
	std::lock_guard<std::mutex> lock(mtx);

	auto it = timers.find(id);
	if (it == timers.end()) return false;

	TimerInfo* timer = it->second.get();

	if (timer->status == TIMER_RUNNING) return true;
	if (timer->status == TIMER_DELETED) return false;

	// Reset state
	timer->shouldStop = false;
	timer->elapsedMs = 0.0;
	timer->totalElapsedMs = 0.0;
	timer->callCount = 0;
	timer->startTime = std::chrono::steady_clock::now();

	// Start worker thread
	timer->worker = std::thread(TimerWorker, timer);
	timer->status = TIMER_RUNNING;

	return true;
}

/**
 * @brief Stop a timer.
 */
bool IntervalManager::stopTimer(int id) {
	std::lock_guard<std::mutex> lock(mtx);

	auto it = timers.find(id);
	if (it == timers.end()) return false;

	TimerInfo* timer = it->second.get();

	if (timer->status != TIMER_RUNNING &&
		timer->status != TIMER_PAUSED) return false;

	// Signal to stop
	timer->shouldStop = true;
	timer->condition.notify_all();

	// Wait for thread to finish
	if (timer->worker.joinable()) {
		timer->worker.join();
	}

	timer->status = TIMER_STOPPED;
	return true;
}

/**
 * @brief Pause a timer.
 */
bool IntervalManager::pauseTimer(int id) {
	std::lock_guard<std::mutex> lock(mtx);

	auto it = timers.find(id);
	if (it == timers.end()) return false;

	TimerInfo* timer = it->second.get();

	if (timer->status == TIMER_RUNNING) {
		timer->shouldStop = true;
		timer->condition.notify_all();

		if (timer->worker.joinable()) {
			timer->worker.join();
		}

		timer->status = TIMER_PAUSED;
		return true;
	}

	return false;
}

/**
 * @brief Restart a timer.
 */
bool IntervalManager::restartTimer(int id) {
	if (!stopTimer(id)) return false;
	return startTimer(id);
}

/**
 * @brief Delete a timer.
 */
bool IntervalManager::deleteTimer(int id) {
	std::lock_guard<std::mutex> lock(mtx);

	auto it = timers.find(id);
	if (it == timers.end()) return false;

	TimerInfo* timer = it->second.get();

	// Stop if running
	if (timer->status == TIMER_RUNNING ||
		timer->status == TIMER_PAUSED) {
		timer->shouldStop = true;
		timer->condition.notify_all();

		if (timer->worker.joinable()) {
			timer->worker.join();
		}
	}

	// Free Lua references
	if (timer->callbackRef != LUA_NOREF && rain && rain->L) {
		luaL_unref(rain->L, LUA_REGISTRYINDEX, timer->callbackRef);
	}
	if (timer->luaObjectRef != LUA_NOREF && rain && rain->L) {
		luaL_unref(rain->L, LUA_REGISTRYINDEX, timer->luaObjectRef);
	}

	// Mark as deleted
	timer->status = TIMER_DELETED;
	timers.erase(it);

	return true;
}

/**
 * @brief Set Lua object reference for timer.
 */
void IntervalManager::setTimerLuaObjectRef(int id, int luaRef) {
	std::lock_guard<std::mutex> lock(mtx);

	auto it = timers.find(id);
	if (it != timers.end()) {
		it->second->luaObjectRef = luaRef;
	}
}

/**
 * @brief Worker thread function.
 */
static void TimerWorker(TimerInfo* timer) {
	timer->isRunning = true;

	auto lastTime = std::chrono::steady_clock::now();
	auto nextFire = lastTime + std::chrono::milliseconds((int)timer->intervalMs);

	while (!timer->shouldStop) {
		// Wait until next fire time or stop signal
		{
			std::unique_lock<std::mutex> lock(timer->mutex);
			if (timer->condition.wait_until(lock, nextFire,
				[&](){ return timer->shouldStop == true; })) {
				// Stop signal received
				break;
			}
		}

		// Check if we should stop
		if (timer->shouldStop) break;

		// Calculate delta time and cumulated seconds
		auto now = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
			now - timer->startTime);

		// Calculate delta time since last frame (em segundos)
		auto frameElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
			now - lastTime);
		double deltaTime = frameElapsed.count() / 1000.0;  // dt em segundos
		lastTime = now;

		// Update timer state
		timer->elapsedMs = elapsed.count() % (int)timer->intervalMs;
		timer->totalElapsedMs = elapsed.count();
		timer->callCount++;

		// Cumulated seconds (tempo total desde o início em segundos)
		double cumulatedSeconds = timer->totalElapsedMs / 1000.0;

		// Build Lua command to execute callback
		if (timer->rainWindow && !timer->measureName.empty()) {
			std::wstring bang = L"!CommandMeasure \"";
			bang += timer->measureName;
			bang += L"\" \"";

			// Lua script to call the registered callback
			std::wstring luaScript = L"pcall(function() ";
			luaScript += L"local rainjit = rainjit or {} ";
			luaScript += L"rainjit._callbacks = rainjit._callbacks or {} ";
			luaScript += L"local cb = rainjit._callbacks[" + std::to_wstring(timer->id) + L"] ";
			luaScript += L"if type(cb) == 'function' then ";

			// Passar parâmetros: cs, dt, ct
			luaScript += L"cb("
				+ std::to_wstring(cumulatedSeconds) + L", "  // cs
				+ std::to_wstring(deltaTime) + L", "         // dt
				+ std::to_wstring(timer->callCount) + L") "; // ct

			luaScript += L"end ";
			luaScript += L"end)";

			// Escape quotes
			std::wstring escaped;
			for (wchar_t c : luaScript) {
				if (c == L'"') escaped += L'\\\"';
				else escaped += c;
			}

			bang += escaped;
			bang += L"\"";

			// Send bang to Rainmeter
			SendBangToRainmeter(bang);
		}

		// If one-shot, stop
		if (timer->once) {
			timer->shouldStop = true;
			break;
		}

		// Calculate next fire time
		nextFire += std::chrono::milliseconds((int)timer->intervalMs);

		// Adjust if we're running late
		now = std::chrono::steady_clock::now();
		if (nextFire < now) {
			// We're running behind, skip to next interval
			nextFire = now + std::chrono::milliseconds((int)timer->intervalMs);
		}
	}

	timer->isRunning = false;
	timer->status = TIMER_STOPPED;
}

/**
 * @brief Send bang to Rainmeter using WM_COPYDATA.
 */
static void SendBangToRainmeter(const std::wstring& bang) {
	HWND rmWnd = FindWindow(L"DummyRainWClass", NULL);
	if (!rmWnd || bang.empty()) return;

	COPYDATASTRUCT cds;
	cds.dwData = 1;  // Rainmeter expects 1 for bang commands
	cds.cbData = (DWORD)((bang.length() + 1) * sizeof(wchar_t));
	cds.lpData = (PVOID)bang.c_str();

	SendMessage(rmWnd, WM_COPYDATA, 0, (LPARAM)&cds);
}

/**
 * @brief Lua: interval(ms, callback [, once]) -> interval object.
 */
static int LuaInterval(lua_State* L) {
	auto* rain = static_cast<Rain*>lua_touserdata(L, lua_upvalueindex(1));
	if (!rain || !rain->L) {
		lua_pushnil(L);
		lua_pushstring(L, "Invalid Rain instance");
		return 2;
	}

	// Get parameters
	double intervalMs = luaL_checknumber(L, 1);

	if (intervalMs < 1.0) {
		lua_pushnil(L);
		lua_pushstring(L, "Interval must be at least 1ms");
		return 2;
	}

	if (!lua_isfunction(L, 2)) {
		lua_pushnil(L);
		lua_pushstring(L, "Callback must be a function");
		return 2;
	}

	bool once = false;
	if (lua_gettop(L) >= 3 && !lua_isnil(L, 3)) {
		once = lua_toboolean(L, 3);
	}

	// Get or create manager
	IntervalManager* manager = GetManager(rain);
	if (!manager) {
		lua_pushnil(L);
		lua_pushstring(L, "Failed to create interval manager");
		return 2;
	}

	// Create reference to callback in registry
	lua_pushvalue(L, 2);  // Copy function
	int callbackRef = luaL_ref(L, LUA_REGISTRYINDEX);

	// Create timer
	int timerId = manager->createTimer(intervalMs, callbackRef, once);
	if (timerId == 0) {
		luaL_unref(L, LUA_REGISTRYINDEX, callbackRef);
		lua_pushnil(L);
		lua_pushstring(L, "Failed to create timer");
		return 2;
	}

	// Register callback in global Lua table for ExecuteBang to find
	lua_getglobal(L, "rainjit");
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		lua_newtable(L);
		lua_setglobal(L, "rainjit");
		lua_getglobal(L, "rainjit");
	}

	lua_getfield(L, -1, "_callbacks");
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		lua_newtable(L);
		lua_pushvalue(L, -1);
		lua_setfield(L, -3, "_callbacks");
	}

	// Store callback reference
	lua_pushvalue(L, 2);  // Original function
	lua_rawseti(L, -2, timerId);
	lua_pop(L, 2);  // Pop _callbacks and rainjit tables

	// Create Lua interval object
	CreateIntervalObject(L, manager, timerId);

	return 1;
}

/**
 * @brief Create Lua interval object with methods and properties.
 */
static void CreateIntervalObject(lua_State* L, IntervalManager* manager, int timerId) {
	// Create new table for the object
	lua_newtable(L);

	// Method: stop()
	lua_pushlightuserdata(L, manager);
	lua_pushinteger(L, timerId);
	lua_pushcclosure(L, [](lua_State* L) -> int {
		IntervalManager* mgr = (IntervalManager*)lua_touserdata(L, lua_upvalueindex(1));
		int id = (int)lua_tointeger(L, lua_upvalueindex(2));

		bool success = mgr ? mgr->stopTimer(id) : false;
		lua_pushboolean(L, success);
		return 1;
	}, 2);
	lua_setfield(L, -2, "stop");

	// Method: restart()
	lua_pushlightuserdata(L, manager);
	lua_pushinteger(L, timerId);
	lua_pushcclosure(L, [](lua_State* L) -> int {
		IntervalManager* mgr = (IntervalManager*)lua_touserdata(L, lua_upvalueindex(1));
		int id = (int)lua_tointeger(L, lua_upvalueindex(2));

		bool success = mgr ? mgr->restartTimer(id) : false;
		lua_pushboolean(L, success);
		return 1;
	}, 2);
	lua_setfield(L, -2, "restart");

	// Method: pause()
	lua_pushlightuserdata(L, manager);
	lua_pushinteger(L, timerId);
	lua_pushcclosure(L, [](lua_State* L) -> int {
		IntervalManager* mgr = (IntervalManager*)lua_touserdata(L, lua_upvalueindex(1));
		int id = (int)lua_tointeger(L, lua_upvalueindex(2));

		bool success = mgr ? mgr->pauseTimer(id) : false;
		lua_pushboolean(L, success);
		return 1;
	}, 2);
	lua_setfield(L, -2, "pause");

	// Method: play()
	lua_pushlightuserdata(L, manager);
	lua_pushinteger(L, timerId);
	lua_pushcclosure(L, [](lua_State* L) -> int {
		IntervalManager* mgr = (IntervalManager*)lua_touserdata(L, lua_upvalueindex(1));
		int id = (int)lua_tointeger(L, lua_upvalueindex(2));

		bool success = mgr ? mgr->startTimer(id) : false;
		lua_pushboolean(L, success);
		return 1;
	}, 2);
	lua_setfield(L, -2, "play");

	// Method: delete()
	lua_pushlightuserdata(L, manager);
	lua_pushinteger(L, timerId);
	lua_pushcclosure(L, [](lua_State* L) -> int {
		IntervalManager* mgr = (IntervalManager*)lua_touserdata(L, lua_upvalueindex(1));
		int id = (int)lua_tointeger(L, lua_upvalueindex(2));

		bool success = mgr ? mgr->deleteTimer(id) : false;
		lua_pushboolean(L, success);
		return 1;
	}, 2);
	lua_setfield(L, -2, "delete");

	// Create metatable with properties
	lua_newtable(L);

	// __index for properties
	lua_pushlightuserdata(L, manager);
	lua_pushinteger(L, timerId);
	lua_pushcclosure(L, [](lua_State* L) -> int {
		IntervalManager* mgr = (IntervalManager*)lua_touserdata(L, lua_upvalueindex(1));
		int id = (int)lua_tointeger(L, lua_upvalueindex(2));
		const char* key = luaL_checkstring(L, 2);

		TimerInfo* timer = mgr ? mgr->getTimer(id) : nullptr;
		if (!timer) {
			lua_pushnil(L);
			return 1;
		}

		if (strcmp(key, "id") == 0) {
			lua_pushinteger(L, timer->id);
		}
		else if (strcmp(key, "status") == 0) {
			switch(timer->status) {
				case TIMER_STOPPED: lua_pushstring(L, "stopped"); break;
				case TIMER_RUNNING: lua_pushstring(L, "running"); break;
				case TIMER_PAUSED:  lua_pushstring(L, "paused"); break;
				case TIMER_DELETED: lua_pushstring(L, "deleted"); break;
				default: lua_pushstring(L, "unknown");
			}
		}
		else if (strcmp(key, "once") == 0) {
			lua_pushboolean(L, timer->once);
		}
		else if (strcmp(key, "callCount") == 0) {
			lua_pushinteger(L, timer->callCount);
		}
		else if (strcmp(key, "interval") == 0) {
			lua_pushnumber(L, timer->intervalMs);
		}
		else if (strcmp(key, "elapsed") == 0) {
			lua_pushnumber(L, timer->elapsedMs);
		}
		else if (strcmp(key, "totalElapsed") == 0) {
			lua_pushnumber(L, timer->totalElapsedMs);
		}
		else {
			lua_pushnil(L);
		}

		return 1;
	}, 2);
	lua_setfield(L, -2, "__index");

	// __newindex (read-only properties)
	lua_pushcfunction(L, [](lua_State* L) -> int {
		lua_pushstring(L, "Interval properties are read-only");
		return lua_error(L);
	});
	lua_setfield(L, -2, "__newindex");

	// __gc for automatic cleanup
	lua_pushlightuserdata(L, manager);
	lua_pushinteger(L, timerId);
	lua_pushcclosure(L, [](lua_State* L) -> int {
		IntervalManager* mgr = (IntervalManager*)lua_touserdata(L, lua_upvalueindex(1));
		int id = (int)lua_tointeger(L, lua_upvalueindex(2));

		if (mgr) {
			mgr->deleteTimer(id);

			// Remove from Lua callback table
			lua_getglobal(L, "rainjit");
			if (!lua_isnil(L, -1)) {
				lua_getfield(L, -1, "_callbacks");
				if (!lua_isnil(L, -1)) {
					lua_pushnil(L);
					lua_rawseti(L, -2, id);
				}
				lua_pop(L, 1); // _callbacks
			}
			lua_pop(L, 1); // rainjit
		}
		return 0;
	}, 2);
	lua_setfield(L, -2, "__gc");

	// Apply metatable
	lua_setmetatable(L, -2);

	// Store Lua object reference
	lua_pushvalue(L, -1); // Copy object
	int luaRef = luaL_ref(L, LUA_REGISTRYINDEX);
	manager->setTimerLuaObjectRef(timerId, luaRef);

	// Auto-start the timer
	manager->startTimer(timerId);
}

/**
 * @brief Module entry point for require("interval").
 */
extern "C" int luaopen_interval(lua_State* L) {
	auto* rain = static_cast<Rain*>lua_touserdata(L, lua_upvalueindex(1));

	// Create module table that's callable
	lua_newtable(L);

	// Make the table callable (interval(ms, callback, once))
	lua_pushlightuserdata(L, rain);
	lua_pushcclosure(L, LuaInterval, 1);

	// Set as __call metamethod
	lua_newtable(L);
	lua_pushvalue(L, -2);
	lua_setfield(L, -2, "__call");
	lua_setmetatable(L, -3);

	return 1;
}

/**
 * @brief Get or create interval manager for Rain instance.
 */
IntervalManager* GetManager(Rain* rain) {
	auto it = g_managers.find(rain);
	if (it != g_managers.end()) {
		return it->second;
	}

	// Create new manager
	IntervalManager* manager = new IntervalManager(rain);
	g_managers[rain] = manager;
	return manager;
}

/**
 * @brief Remove manager for Rain instance.
 */
void RemoveManager(Rain* rain) {
	auto it = g_managers.find(rain);
	if (it != g_managers.end()) {
		delete it->second;
		g_managers.erase(it);
	}
}

/**
 * @brief Register interval module in Lua package.preload.
 */
void RegisterModule(lua_State* L, Rain* rain) {
	lua_getglobal(L, "package");
	lua_getfield(L, -1, "preload");

	lua_pushlightuserdata(L, rain);
	lua_pushcclosure(L, luaopen_interval, 1);
	lua_setfield(L, -2, "interval");

	lua_pop(L, 2);
}

/**
 * @brief Cleanup all interval resources for Rain instance.
 */
void Cleanup(Rain* rain) {
	RemoveManager(rain);
}

} // namespace interval
