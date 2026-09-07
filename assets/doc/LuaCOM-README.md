<!-- https://web.tecgraf.puc-rio.br/~rcerq/luacom/pub/1.3/luacom-htmldoc/ -->
<!-- https://web.tecgraf.puc-rio.br/~rcerq/luacom/ -->

<div align="center">

  # LuaCOM

  ### Microsoft Component Object Model (COM) binding for Lua

  <br>
  <br>


  <img src="../images/com-logo.png" alt="LOGO" width="200" height="200">

</div>




## Summary

<details>

<summary><ins>Table of contents</ins></summary>

- [Overview](#overview)
  - [Locating COM Objects](#beginner-locating-com-objects)
- [Features](#green_book-features)
- [Quick Example](#jigsaw-quick-example)
- [Module `luaCOM`](#book-module-luacom)
  - [Method `luacom.CreateObject()`](#large_orange_diamond-method-luacomcreateobject)
  - [Method `luacom.Connect()`](#large_orange_diamond-method-luacomconnect)
  - [Method `luacom.ImplInterface()`](#large_orange_diamond-method-luacomimplinterface)
  - [Method `luacom.ImplInterfaceFromTypelib()`](#large_orange_diamond-method-luacomimplinterfacefromtypelib)
  - [Method `luacom.GetObject()`](#large_orange_diamond-method-luacomgetobject)
  - [Method `luacom.NewObject() NewControl`](#large_orange_diamond-method-luacomnewobject-newcontrol)
  - [Method `luacom.ExposeObject()`](#large_orange_diamond-method-luacomexposeobject)
  - [Method `luacom.RevokeObject()`](#large_orange_diamond-method-luacomrevokeobject)
  - [Method `luacom.RegisterObject()`](#large_orange_diamond-method-luacomregisterobject)
  - [Method `luacom.UnRegisterObject()`](#large_orange_diamond-method-luacomunregisterobject)
  - [Method `luacom.addConnection()`](#large_orange_diamond-method-luacomaddconnection)
  - [Method `luacom.releaseConnection()`](#large_orange_diamond-method-luacomreleaseconnection)
  - [Method `luacom.ProgIDfromCLSID()`](#large_orange_diamond-method-luacomprogidfromclsid)
  - [Method `luacom.CLSIDfromProgID()`](#large_orange_diamond-method-luacomclsidfromprogid)
  - [Method `luacom.ShowHelp()`](#large_orange_diamond-method-luacomshowhelp)
  - [Method `luacom.GetIUnknown()`](#large_orange_diamond-method-luacomgetiunknown)
  - [Method `luacom.isMember()`](#large_orange_diamond-method-luacomismember)
  - [Method `luacom.StartLog()`](#large_orange_diamond-method-luacomstartlog)
  - [Method `luacom.EndLog()`](#large_orange_diamond-method-luacomendlog)
  - [Method `luacom.GetEnumerator()`](#large_orange_diamond-method-luacomgetenumerator)
- [License](#scroll-license)

</details>

<br>
<br>


## Overview

LuaCOM is an add-on library to the [**Lua language**](http://www.lua.org/) that allows Lua programs to use and implement objects that follow **Microsoft Component Object Model (COM)** specification and use the **ActiveX technology** for property access and method calls.<br>

<br>


### :beginner: Locating COM Objects

The first step to use a COM object is to find it. COM objects are registered in the system registry and are associated with an unique Class Identifier, known as CLSID. A CLSID may also be associated with a string known as Programmatic Identifier or ProgID. This last one is the easiest way to reference a COM object. E.g., the ProgID for Microsoft Word is [**`Word.Application`**](https://learn.microsoft.com/pt-br/office/vba/api/word.application).

If one do not know in advance what is the CLSID or the ProgID of the object of interest, them it's possible to use tools like [**OleView**](https://learn.microsoft.com/pt-br/windows/win32/com/ole-com-object-viewer) to find the object, although the best place to find it is in the object's documentation.

<br>


## :green_book: Features

- Dynamic instantiation of COM objects registered in the system registry, via the `CreateObject` method
- Dynamic access to running COM objects via `GetObject`
- COM method calls as normal Lua function calls and property accesses as normal table field accesses
- Ability to read type libraries and to generate HTML documentation on-the-fly for COM objects
- Use of COM objects without type information
- Type conversion between OLE Automation types and Lua types
- Object disposal using Lua garbage collection mechanism
- Implementation of COM interfaces and objects using Lua tables
- ~Implementation of OLE controls using Lua tables (needs a Lua GUI toolkit that can create in-place windows, like IUP)~
- Use of COM connection point mechanism for bidirectional communication and event handling
- Fully compatible with Lua 5 and with [**LuaJIT**](https://luajit.org/)
- Log mechanism to ease the debugging of applications

<br>
<br>


## :jigsaw: Quick Example

**With the ProgID or the CLSID of an object, it's now possible to create a new instance of it or to get a running instance.**<br>
**To do so, the easiest way is to use the method [`CreateObject`](#large_orange_diamond-method-luacomcreateobject) of the Lua API.**

```lua
-- @see https://github.com/F4Jonatas/RainJIT#large_orange_diamond-method-import
local luacom = import("luacom")

-- @see https://learn.microsoft.com/en-us/previous-versions/windows/desktop/wmp/player-object
local player = luacom.CreateObject("WMPlayer.OCX")
assert( player )
player.uiMode = "invisible"
```


**If there is an already running instance of the object you want, [`GetObject`](#large_orange_diamond-method-luacomgetobject) must be used to use it. The following code illustrates this.**

```lua
-- @see https://github.com/F4Jonatas/RainJIT#large_orange_diamond-method-import
local luacom = import("luacom")

-- If there is an instance of Word(r) running, it will end it
-- @see https://learn.microsoft.com/pt-br/office/vba/api/word.application
local word = luacom.GetObject("Word.Application")
if word then
  word:Quit()
  word = nil
end
```

---

<br>
<br>


## :book: Module `luaCOM`

### :large_orange_diamond: Method `luacom.CreateObject()`

This method finds the Class ID referenced by the ID parameter and creates an instance of the object with this Class ID.<br>
If there is any problem (ProgID not found, error instantiating object), the method returns nil.

```lua
-- @usage luacom.CreateObject(progID)
-- @param (string) progID
-- @return (table|nil) LuaCOM instance
local inet = luacom.CreateObject("InetCtls.Inet")

if inet == nil then
  print("Error! Object could not be created!")
end
```

<br>


### :large_orange_diamond: Method `luacom.Connect()`

This method finds the default source interface of the object `luacom_obj`, creates an instance of this interface whose implementation is given by `implementation_table` and creates a connection point between the `luacom_obj` and the implemented source interface. Any calls made by the `luacom_obj` to the source interface implementation will be translated to Lua calls to member function present in the `implementation_table`. If the method succeeds, the LuaCOM instance implemented by `implementation_table`, plus a cookie that identifies the connection, are returned; otherwise, nil is returned.

Notice that, to receive events, it's necessary to have a Windows message loop.

```lua
local events_handler = {}
function events_handler:NewValue(new_value)
  print(new_value)
end


-- @usage luacom.Connect(luacom, implementation_table)
-- @param (table) luacom - LuaCOM instance
-- @param (table|userdata) implementation_table
-- @return (table|nil) LuaCOM instance
-- @return (number) cookie
local events_obj = luacom.Connect(luacom, events_handler)
```

<br>


### :large_orange_diamond: Method `luacom.ImplInterface()`

This method finds the type library associated with the ProgID and tries to find the type information of an interface called `interface_name`. If it does, then creates an object whose implementation is `impl_table`, that is, any method call or property access on this object is translated to calls or access on the members of the table. Then it makes a LuaCOM instance for the implemented interface and returns it.<br>
If there are any problems in the process (ProgID not found, interface not found, interface isn't a `dispinterface`), the method returns nil.

```lua
-- @usage luacom.ImplInterface(impl_table, ProgID, interface_name)
-- @param (table|userdata) impl_table
-- @param (string) ProgID
-- @param (string) interface_name
-- @return (table|nil) implemented_obj - LuaCOM instance

local myobject = {}
myobject.Property = "teste"

function myobject:MyMethod()
  print("My method!")
end



local COM = luacom.ImplInterface(myobject, "TEST.Test", "ITest")

-- these are done via Lua
myobject:MyMethod()
print( myobject.Property )

-- this call is done through COM
COM:MyMethod()
print( COM.Property )
```

<br>


### :large_orange_diamond: Method `luacom.ImplInterfaceFromTypelib()`

This method loads the type library whose file path is "typelib_path" and tries to find the type information of an interface called "interface_name". If it does, then creates an object whose implementation is "impl_table", that is, any method call or property access on this object is translated to calls or access on the members of the table. Then it makes a LuaCOM instance for the implemented interface and returns it. If there are any problems in the process (ProgID not found, interface not found, interface isn't a dispinterface), the method returns nil. The "coclass_name" parameter is optional; it is only needed if the resulting LuaCOM instance is to be passed to the methods `Connect`, `AddConnection` or `ExposeObject`. This parameter specifies the Component Object class name to which the interface belongs, as one interface may be used in more than one "coclass".

```lua
-- @usage luacom.ImplInterfaceFromTypelib(impl_table, typelib_path, interface_name [, coclass_name])
-- @param (table|userdata) impl_table
-- @param (string) typelib_path
-- @param (string) interface_name
-- @param (string) [coclass_name]
-- @return (table|nil) implemented_obj - LuaCOM instance

local myobject = {}
myobject.Property = "teste"

function myobject:MyMethod()
  print("My method!")
end

local luacom_obj = luacom.ImplInterfaceFromTypelib(myobject, "test.tlb", "ITest", "Test")

-- these are done via Lua
myobject:MyMethod()
print(myobject.Property)

-- this call is done through COM
luacom_obj:MyMethod()
print( luacom_obj.Property )
```


<br>


### :large_orange_diamond: Method `luacom.GetObject()`

The first version method finds the Class ID referenced by the ProgID parameter and tries to find a running instance of the object having this Class ID. If there is any problem (ProgID not found, object is not running), the method returns nil.
The second version tries to find an object through its moniker. If there is any problem, the method returns nil.

```lua
-- @usage luacom.GetObject(ProgID)
-- @param (string) ProgID
-- @return (table|nil) luacom_obj - LuaCOM instance

local excel = luacom.GetObject("Excel.Application")
if excel == nil then
  print("Error! Could not get object!")
end
```

<br>


### :large_orange_diamond: Method `luacom.NewObject() NewControl`

This method is analogous to `ImplInterface`, doing just a step further: it locates the default interface for the ProgID and uses its type information. That is, this method creates a Lua implementation of a COM object's default interface. This is useful when implementing a complete COM object in Lua. It also creates a connection point for sending events to the client application and returns it as the second return value. If there are any problems in the process (ProgID not found, default interface is not a dispinterface etc), the method returns nil twice and returns the error message as the third return value.

To send events to the client application, just call methods of the event sink table returned. The method call will be translated to COM calls to each connection. These calls may contain parameters (as specified in the type information).

```lua
-- @usage luacom.NewObject(impl_table, ProgID)  - Creates a COM object
-- @usage luacom.NewControl(impl_table, ProgID) - Creates an OLE control
-- @param (table|userdata) impl_table
-- @param (string) ProgID
-- @return (table|nil) - LuaCOM instance
-- @return (table|nil) - Event sink
-- @return (string|nil) - Error message in the case of failure


local myobject = {}
myobject.Property = "teste"

function myobject:MyMethod()
  print("My method!")
end


local obj, evt, err = luacom.NewObject(myobject, "TEST.Test")

-- these are done via Lua
myobject:MyMethod()
print(myobject.Property)

-- this call is done through COM
luacom_obj:MyMethod()
print(luacom_obj.Property)

-- here we sink events
evt:Event1()
```

<br>


### :large_orange_diamond: Method `luacom.ExposeObject()`

This method creates and registers a class factory for `luacom_obj`, so that other running applications can use it. It returns a cookie that must be used to unregister the object. If the method fails, it returns nil.

**ATTENTION**: the object MUST be unregistered (using `RevokeObject`) before calling `luacom_close` or `lua_close`, otherwise unhandled exceptions might occur.

```lua
-- @usage luacom.ExposeObject(luacom_obj)
-- @param (table) luacom_obj - LuaCOM instance
-- @return (number|nil) cookie

local myobject = luacom.NewObject(impl_table, "Word.Application")
local cookie = luacom.ExposeObject(myobject)

function end_of_application()
  luacom.RevokeObject(cookie)
end
```

<br>


### :large_orange_diamond: Method `luacom.RevokeObject()`

Revokes a previously registered `ExposeObject` operation.

```lua
-- @usage luacom.RevokeObject(cookie)
-- @param (number) cookie
-- @return (boolean|nil)

local myobject = luacom.NewObject(impl_table, "Word.Application")
local cookie = luacom.ExposeObject(myobject)
assert(luacom.RevokeObject(cookie))
```

<br>


### :large_orange_diamond: Method `luacom.RegisterObject()`

This method creates the necessary registry entries for a COM object, using the information in `registration_info` table. If the component is successfully registered, the method returns a non-nil value.

The `registration_info` table must contain the following fields:

- **VersionIndependentProgID** This field must contain a string describing the programmatic identifier for the component, e.g. "MyCompany.MyApplication".
- **ProgID** The same as VersionIndependentProgID but with a version number, e.g. "MyCompany.MyApplication.2".
- **TypeLib** The file name of the type library describing the component. This file name should contain a path, if the type library isn't in the same folder of the executable. Samples: `mytypelib.tlb`, `c:\app\test.tlb`, `test.exe\1` (this last one can be used when the type library is bound to the executable as a resource).
- **Control** Must be `true` if the object is an OLE control, and `false` or nil otherwise.
- **CoClass** The name of the component class. There must be a coclass entry in the type library with the same name or the registration will fail.
- **ComponentName** This is the human-readable name of the component.
- **Arguments** This field specifies what arguments will be supplied to the component executable when started via COM. Normally it should contain "/Automation".
- **ScriptFile** This field specifies the full path of the script file that implements the component. Only used to register in-process servers.

This method is not a generic "registering tool" for COM components, as it assumes the component to be registered is implemented by the running executable during registration.

```lua
-- @usage luacom.RegisterObject(registration_info)
-- @param (table) registration_info - Registration information
-- @return (boolean) status

-- Lua registration code
local function RegisterComponent()
  local reginfo = {}
  reginfo.VersionIndependentProgID = "TESTE.Teste"

  -- Adds version information
  reginfo.ProgID = reginfo.VersionIndependentProgID..".1"
  reginfo.TypeLib = "teste.tlb"
  reginfo.CoClass = "Teste"
  reginfo.ComponentName = "Test Component"
  reginfo.Arguments = "/Automation"
  reginfo.ScriptFile = "teste.lua"

  local res = luacom.RegisterObject(reginfo)
  return res
end
```

<br>


### :large_orange_diamond: Method `luacom.UnRegisterObject()`

This method removes the registry entries for a COM object, using the information in `registration_info` table. If the component is successfully unregistered, the method returns a non-nil value.

The `registration_info` table must contain the following fields:

- **VersionIndependentProgID** This field must contain a string describing the programmatic identifier for the component, e.g. "MyCompany.MyApplication".
- **ProgID** The same as VersionIndependentProgID but with a version number, e.g. "MyCompany.MyApplication.2".
- **TypeLib** The file name of the type library describing the component. This file name should contain a path, if the type library isn't in the same folder of the executable. Samples: `mytypelib.tlb`, `c:\app\test.tlb`, `test.exe\1` (this last one can be used when the type library is bound to the executable as a resource).
- **CoClass** The name of the component class. There must be a coclass entry in the type library with the same name or the registration will fail.

```lua
-- @usage luacom.UnRegisterObject(registration_info)
-- @param (table) registration_info - Registration information
-- @return (boolean) status

-- Lua registration code
function UnRegisterComponent()
  local reginfo = {}
  reginfo.VersionIndependentProgID = "TESTE.Teste"

  -- Adds version information
  reginfo.ProgID = reginfo.VersionIndependentProgID..".1"
  reginfo.TypeLib = "teste.tlb"
  reginfo.CoClass = "Teste"

  local res = luacom.UnRegisterObject(reginfo)
  return res
end

```

<br>


### :large_orange_diamond: Method `luacom.addConnection()`


This method connects two LuaCOM instances, setting the server as an event sink for the client, that is, the client will call methods of the server to notify events (following the COM model). This will only work if the client supports connection points of the server's type. If the method succeeds, it returns the cookie that identifies the connection; otherwise, it throws an error.

```lua
-- @usage luacom.addConnection(client, server)
-- @param (table) client - LuaCOM instance
-- @param (table) server - LuaCOM instance
-- @return (number) cookie

local obj = luacom.CreateObject("TEST.Test")

local event_sink = {}
function event_sink:KeyPress(keynumber)
  print(keynumber)
end

local event_obj = luacom.ImplInterface(event_sink, "TEST.Test", "ITestEvents")
local cookie = luacom.addConnection(obj, event_obj)
```

<br>


### :large_orange_diamond: Method `luacom.releaseConnection()`

This method disconnects a LuaCOM instance from an event sink.

```lua
-- @usage luacom.releaseConnection(client, event_sink, cookie)
-- @param (table) client
-- @param (table) event_sink
-- @param (table) cookie
-- @return (nil)

local obj = luacom.CreateObject("TEST.Test")

local event_sink = {}
function event_sink:KeyPress(keynumber)
  print(keynumber)
end

local event_obj = luacom.ImplInterface(event_sink, "TEST.Test", "ITestEvents")

local result = luacom.addConnection(obj, event_obj)
luacom.releaseConnection(obj)
```

<br>


### :large_orange_diamond: Method `luacom.ProgIDfromCLSID()`

This method is a proxy for the Win32 function `ProgIDFromCLSID`.

```lua
-- @usage luacom.ProgIDfromCLSID(clsid)
-- @param (string) clsid
-- @return (string|nil) progID

local progid = luacom.ProgIDfromCLSID("{8E27C92B-1264-101C-8A2F-040224009C02}")
print(progid)
```

<br>


### :large_orange_diamond: Method `luacom.CLSIDfromProgID()`

It's the inverse of `ProgIDfromCLSID`.

```lua
-- @usage luacom.CLSIDfromProgID(progID)
-- @param (string) progID
-- @return (string|nil) clsID

local clsid = luacom.CLSIDfromProgID("Word.Application")
print(clsid)
```

<br>


### :large_orange_diamond: Method `luacom.ShowHelp()`

This method tries to locate the `luacom_obj`'s help file in its type information and shows it.

```lua
-- @usage luacom.ShowHelp(luacom_obj)
-- @param (table) luacom_obj - LuaCOM instance
-- @return (nil)

local obj = luacom.CreateObject("Word.Application")
luacom.ShowHelp(obj)
```

<br>


### :large_orange_diamond: Method `luacom.GetIUnknown()`

This method returns a userdata holding the `IUnknown` interface pointer to the COM object behind `luacom_obj`. It's important to notice that Lua does not duplicates userdata: many calls to `GetIUnknown` for the same LuaCOM instance will return the same userdata. This means that the reference count for the `IUnknown` interface will be incremented only once (that is, the first time the userdata is pushed) and will be decremented only when all the references to that userdata go out of scope (that is, when the userdata suffers garbage collection).

One possible use for this method is to check whether two LuaCOM instances reference the same COM object.

```lua
-- @usage luacom.GetIUnknown(luacom_obj)
-- @param (table) luacom_obj - LuaCOM instance
-- @return (userdata|nil) IUnknown metatable

-- Creates two LuaCOM instances for the same COM object
-- a running instance of Microsoft Word
local word1 = luacom.GetObject("Word.Application")
local word2 = luacom.GetObject("Word.Application")

-- These two userdata should be the same
local unk1 = luacom.GetIUnknown(word1)
local unk2 = luacom.GetIUnknown(word2)

assert(unk1 == unk2)
```

<br>


### :large_orange_diamond: Method `luacom.isMember()`

This method returns `true` (that is, different from nil) if there exists a method or a property of the `luacom_obj` named `member_name`.

```lua
-- @usage luacom.isMember(luacom_obj, name)
-- @param (table) luacom_obj - LuaCOM instance
-- @param (string) name - Member name
-- @return (boolean)

local obj = luacom.CreateObject("MyObject.Test")
if luacom.isMember(obj, "Test") then
  obj:Test()
end
```

<br>


### :large_orange_diamond: Method `luacom.StartLog()`

This methods activates the log facility of LuaCOM, writing to the log file all errors that occurr. If the library was compiled with `VERBOSE` defined, it also logs other informative messages like creation and destruction of LuaCOM internal objects, method calls etc. This can help track down object leaks. The method returns `true` if the log file could be opened, `false` otherwise.

```lua
-- @usage luacom.StartLog(log_file_name)
-- @param (string) log_file_name
-- @return (boolean) status

local ok = luacom.StartLog("luacomlog.txt")
if not ok then
  print("log not opened")
end
```

<br>


### :large_orange_diamond: Method `luacom.EndLog()`

This method stops the log facility (if it has been activated), closing the log file.

```lua
-- @usage luacom.EndLog()
-- @return (nil)

luacom.EndLog()
```

<br>


### :large_orange_diamond: Method `luacom.GetEnumerator()`

This method returns a COM enumerator for a given LuaCOM instance, if it provides one.<br>
This is the same as calling the `NewEnum` method, at least for the majority of the objects.<br>

This object is a proxy for a COM object that implements the [**`IEnumVARIANT`**](https://learn.microsoft.com/pt-br/windows/win32/api/oaidl/nn-oaidl-ienumvariant) interface. It translates the calls made to fields of the table to method calls using that interface. Enumerators arise often when dealing with collections.

> [!NOTE]
> #### Enumerator Object Methods
>
> - `:Next()` — Returns the next object in the enumeration or `nil` if the end has been reached.
> - `:Skip()` — Skips the next object, returning `true` if succeeded of `false` if not.
> - `:Reset()` — Restarts the enumerator.
> - `:Clone()` — Returns a new enumerator in the same state.

```lua
-- @usage luacom.GetEnumerator(luacom)
-- @param (table) luacom - LuaCOM instance
-- @return (table|nil) Enumerator object or nil

-- Prints all sheets of an open Excel Application
local excel = luacom.GetObject("Excel.Application")
local e = luacom.GetEnumerator(excel.Sheets)

local s = e:Next()
while s do
  print(s.Name)
  s = e:Next()
end
```

<br>

---

<br>
<br>


## :scroll: License

<a href="../images/logo-gpl-v2.png">
  <img src="../images/logo-gpl-v2.png" alt="LOGO-GPL-V2" width="150" height="150" align="right">
</a>

The **RainJIT** Plugin is licensed under the [**GPL v2.0 license**](../../LICENSE).<br>
This project also relies on external libraries that may use different open-source licenses.<br>
If you are contributing documentation or changes to the source code, please ensure that your contributions comply with the project's licensing guidelines.

<br>

---

<br>
<br>
