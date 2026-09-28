--!strict
-- Modules gathered at the top, each once, dependencies first.
--#define CHANNEL 0
local util = require("util")
local deep = require("deep")
local s = [[require("util") CHANNEL]]
ll.Say(CHANNEL, util.greet("world") .. deep.suffix .. #s)
