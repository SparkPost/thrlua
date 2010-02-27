require("tap")
plan(9)

local expected = {
"t/trace-globals.lua:49: a is now 1 (was nil)",
"t/trace-globals.lua:50: b is now 2 (was nil)",
"t/trace-globals.lua:51: a is now 10 (was 1)",
"t/trace-globals.lua:52: b is now 20 (was 2)",
"t/trace-globals.lua:53: b is now nil (was 20)",
"t/trace-globals.lua:54: b is now 200 (was nil)",
}
local events = {}

-- trace assigments to global variables

do
 -- a tostring that quotes strings. note the use of the original tostring.
 local _tostring=tostring
 local tostring=function(a)
  if type(a)=="string" then
   return string.format("%q",a)
  else
   return _tostring(a)
  end
 end

 local log=function (name,old,new)
  local t=debug.getinfo(3,"Sl")
  local line=t.currentline
  local msg = t.short_src
  if line>=0 then 
    msg = msg .. ":" .. line
  end
  msg = msg .. ": " .. name .. " is now " .. tostring(new)
          .. " (was " .. tostring(old) .. ")"
  table.insert(events, msg)
 end

 local g={}
 local set=function (t,name,value)
  log(name,g[name],value)
  g[name]=value
 end
 setmetatable(getfenv(),{__index=g,__newindex=set})
end

-- an example

a=1
b=2
a=10
b=20
b=nil
b=200

is(a, 10, "a is 10");
is(b, 200, "b is 200");
is(c, nil, "c is nil (not set anywhere)");

for i=1, table.maxn(events) do
  is(events[i], expected[i], events[i])
end
