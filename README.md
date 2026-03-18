# lua-quickjs

> 中文版：[README.zh-CN.md](README.zh-CN.md)

A Lua 5.4 C module that embeds the [QuickJS](https://bellard.org/quickjs/) JavaScript engine, built for the [Skydimo](https://skydimo.com) RGB lighting control application's Lua plugin system.

Compiles to `quickjs.dll` (Windows) or `quickjs.so` (Linux).

---

## Table of Contents

- [Overview](#overview)
- [Requirements](#requirements)
- [Quick Start](#quick-start)
- [API Reference](#api-reference)
- [Project Structure](#project-structure)
- [Building from Source](#building-from-source)
- [License](#license)

---

## Overview

Skydimo's plugin system is written in **Lua 5.4**. This module lets plugins run JavaScript code inside the Lua runtime — useful for porting existing JS-based device logic, running bundled scripts, or bridging third-party JS libraries.

**Features:**
- Full ES2020+ JavaScript via QuickJS (2025-09-13)
- Bidirectional value conversion: Lua ↔ JS (nil, boolean, integer, float, string, table/array ↔ object/array)
- `console.log / warn / error / info` piped to stdout
- Async / Promise support via pending job execution
- Per-context memory and stack size limits
- Automatic GC via Lua `__gc` metamethod

---

## Requirements

| Dependency | Version | Notes |
|------------|---------|-------|
| Lua 5.4 | 5.4.x | Headers + import library required |
| CMake | 3.20+ | Build system |
| GCC (MinGW-w64) | 10+ | C17 support required |
| Ninja | 1.10+ | Recommended build backend |

---

## Quick Start

### 1. Get the DLL

Build from source (see [BUILD.md](BUILD.md)) or copy a pre-built `quickjs.dll` into the same directory as `lua.exe` or your project root.

### 2. Use in Lua

```lua
local qjs = require("quickjs")

-- Check version
print(qjs.version)  -- "QuickJS 2025-09-13"

-- Create a JS execution context
local ctx = qjs.new()

-- Evaluate JavaScript, get the result as a Lua value
local result = ctx:eval("1 + 2")           -- 3
local str    = ctx:eval("'Hello' + ' JS'") -- "Hello JS"
local arr    = ctx:eval("[1, 2, 3]")       -- {1, 2, 3}  (Lua table)
local obj    = ctx:eval("({a:1, b:true})") -- {a=1, b=true}

-- Pass Lua values into JS globals
ctx:set("name", "Skydimo")
ctx:eval("console.log('Hello from ' + name)")

-- Read a JS global back to Lua
local val = ctx:get("name")  -- "Skydimo"

-- Define and call a JS function
ctx:eval("function add(a, b) { return a + b; }")
local sum = ctx:call("add", 10, 20)  -- 30

-- Always close when done (or let GC collect it)
ctx:close()
```

---

## API Reference

### Module-level

| Symbol | Type | Description |
|--------|------|-------------|
| `qjs.version` | `string` | QuickJS version string, e.g. `"QuickJS 2025-09-13"` |
| `qjs.new([opts])` | `function` | Create a new JS context. `opts` is an optional table. |

#### `qjs.new(opts)` options

| Key | Type | Description |
|-----|------|-------------|
| `memory_limit` | `integer` | Max heap bytes for this runtime (0 = unlimited) |
| `stack_size` | `integer` | Max JS call stack bytes (0 = default) |

```lua
local ctx = qjs.new({ memory_limit = 4 * 1024 * 1024 }) -- 4 MB limit
```

---

### Context methods

#### `ctx:eval(code [, filename [, flags]])`

Evaluate a JavaScript expression or statement. Returns the result as a Lua value.

```lua
local pi = ctx:eval("Math.PI")    -- 3.1415926535898
local ok = ctx:eval("2 > 1")      -- true
```

- `filename` — shown in stack traces (default `"<eval>"`)
- `flags` — QuickJS eval flags (default `JS_EVAL_TYPE_GLOBAL`)
- Throws a Lua error on JS exceptions (includes stack trace)

#### `ctx:eval_module(code [, filename])`

Evaluate code as an ES module (`import`/`export` are valid).

#### `ctx:call(funcname, ...)`

Call a named global JavaScript function with Lua arguments. Returns the result.

```lua
ctx:eval("function greet(n) { return 'Hello, ' + n; }")
local msg = ctx:call("greet", "World")  -- "Hello, World"
```

Throws a Lua error if the function doesn't exist or throws.

#### `ctx:set(name, value)`

Set a JavaScript global variable from a Lua value.

```lua
ctx:set("config", { debug = true, port = 8080 })
```

#### `ctx:get(name)`

Get a JavaScript global variable as a Lua value.

#### `ctx:memory_usage()`

Returns a table with memory statistics:

| Key | Description |
|-----|-------------|
| `memory_used_size` | Bytes currently in use |
| `malloc_size` | Total bytes allocated |
| `malloc_count` | Number of live allocations |
| `memory_used_count` | Number of live JS values |
| `atom_count` | Number of interned strings |

#### `ctx:gc()`

Force a QuickJS garbage collection cycle.

#### `ctx:close()`

Free the JS context and runtime immediately. Safe to call multiple times. Also called automatically when the context is garbage-collected by Lua.

---

### Type conversion table

| Lua type | JS type | Notes |
|----------|---------|-------|
| `nil` | `undefined` | |
| `boolean` | `boolean` | |
| `integer` | `int32` or `float64` | Promotes to float64 if outside ±2³¹ |
| `number` (float) | `float64` | |
| `string` | `string` | Binary safe |
| `table` (array) | `Array` | Sequential integer keys 1..n |
| `table` (object) | `Object` | String and numeric keys |
| JS `null` | `nil` | |
| JS `Array` | `table` (array) | 0-based → 1-based |
| JS `Object` | `table` (hash) | |
| JS `BigInt` | `integer` | Clamped to int64 |

Max recursion depth for nested tables/objects: 64.

---

## Project Structure

```
lua-quickjs/
├── CMakeLists.txt          # Build script
├── lua_quickjs.c           # Lua ↔ QuickJS binding layer (main source)
├── test_quickjs.lua        # Functional test script
├── BUILD.md                # Detailed build instructions
├── README.md               # This file
├── README.zh-CN.md         # Chinese documentation
├── LICENSE                 # MIT License
└── QuickJS/                # QuickJS engine source (git submodule)
    ├── quickjs.c / .h
    ├── cutils.c / .h
    ├── dtoa.c / .h
    ├── libregexp.c / .h
    └── libunicode.c / .h
```

---

## Building from Source

See **[BUILD.md](BUILD.md)** for the full step-by-step guide covering toolchain setup, CMake configuration, and common issues.

Quick summary:

```powershell
cd Lua-QuickJS
mkdir build; cd build
cmake .. -G Ninja `
    -DCMAKE_C_COMPILER="<path>/gcc.exe" `
    -DLUA_INCLUDE_DIR="<path>/include" `
    -DLUA_LIBRARY="<path>/liblua.dll.a" `
    -DCMAKE_BUILD_TYPE=Release
ninja
copy quickjs.dll ..
```

---

## License

This project is distributed under the **MIT License**.

**QuickJS engine** — Copyright © 2017–2021 Fabrice Bellard, Charlie Gordon  
**lua-quickjs binding** — Copyright © 2026 Skydimo Team

See [LICENSE](LICENSE) for the full text.

