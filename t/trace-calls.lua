require("tap");
plan(8);


-- trace calls

local level=0
local events = {}
local expected = {
  "0 t/trace-calls.lua:60  sethook [C]",
  "0 t/trace-calls.lua:61  three <55:t/trace-calls.lua>",
  "1 t/trace-calls.lua:56  two <51:t/trace-calls.lua>",
  "2 t/trace-calls.lua:52  one <48:t/trace-calls.lua>",
  "3 t/trace-calls.lua:52  one <48:t/trace-calls.lua>",
  "2 t/trace-calls.lua:56  two <51:t/trace-calls.lua>",
  "1 t/trace-calls.lua:61  three <55:t/trace-calls.lua>",
  "0 t/trace-calls.lua:62  sethook [C]"
}

local function hook(event)
 local t=debug.getinfo(3)
 local msg = level .. " ";
 if t~=nil and t.currentline>=0 then 
    msg = msg .. t.short_src .. ":" .. t.currentline .. " "
 end
 t=debug.getinfo(2)
 if event=="call" then
  level=level+1
 else
  level=level-1 if level<0 then level=0 end
 end
 if t.what=="main" then
  if event=="call" then
    msg = msg .. "begin " .. t.short_src
  else
    msg = msg .. "end " .. t.short_src
  end
 elseif t.what=="Lua" then
    msg = msg .. " " .. (t.name or t.what) ..
        " <" .. t.linedefined .. ":" .. t.short_src .. ">"
 else
    msg = msg .. " " .. (t.name or "(C)") ..
        " [" .. t.what .. "]"
 end
 table.insert(events, msg)
end

function one()
end

function two()
  one()
end

function three()
  two()
end

level=0
debug.sethook(hook,"cr")
three()
debug.sethook()

for i = 1, table.maxn(events) do
  is(events[i], expected[i], i)
end


