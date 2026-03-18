/*
 * lua_quickjs.c - QuickJS JavaScript Engine binding for Lua 5.4
 *
 * Provides a Lua module "quickjs" that allows evaluating JavaScript code
 * and converting values between Lua and JavaScript.
 *
 * Usage:
 *   local qjs = require("quickjs")
 *   local ctx = qjs.new()
 *   local result = ctx:eval("1 + 2")           -- 3
 *   ctx:set("x", 42)
 *   ctx:eval("function add(a,b) { return a+b; }")
 *   local sum = ctx:call("add", 3, 4)          -- 7
 *   local obj = ctx:eval("({a:1, b:[2,3]})")   -- {a=1, b={2,3}}
 *   ctx:close()
 *
 * MIT License - Copyright (c) 2026 Skydimo Team
 */

#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "quickjs.h"

/* Maximum recursion depth for value conversion */
#define MAX_CONVERT_DEPTH 64

/* Metatable names */
#define LUA_QJS_CONTEXT "QuickJS.Context"

/* ============================================================
 * Context userdata
 * ============================================================ */

typedef struct {
    JSRuntime *rt;
    JSContext *ctx;
    int closed;
} LuaQJSContext;

static LuaQJSContext *check_context(lua_State *L, int idx)
{
    LuaQJSContext *qctx = (LuaQJSContext *)luaL_checkudata(L, idx, LUA_QJS_CONTEXT);
    if (qctx->closed)
        luaL_error(L, "QuickJS context is already closed");
    return qctx;
}

/* ============================================================
 * JS Value -> Lua Value conversion
 * ============================================================ */

static int js_to_lua(lua_State *L, JSContext *ctx, JSValue val, int depth);

static int js_array_to_lua(lua_State *L, JSContext *ctx, JSValue val, int depth)
{
    JSValue len_val;
    int64_t len, i;

    len_val = JS_GetPropertyStr(ctx, val, "length");
    if (JS_ToInt64(ctx, &len, len_val)) {
        JS_FreeValue(ctx, len_val);
        return 0;
    }
    JS_FreeValue(ctx, len_val);

    lua_createtable(L, (int)len, 0);
    for (i = 0; i < len; i++) {
        JSValue elem = JS_GetPropertyUint32(ctx, val, (uint32_t)i);
        js_to_lua(L, ctx, elem, depth + 1);
        JS_FreeValue(ctx, elem);
        lua_rawseti(L, -2, (lua_Integer)(i + 1)); /* Lua 1-based index */
    }
    return 1;
}

static int js_object_to_lua(lua_State *L, JSContext *ctx, JSValue val, int depth)
{
    JSPropertyEnum *tab;
    uint32_t len, i;

    if (JS_GetOwnPropertyNames(ctx, &tab, &len, val,
            JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY)) {
        lua_pushnil(L);
        return 1;
    }

    lua_createtable(L, 0, (int)len);
    for (i = 0; i < len; i++) {
        const char *key = JS_AtomToCString(ctx, tab[i].atom);
        if (key) {
            JSValue prop = JS_GetProperty(ctx, val, tab[i].atom);
            lua_pushstring(L, key);
            js_to_lua(L, ctx, prop, depth + 1);
            JS_FreeValue(ctx, prop);
            lua_rawset(L, -3);
            JS_FreeCString(ctx, key);
        }
    }

    for (i = 0; i < len; i++)
        JS_FreeAtom(ctx, tab[i].atom);
    js_free(ctx, tab);
    return 1;
}

