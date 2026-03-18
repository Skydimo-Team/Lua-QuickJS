# 构建指南

> 项目概述、API 参考和用法示例请见 [README.md](README.md)。

---

## 目录

- [依赖项](#依赖项)
- [编译步骤](#编译步骤)
- [运行测试](#运行测试)
- [CMake 配置参数说明](#cmake-配置参数说明)
- [常见问题](#常见问题)

---

## 依赖项

| 依赖 | 最低版本 | 说明 |
|------|---------|------|
| **CMake** | 3.20 | 构建系统 |
| **Ninja** | 1.10+ | 推荐的构建后端（也可用 Make） |
| **MinGW-w64 (GCC)** | GCC 10+ | 需要 C17 标准支持 |
| **Lua 5.4** | 5.4.x | 需要头文件（`lua.h` 等）和导入库（`.dll.a`） |

### MinGW-w64 安装

从以下任一渠道获取 MinGW-w64（需要 x86_64 版本）：

- **推荐：** [MinGW-w64 by niXman](https://github.com/niXman/mingw-builds-binaries/releases) 或 [GCC with MCF thread model by LH_Mouse](https://gcc-mcf.lhmouse.com/)
- **Scoop：** `scoop install mingw`
- **MSYS2：** `pacman -S mingw-w64-x86_64-toolchain`

确保 `gcc.exe`、`cmake.exe`（或系统 CMake）、`ninja.exe` 可用。

### Lua 5.4 安装

如果你还没有 Lua 5.4，可以通过以下方式安装：

**Scoop（推荐）：**
```powershell
scoop install lua
```
安装后头文件和库文件通常位于：
- 头文件：`C:\Users\<用户名>\scoop\apps\lua\current\include\`
- 导入库：`C:\Users\<用户名>\scoop\apps\lua\current\lib\liblua.dll.a`

**手动编译：** 从 https://www.lua.org/download.html 下载源码自行编译。

---

## 编译步骤

QuickJS 原生支持 GCC，使用 MinGW-w64 **无需修改任何 QuickJS 源码**即可编译。

### 1. 验证工具链

```powershell
# 将 MinGW-w64 加入 PATH（根据实际安装路径修改）
$env:PATH = "C:\bin\Mingw-w64\bin;$env:PATH"

gcc --version      # 应输出 GCC 版本信息
cmake --version    # 应输出 CMake 3.20+
ninja --version    # 应输出 Ninja 版本信息
```

### 2. 配置 CMake

```powershell
cd Lua-QuickJS
mkdir build
cd build

# 配置（请根据实际路径修改）
cmake .. -G Ninja `
    -DCMAKE_C_COMPILER="C:/bin/Mingw-w64/bin/gcc.exe" `
    -DCMAKE_MAKE_PROGRAM="C:/bin/Mingw-w64/bin/ninja.exe" `
    -DLUA_INCLUDE_DIR="C:/Users/<用户名>/scoop/apps/lua/current/include" `
    -DLUA_LIBRARY="C:/Users/<用户名>/scoop/apps/lua/current/lib/liblua.dll.a" `
    -DCMAKE_BUILD_TYPE=Release
```

> **注意：** CMake 路径使用正斜杠 `/` 或转义反斜杠 `\\`，避免 PowerShell 解释问题。

### 3. 编译

```powershell
ninja
```

成功后将在 `build/` 目录下生成 `quickjs.dll`。

### 4. 安装 / 使用

将 `quickjs.dll` 复制到 Lua 的 `cpath` 搜索路径中（与 `lua.exe` 同目录，或项目目录下）：

```powershell
copy quickjs.dll ..
```

---

## 运行测试

将编译好的 `quickjs.dll` 放到项目根目录，然后运行测试脚本：

```powershell
cd Lua-QuickJS
lua test_quickjs.lua
```

预期输出：
```
Module loaded:  QuickJS 2025-09-13
Context:        QuickJS.Context (...)
1 + 2 = 3
String: Hello from JS!
x =     42
data.name =     Lua
add(10, 20) =   30
Array:  10      20      30
Object: a =     1       b =     test    c =     true
Hello from JS console!
Math.PI =       3.1415926535898
JSON:   {"x":1,"y":[2,3]}
Memory used:    82217   bytes

All tests passed!
```

---

## CMake 配置参数说明

| 参数 | 类型 | 说明 |
|------|------|------|
| `LUA_INCLUDE_DIR` | PATH | Lua 5.4 头文件目录（包含 `lua.h`、`lauxlib.h`、`lualib.h`） |
| `LUA_LIBRARY` | FILEPATH | Lua 5.4 导入库路径（`.dll.a`） |
| `CMAKE_C_COMPILER` | FILEPATH | GCC 编译器路径 |
| `CMAKE_MAKE_PROGRAM` | FILEPATH | 构建工具路径（Ninja） |
| `CMAKE_BUILD_TYPE` | STRING | `Release`（优化）/ `Debug`（调试信息）/ `RelWithDebInfo` |

---

## 常见问题

### Q: 链接时报 `undefined reference to pthread_mutex_lock`

**原因：** QuickJS 的 `CONFIG_ATOMICS` 特性需要 pthread 库。

**解决：** CMakeLists.txt 中已自动链接 `pthread`。如果仍有问题，确认你的 MinGW 发行版包含 `libpthread.a` 或 `libwinpthread.a`。

### Q: `lua test_quickjs.lua` 报 `module 'quickjs' not found`

**原因：** `quickjs.dll` 不在 Lua 的 C 模块搜索路径（`package.cpath`）中。

**解决：** 将 `quickjs.dll` 复制到以下位置之一：
- `lua.exe` 所在目录
- 当前工作目录（`.`）
- 在 Lua 中查看搜索路径：`print(package.cpath)`

### Q: 如何切换 Debug 构建？

```powershell
cmake .. -DCMAKE_BUILD_TYPE=Debug
ninja
```

Debug 模式包含调试符号，不开启优化，方便用 GDB 调试。
