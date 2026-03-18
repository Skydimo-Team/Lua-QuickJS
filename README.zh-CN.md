# lua-quickjs（中文说明）

> English version: [README.md](README.md)

Lua 5.4 C 模块，将 [QuickJS](https://bellard.org/quickjs/) JavaScript 引擎嵌入 Lua。

编译产物为 `quickjs.dll`（Windows）或 `quickjs.so`（Linux）。

---

## 目录

- [概述](#概述)
- [系统需求](#系统需求)
- [快速上手](#快速上手)
- [API 参考](#api-参考)
- [项目结构](#项目结构)
- [从源码构建](#从源码构建)
- [许可证](#许可证)

---

## 概述

本模块允许在 Lua 运行时中执行 JavaScript 代码，适用于在 Lua 项目中嵌入 JS 逻辑、运行打包脚本或桥接第三方 JS 库。

**特性：**
- 通过 QuickJS（2025-09-13）支持完整的 ES2020+ JavaScript
- Lua ↔ JS 双向值转换（nil、bool、整数、浮点数、字符串、table/数组 ↔ object/array）
- `console.log / warn / error / info` 输出到 stdout
- 通过 pending job 执行支持 async / Promise
- 可为每个 context 单独设置内存和调用栈限制
- 通过 Lua `__gc` 元方法自动释放资源

---

## 系统需求

| 依赖 | 版本 | 说明 |
|------|------|----|
| Lua 5.4 | 5.4.x | 需要头文件和导入库 |
| CMake | 3.20+ | 构建系统 |
| GCC (MinGW-w64) | 10+ | 需要 C17 标准支持 |
| Ninja | 1.10+ | 推荐的构建后端 |

---

## 快速上手

### 1. 获取 DLL

从源码编译（参见 [BUILD.md](BUILD.md)）或将预编译的 `quickjs.dll` 放到 `lua.exe` 同目录或项目根目录。

### 2. 在 Lua 中使用

```lua
local qjs = require("quickjs")

-- 查看版本
print(qjs.version)  -- "QuickJS 2025-09-13"

-- 创建 JS 执行上下文
local ctx = qjs.new()

-- 执行 JavaScript，将结果作为 Lua 值返回
local result = ctx:eval("1 + 2")           -- 3
local str    = ctx:eval("'Hello' + ' JS'") -- "Hello JS"
local arr    = ctx:eval("[1, 2, 3]")       -- {1, 2, 3}（Lua table）
local obj    = ctx:eval("({a:1, b:true})") -- {a=1, b=true}

-- 将 Lua 值注入 JS 全局变量
ctx:set("name", "Skydimo")
ctx:eval("console.log('Hello from ' + name)")

-- 从 JS 读取全局变量
local val = ctx:get("name")  -- "Skydimo"

-- 定义并调用 JS 函数
ctx:eval("function add(a, b) { return a + b; }")
local sum = ctx:call("add", 10, 20)  -- 30

-- 用完后关闭（也可以让 GC 自动回收）
ctx:close()
```

---

## API 参考

### 模块级别

| 符号 | 类型 | 说明 |
|------|------|------|
| `qjs.version` | `string` | QuickJS 版本字符串，如 `"QuickJS 2025-09-13"` |
| `qjs.new([opts])` | `function` | 创建新的 JS context，`opts` 为可选配置表 |

#### `qjs.new(opts)` 配置项

| 键 | 类型 | 说明 |
|----|------|------|
| `memory_limit` | `integer` | 运行时最大堆内存（字节，0 = 不限制） |
| `stack_size` | `integer` | JS 调用栈最大字节数（0 = 默认值） |

---

### Context 方法

#### `ctx:eval(code [, filename [, flags]])`

执行一段 JavaScript 表达式或语句，将结果作为 Lua 值返回。JS 异常会作为 Lua 错误抛出（含堆栈信息）。

#### `ctx:eval_module(code [, filename])`

以 ES 模块模式执行代码（支持 `import`/`export`）。

#### `ctx:call(funcname, ...)`

调用一个具名 JS 全局函数，参数和返回值自动转换。

#### `ctx:set(name, value)`

从 Lua 值设置一个 JS 全局变量。

#### `ctx:get(name)`

将 JS 全局变量读取为 Lua 值。

#### `ctx:memory_usage()`

返回内存统计表，包含 `memory_used_size`、`malloc_size`、`malloc_count`、`memory_used_count`、`atom_count` 等字段。

#### `ctx:gc()`

触发一次 QuickJS 垃圾回收。

#### `ctx:close()`

立即释放 JS context 和 runtime。可多次调用，Lua GC 时也会自动调用。

---

### 类型转换对照

| Lua 类型 | JS 类型 | 备注 |
|----------|---------|------|
| `nil` | `undefined` | |
| `boolean` | `boolean` | |
| `integer` | `int32` 或 `float64` | 超出 ±2³¹ 则升级为 float64 |
| `number`（浮点） | `float64` | |
| `string` | `string` | 二进制安全 |
| `table`（数组） | `Array` | 顺序整数键 1..n |
| `table`（哈希） | `Object` | 字符串和数字键 |
| JS `null` | `nil` | |
| JS `Array` | `table`（数组） | 0-based → 1-based |
| JS `Object` | `table`（哈希） | |
| JS `BigInt` | `integer` | 截断到 int64 |

嵌套 table/object 最大递归深度：64。

---

## 项目结构

```
lua-quickjs/
├── CMakeLists.txt          # 构建脚本
├── lua_quickjs.c           # Lua ↔ QuickJS 绑定层（主要源码）
├── test_quickjs.lua        # 功能测试脚本
├── BUILD.md                # 详细构建说明
├── README.md               # 英文说明
├── README.zh-CN.md         # 中文说明（本文件）
├── LICENSE                 # MIT 许可证
└── QuickJS/                # QuickJS 引擎源码（git 子模块）
    ├── quickjs.c / .h
    ├── cutils.c / .h
    ├── dtoa.c / .h
    ├── libregexp.c / .h
    └── libunicode.c / .h
```

---

## 从源码构建

完整的工具链配置、CMake 参数和常见问题解答请参见 **[BUILD.md](BUILD.md)**。

简要步骤：

```powershell
cd Lua-QuickJS
mkdir build; cd build
cmake .. -G Ninja `
    -DCMAKE_C_COMPILER="<路径>/gcc.exe" `
    -DLUA_INCLUDE_DIR="<路径>/include" `
    -DLUA_LIBRARY="<路径>/liblua.dll.a" `
    -DCMAKE_BUILD_TYPE=Release
ninja
copy quickjs.dll ..
```

---

## 许可证

本项目基于 **MIT 许可证** 发布。

**QuickJS 引擎** — Copyright © 2017–2021 Fabrice Bellard、Charlie Gordon  
**lua-quickjs 绑定层** — Copyright © 2026 Skydimo Team

完整协议文本见 [LICENSE](LICENSE)。
