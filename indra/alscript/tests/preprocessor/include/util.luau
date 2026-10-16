--!strict
local deep = require("deep")
local M = {}
function M.greet(name: string): string
    return "hello " .. name .. deep.suffix
end
return M
