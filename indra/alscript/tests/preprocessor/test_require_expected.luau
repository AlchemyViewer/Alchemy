--!strict
-- Modules gathered at the top, each run once, at its first require.

do
    local modules = {
        [1] = function()
--!strict
local deep = require(2)
local M = {}
function M.greet(name: string): string
    return "hello " .. name .. deep.suffix
end
return M
        end,
        [2] = function()
return { suffix = "!" }
        end,
    }
    local loaded = {}
    function require(module)
        local held = loaded[module]
        if held == nil then
            held = dangerouslyexecuterequiredmodule(modules[module])
            loaded[module] = held
        end
        return held
    end
end
local util = require(1)
local deep = require(2)
local s = [[require("util") CHANNEL]]
ll.Say(0, util.greet("world") .. deep.suffix .. #s)
