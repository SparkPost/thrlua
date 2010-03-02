require("Test.More");
plan(4);


-- trace calls

local level=0
local events = {}

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
fn, mask, count = debug.gethook()
three()
debug.sethook()

is(fn, hook);
is(mask, 'cr');
is(count, 0);

local expected = {
  "0 t/trace-calls.lua:50  sethook [C]",
  "0 t/trace-calls.lua:51  gethook [C]",
  "1 t/trace-calls.lua:51  gethook [C]",
  "0 t/trace-calls.lua:52  three <45:t/trace-calls.lua>",
  "1 t/trace-calls.lua:46  two <41:t/trace-calls.lua>",
  "2 t/trace-calls.lua:42  one <38:t/trace-calls.lua>",
  "3 t/trace-calls.lua:42  one <38:t/trace-calls.lua>",
  "2 t/trace-calls.lua:46  two <41:t/trace-calls.lua>",
  "1 t/trace-calls.lua:52  three <45:t/trace-calls.lua>",
  "0 t/trace-calls.lua:53  sethook [C]"
}

is_deeply(events, expected);