static int js_to_lua(lua_State *L, JSContext *ctx, JSValue val, int depth)
{
    if (depth > MAX_CONVERT_DEPTH) {
        lua_pushnil(L);
        return 1;
    }

    int tag = JS_VALUE_GET_TAG(val);

    if (JS_IsUndefined(val) || JS_IsNull(val) || JS_IsUninitialized(val)) {
        lua_pushnil(L);
    } else if (JS_IsBool(val)) {
        lua_pushboolean(L, JS_ToBool(ctx, val));
    } else if (tag == JS_TAG_INT) {
        int32_t v;
        JS_ToInt32(ctx, &v, val);
        lua_pushinteger(L, (lua_Integer)v);
    } else if (JS_TAG_IS_FLOAT64(tag)) {
        double v;
        JS_ToFloat64(ctx, &v, val);
        lua_pushnumber(L, (lua_Number)v);
    } else if (JS_IsString(val)) {
        size_t len;
        const char *str = JS_ToCStringLen(ctx, &len, val);
        if (str) {
            lua_pushlstring(L, str, len);
            JS_FreeCString(ctx, str);
        } else {
            lua_pushnil(L);
        }
    } else if (JS_IsArray(ctx, val)) {
        js_array_to_lua(L, ctx, val, depth);
    } else if (JS_IsObject(val)) {
        js_object_to_lua(L, ctx, val, depth);
    } else if (JS_IsBigInt(ctx, val)) {
        int64_t v;
        if (!JS_ToBigInt64(ctx, &v, val)) {
            lua_pushinteger(L, (lua_Integer)v);
        } else {
            lua_pushnil(L);
        }
    } else {
        /* Exception or other unhandled type */
        lua_pushnil(L);
    }
    return 1;
}

/* ============================================================
 * Lua Value -> JS Value conversion
 * ============================================================ */

static JSValue lua_to_js(lua_State *L, JSContext *ctx, int idx, int depth);

/* Check if a Lua table is array-like (sequential integer keys from 1) */
static int lua_table_is_array(lua_State *L, int idx)
{
    lua_Integer len = luaL_len(L, idx);
    if (len <= 0)
        return 0;

    /* Quick check: if rawlen > 0, treat as array */
    /* Verify the table has mainly integer keys */
    int count = 0;
    lua_pushnil(L);
    while (lua_next(L, idx) != 0) {
        count++;
        lua_pop(L, 1); /* pop value, keep key */
        if (count > len) {
            /* More keys than the array length -> has string keys too */
            lua_pop(L, 1); /* pop key */
            return 0;
        }
    }
    return (count == (int)len);
}

static JSValue lua_to_js(lua_State *L, JSContext *ctx, int idx, int depth)
{
    if (depth > MAX_CONVERT_DEPTH)
        return JS_UNDEFINED;

    idx = lua_absindex(L, idx);

    switch (lua_type(L, idx)) {
    case LUA_TNIL:
        return JS_UNDEFINED;

    case LUA_TBOOLEAN:
        return JS_NewBool(ctx, lua_toboolean(L, idx));

    case LUA_TNUMBER:
        if (lua_isinteger(L, idx)) {
            lua_Integer v = lua_tointeger(L, idx);
            if (v >= INT32_MIN && v <= INT32_MAX)
                return JS_NewInt32(ctx, (int32_t)v);
            else
                return JS_NewFloat64(ctx, (double)v);
        } else {
            return JS_NewFloat64(ctx, lua_tonumber(L, idx));
        }

    case LUA_TSTRING: {
        size_t len;
        const char *s = lua_tolstring(L, idx, &len);
        return JS_NewStringLen(ctx, s, len);
    }

    case LUA_TTABLE: {
        if (lua_table_is_array(L, idx)) {
            /* Convert to JS Array */
            lua_Integer len = luaL_len(L, idx);
            JSValue arr = JS_NewArray(ctx);
            if (JS_IsException(arr))
                return arr;
            for (lua_Integer i = 1; i <= len; i++) {
                lua_rawgeti(L, idx, i);
                JSValue elem = lua_to_js(L, ctx, -1, depth + 1);
                lua_pop(L, 1);
                JS_SetPropertyUint32(ctx, arr, (uint32_t)(i - 1), elem);
            }
            return arr;
        } else {
            /* Convert to JS Object */
            JSValue obj = JS_NewObject(ctx);
            if (JS_IsException(obj))
                return obj;
            lua_pushnil(L);
            while (lua_next(L, idx) != 0) {
                const char *key = NULL;
                if (lua_type(L, -2) == LUA_TSTRING) {
                    key = lua_tostring(L, -2);
                } else if (lua_type(L, -2) == LUA_TNUMBER) {
                    /* Convert numeric key to string */
                    lua_pushvalue(L, -2);
                    key = lua_tostring(L, -1);
                    lua_pop(L, 1);
                }
                if (key) {
                    JSValue val = lua_to_js(L, ctx, -1, depth + 1);
                    JS_SetPropertyStr(ctx, obj, key, val);
                }
                lua_pop(L, 1); /* pop value, keep key */
            }
            return obj;
        }
    }

    default:
        return JS_UNDEFINED;
    }
}

