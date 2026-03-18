-- test_quickjs.lua - Test QuickJS Lua module
local qjs = require("quickjs")
print("Module loaded:", qjs.version)

-- Create a JS context
local ctx = qjs.new()
print("Context:", ctx)

-- Basic arithmetic
local result = ctx:eval("1 + 2")
print("1 + 2 =", result)
assert(result == 3)

-- String operations
local str = ctx:eval("'Hello' + ' ' + 'from JS!'")
print("String:", str)
assert(str == "Hello from JS!")

-- Boolean
local b = ctx:eval("true")
assert(b == true)

-- Nil/undefined
local u = ctx:eval("undefined")
assert(u == nil)

-- Set global variable from Lua
ctx:set("x", 42)
local x = ctx:get("x")
print("x =", x)
assert(x == 42)

-- Set and get table/object
ctx:set("data", {name = "Lua", version = 54})
local name = ctx:eval("data.name")
print("data.name =", name)
assert(name == "Lua")

-- Define and call JS function
ctx:eval("function add(a, b) { return a + b; }")
local sum = ctx:call("add", 10, 20)
print("add(10, 20) =", sum)
assert(sum == 30)

-- Array conversion
local arr = ctx:eval("[10, 20, 30]")
print("Array:", arr[1], arr[2], arr[3])
assert(arr[1] == 10 and arr[2] == 20 and arr[3] == 30)

-- Object conversion
local obj = ctx:eval("({a: 1, b: 'test', c: true})")
print("Object: a =", obj.a, "b =", obj.b, "c =", obj.c)
assert(obj.a == 1 and obj.b == "test" and obj.c == true)

-- console.log
ctx:eval("console.log('Hello from JS console!')")

-- Math operations
local pi = ctx:eval("Math.PI")
print("Math.PI =", pi)

-- JSON
local json = ctx:eval("JSON.stringify({x: 1, y: [2,3]})")
print("JSON:", json)

-- Memory usage
local mem = ctx:memory_usage()
print("Memory used:", mem.memory_used_size, "bytes")

-- Cleanup
ctx:close()
print("\nAll tests passed!")
