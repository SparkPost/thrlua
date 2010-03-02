require 'Test.More'

-- http://www.lua.org/bugs.html#5.1.4-3

plan(1);

is(((1 or false) and true) or false, true);