/* ============================================================
 * console.log implementation (prints to stdout)
 * ============================================================ */

static JSValue js_console_log(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    int i;
    for (i = 0; i < argc; i++) {
        const char *str = JS_ToCString(ctx, argv[i]);
        if (str) {
            if (i > 0)
                fputc(' ', stdout);
            fputs(str, stdout);
            JS_FreeCString(ctx, str);
        }
    }
    fputc('\n', stdout);
    fflush(stdout);
    return JS_UNDEFINED;
}

static void js_add_console(JSContext *ctx)
{
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue console = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, console, "log",
        JS_NewCFunction(ctx, js_console_log, "log", 1));
    JS_SetPropertyStr(ctx, console, "warn",
        JS_NewCFunction(ctx, js_console_log, "warn", 1));
    JS_SetPropertyStr(ctx, console, "error",
        JS_NewCFunction(ctx, js_console_log, "error", 1));
    JS_SetPropertyStr(ctx, console, "info",
        JS_NewCFunction(ctx, js_console_log, "info", 1));
    JS_SetPropertyStr(ctx, global, "console", console);
    JS_FreeValue(ctx, global);
}

/* ============================================================
 * Push JS exception as Lua error string
 * ============================================================ */

static int push_js_error(lua_State *L, JSContext *ctx)
{
    JSValue exc = JS_GetException(ctx);
    const char *str = JS_ToCString(ctx, exc);
    if (str) {
        /* Try to get stack trace */
        JSValue stack = JS_GetPropertyStr(ctx, exc, "stack");
        if (!JS_IsUndefined(stack)) {
            const char *stack_str = JS_ToCString(ctx, stack);
            if (stack_str) {
                lua_pushfstring(L, "%s\n%s", str, stack_str);
                JS_FreeCString(ctx, stack_str);
            } else {
                lua_pushstring(L, str);
            }
            JS_FreeValue(ctx, stack);
        } else {
            lua_pushstring(L, str);
        }
        JS_FreeCString(ctx, str);
    } else {
        lua_pushliteral(L, "unknown JavaScript error");
    }
    JS_FreeValue(ctx, exc);
    return 1;
}

/* ============================================================
 * Module methods
 * ============================================================ */

/* qjs.new([options_table]) -> context userdata */
static int lqjs_new(lua_State *L)
{
    LuaQJSContext *qctx;
    size_t mem_limit = 0;
    size_t stack_size = 0;

    /* Optional options table */
    if (lua_istable(L, 1)) {
        lua_getfield(L, 1, "memory_limit");
        if (lua_isinteger(L, -1))
            mem_limit = (size_t)lua_tointeger(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "stack_size");
        if (lua_isinteger(L, -1))
            stack_size = (size_t)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }

    qctx = (LuaQJSContext *)lua_newuserdatauv(L, sizeof(LuaQJSContext), 0);
    qctx->rt = NULL;
    qctx->ctx = NULL;
    qctx->closed = 0;
    luaL_setmetatable(L, LUA_QJS_CONTEXT);

    qctx->rt = JS_NewRuntime();
    if (!qctx->rt)
        return luaL_error(L, "failed to create QuickJS runtime");

    if (mem_limit > 0)
        JS_SetMemoryLimit(qctx->rt, mem_limit);
    if (stack_size > 0)
        JS_SetMaxStackSize(qctx->rt, stack_size);

    qctx->ctx = JS_NewContext(qctx->rt);
    if (!qctx->ctx) {
        JS_FreeRuntime(qctx->rt);
        qctx->rt = NULL;
        return luaL_error(L, "failed to create QuickJS context");
    }

    /* Add console.log support */
    js_add_console(qctx->ctx);

    return 1;
}

/* ctx:eval(code [, filename [, flags]]) -> value */
static int lqjs_eval(lua_State *L)
{
    LuaQJSContext *qctx = check_context(L, 1);
    size_t code_len;
    const char *code = luaL_checklstring(L, 2, &code_len);
    const char *filename = luaL_optstring(L, 3, "<eval>");
    int flags = (int)luaL_optinteger(L, 4, JS_EVAL_TYPE_GLOBAL);

    JSValue result = JS_Eval(qctx->ctx, code, code_len, filename, flags);

    if (JS_IsException(result)) {
        push_js_error(L, qctx->ctx);
        JS_FreeValue(qctx->ctx, result);
        return lua_error(L);
    }

    /* Execute pending jobs (for promises, async) */
    JSContext *pctx;
    while (JS_IsJobPending(qctx->rt)) {
        int ret = JS_ExecutePendingJob(qctx->rt, &pctx);
        if (ret < 0)
            break;
    }

    js_to_lua(L, qctx->ctx, result, 0);
    JS_FreeValue(qctx->ctx, result);
    return 1;
}

