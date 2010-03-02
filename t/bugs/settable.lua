require 'Test.More'

-- http://www.lua.org/bugs.html#5.1.4-4

plan(1);

local value = 42;

grandparent = {}
grandparent.__newindex = function(s,_,_) value = s end

parent = {}
parent.__newindex = parent
setmetatable(parent, grandparent)

child = setmetatable({}, parent)
child.foo = 10      --> (crash on some machines)

like(value, "table:");

