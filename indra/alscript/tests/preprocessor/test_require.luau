--!strict
-- Modules gathered at the top, each run once, at its first require.
--#define CHANNEL 0
local util = require("util")
local deep = require("deep")
local s = [[require("util") CHANNEL]]
ll.Say(CHANNEL, util.greet("world") .. deep.suffix .. #s)