/* ctx:eval_module(code [, filename]) -> value */
static int lqjs_eval_module(lua_State *L)
{
    LuaQJSContext *qctx = check_context(L, 1);
    size_t code_len;
    const char *code = luaL_checklstring(L, 2, &code_len);
    const char *filename = luaL_optstring(L, 3, "<module>");

    JSValue result = JS_Eval(qctx->ctx, code, code_len, filename,
                             JS_EVAL_TYPE_MODULE);

    if (JS_IsException(result)) {
        push_js_error(L, qctx->ctx);
        JS_FreeValue(qctx->ctx, result);
        return lua_error(L);
    }

    /* Execute pending jobs */
    JSContext *pctx;
    while (JS_IsJobPending(qctx->rt)) {
        int ret = JS_ExecutePendingJob(qctx->rt, &pctx);
        if (ret < 0)
            break;
    }

    js_to_lua(L, qctx->ctx, result, 0);
    JS_FreeValue(qctx->ctx, result);
    return 1;
}

/* ctx:call(funcname, ...) -> value */
static int lqjs_call(lua_State *L)
{
    LuaQJSContext *qctx = check_context(L, 1);
    const char *name = luaL_checkstring(L, 2);
    int nargs = lua_gettop(L) - 2;

    JSValue global = JS_GetGlobalObject(qctx->ctx);
    JSValue func = JS_GetPropertyStr(qctx->ctx, global, name);

    if (!JS_IsFunction(qctx->ctx, func)) {
        JS_FreeValue(qctx->ctx, func);
        JS_FreeValue(qctx->ctx, global);
        return luaL_error(L, "'%s' is not a JavaScript function", name);
    }

    /* Convert Lua arguments to JS */
    JSValue *argv = NULL;
    if (nargs > 0) {
        argv = (JSValue *)js_malloc(qctx->ctx, sizeof(JSValue) * nargs);
        if (!argv) {
            JS_FreeValue(qctx->ctx, func);
            JS_FreeValue(qctx->ctx, global);
            return luaL_error(L, "out of memory");
        }
        for (int i = 0; i < nargs; i++) {
            argv[i] = lua_to_js(L, qctx->ctx, i + 3, 0);
        }
    }

    JSValue result = JS_Call(qctx->ctx, func, global, nargs, argv);

    /* Free arguments */
    for (int i = 0; i < nargs; i++)
        JS_FreeValue(qctx->ctx, argv[i]);
    if (argv)
        js_free(qctx->ctx, argv);
    JS_FreeValue(qctx->ctx, func);
    JS_FreeValue(qctx->ctx, global);

    if (JS_IsException(result)) {
        push_js_error(L, qctx->ctx);
        JS_FreeValue(qctx->ctx, result);
        return lua_error(L);
    }

    /* Execute pending jobs */
    JSContext *pctx;
    while (JS_IsJobPending(qctx->rt)) {
        int ret = JS_ExecutePendingJob(qctx->rt, &pctx);
        if (ret < 0)
            break;
    }

    js_to_lua(L, qctx->ctx, result, 0);
    JS_FreeValue(qctx->ctx, result);
    return 1;
}

/* ctx:set(name, value) -> nil */
static int lqjs_set(lua_State *L)
{
    LuaQJSContext *qctx = check_context(L, 1);
    const char *name = luaL_checkstring(L, 2);
    luaL_checkany(L, 3);

    JSValue global = JS_GetGlobalObject(qctx->ctx);
    JSValue val = lua_to_js(L, qctx->ctx, 3, 0);
    JS_SetPropertyStr(qctx->ctx, global, name, val);
    JS_FreeValue(qctx->ctx, global);
    return 0;
}

