--!strict
-- Modules gathered at the top, each once, dependencies first.

local __modules = {}
__modules["deep.luau"] = (function()
return { suffix = "!" }
end)()
__modules["util.luau"] = (function()
--!strict
local deep = __modules["deep.luau"]
local M = {}
function M.greet(name: string): string
    return "hello " .. name .. deep.suffix
end
return M
end)()
local util = __modules["util.luau"]
local deep = __modules["deep.luau"]
local s = [[require("util") CHANNEL]]
ll.Say(0, util.greet("world") .. deep.suffix .. #s)
