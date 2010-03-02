require 'Test.More'

-- http://www.lua.org/bugs.html#5.1.4-5

plan(1);

error_like(function() debug.getfenv() end, 'value expected');