/* ctx:get(name) -> value */
static int lqjs_get(lua_State *L)
{
    LuaQJSContext *qctx = check_context(L, 1);
    const char *name = luaL_checkstring(L, 2);

    JSValue global = JS_GetGlobalObject(qctx->ctx);
    JSValue val = JS_GetPropertyStr(qctx->ctx, global, name);
    JS_FreeValue(qctx->ctx, global);

    js_to_lua(L, qctx->ctx, val, 0);
    JS_FreeValue(qctx->ctx, val);
    return 1;
}

/* ctx:gc() */
static int lqjs_gc(lua_State *L)
{
    LuaQJSContext *qctx = check_context(L, 1);
    JS_RunGC(qctx->rt);
    return 0;
}

/* ctx:close() */
static int lqjs_close(lua_State *L)
{
    LuaQJSContext *qctx = (LuaQJSContext *)luaL_checkudata(L, 1, LUA_QJS_CONTEXT);
    if (!qctx->closed) {
        qctx->closed = 1;
        if (qctx->ctx) {
            JS_FreeContext(qctx->ctx);
            qctx->ctx = NULL;
        }
        if (qctx->rt) {
            JS_FreeRuntime(qctx->rt);
            qctx->rt = NULL;
        }
    }
    return 0;
}

/* __gc metamethod */
static int lqjs_gc_meta(lua_State *L)
{
    return lqjs_close(L);
}

/* __tostring metamethod */
static int lqjs_tostring(lua_State *L)
{
    LuaQJSContext *qctx = (LuaQJSContext *)luaL_checkudata(L, 1, LUA_QJS_CONTEXT);
    if (qctx->closed) {
        lua_pushliteral(L, "QuickJS.Context (closed)");
    } else {
        lua_pushfstring(L, "QuickJS.Context (%p)", qctx->ctx);
    }
    return 1;
}

/* ctx:memory_usage() -> table */
static int lqjs_memory_usage(lua_State *L)
{
    LuaQJSContext *qctx = check_context(L, 1);
    JSMemoryUsage stats;
    JS_ComputeMemoryUsage(qctx->rt, &stats);

    lua_createtable(L, 0, 8);

    lua_pushinteger(L, (lua_Integer)stats.malloc_size);
    lua_setfield(L, -2, "malloc_size");

    lua_pushinteger(L, (lua_Integer)stats.malloc_count);
    lua_setfield(L, -2, "malloc_count");

    lua_pushinteger(L, (lua_Integer)stats.memory_used_size);
    lua_setfield(L, -2, "memory_used_size");

    lua_pushinteger(L, (lua_Integer)stats.memory_used_count);
    lua_setfield(L, -2, "memory_used_count");

    lua_pushinteger(L, (lua_Integer)stats.atom_count);
    lua_setfield(L, -2, "atom_count");

    lua_pushinteger(L, (lua_Integer)stats.str_count);
    lua_setfield(L, -2, "str_count");

    lua_pushinteger(L, (lua_Integer)stats.obj_count);
    lua_setfield(L, -2, "obj_count");

    lua_pushinteger(L, (lua_Integer)stats.js_func_count);
    lua_setfield(L, -2, "js_func_count");

    return 1;
}

/* ============================================================
 * Module registration
 * ============================================================ */

static const luaL_Reg context_methods[] = {
    {"eval",          lqjs_eval},
    {"eval_module",   lqjs_eval_module},
    {"call",          lqjs_call},
    {"set",           lqjs_set},
    {"get",           lqjs_get},
    {"gc",            lqjs_gc},
    {"close",         lqjs_close},
    {"memory_usage",  lqjs_memory_usage},
    {NULL, NULL}
};

static const luaL_Reg context_meta[] = {
    {"__gc",       lqjs_gc_meta},
    {"__tostring", lqjs_tostring},
    {"__close",    lqjs_close},
    {NULL, NULL}
};

static const luaL_Reg module_funcs[] = {
    {"new", lqjs_new},
    {NULL, NULL}
};

#if defined(_WIN32)
__declspec(dllexport)
#endif
int luaopen_quickjs(lua_State *L)
{
    /* Create context metatable */
    luaL_newmetatable(L, LUA_QJS_CONTEXT);
    luaL_setfuncs(L, context_meta, 0);

    /* Set __index to methods table */
    luaL_newlib(L, context_methods);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    /* Create module table */
    luaL_newlib(L, module_funcs);

    /* Add version info */
    lua_pushliteral(L, "QuickJS 2025-09-13");
    lua_setfield(L, -2, "version");

    return 1;
}
