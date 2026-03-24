cmake_minimum_required(VERSION 3.20)
project(lua_quickjs C)

# ============================================================
# Options
# ============================================================
set(LUA_INCLUDE_DIR "" CACHE PATH "Path to Lua 5.4 include directory")
set(LUA_LIBRARY     "" CACHE FILEPATH "Path to Lua 5.4 import library (.a)")

# ============================================================
# QuickJS source files (core engine only, no libc)
# ============================================================
set(QJS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/QuickJS")

set(QJS_SOURCES
    ${QJS_DIR}/quickjs.c
    ${QJS_DIR}/dtoa.c
    ${QJS_DIR}/libregexp.c
    ${QJS_DIR}/libunicode.c
    ${QJS_DIR}/cutils.c
)

# ============================================================
# Lua binding source
# ============================================================
set(BINDING_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/lua_quickjs.c
)

# ============================================================
# Build shared library (.dll / .so)
# ============================================================
add_library(quickjs SHARED ${QJS_SOURCES} ${BINDING_SOURCES})

# Output name: quickjs.dll (not libquickjs.dll)
set_target_properties(quickjs PROPERTIES
    PREFIX ""
    OUTPUT_NAME "quickjs"
)

# ============================================================
# Include directories
# ============================================================
target_include_directories(quickjs PRIVATE
    ${QJS_DIR}              # QuickJS headers
    ${LUA_INCLUDE_DIR}      # Lua headers
)

# ============================================================
# Compile definitions
# ============================================================
target_compile_definitions(quickjs PRIVATE
    CONFIG_VERSION="2025-09-13"
    _GNU_SOURCE               # Needed by QuickJS
)

# Require C17 standard
set_target_properties(quickjs PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
)

# ============================================================
# Compiler flags
# ============================================================
target_compile_options(quickjs PRIVATE
    -Wall
    -Wno-sign-compare
    -Wno-unused-parameter
    -Wno-implicit-fallthrough
)

# ============================================================
# Link libraries
# ============================================================
if(WIN32)
    target_link_libraries(quickjs PRIVATE ws2_32)
    # Statically embed MinGW runtime so the DLL has no external MinGW
    # dependencies on end-user machines.
    # -static-libgcc embeds libgcc; the static mcfgthread and winpthread
    # archives are linked via target_link_libraries so they appear after
    # object files in the linker command (required for symbol resolution).
    # lua54.dll intentionally stays dynamic: a Lua module must share the
    # host Lua runtime's C API tables or type checks will break.
    target_link_options(quickjs PRIVATE
        -static-libgcc
    )
    target_link_libraries(quickjs PRIVATE
        -Wl,-Bstatic -lmcfgthread -lwinpthread -Wl,-Bdynamic
    )
endif()

if(LUA_LIBRARY)
    target_link_libraries(quickjs PRIVATE ${LUA_LIBRARY})
endif()
