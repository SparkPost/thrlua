require("Test.More");
plan(4);

-- make global variables readonly

local g={}
setmetatable(g, {
  __index = getfenv(),
  __newindex = function (t,i)
    error("cannot redefine global variable `"..i.."'",2)
  end
})
setfenv(1,g)

-- an example
rawset(g,"x",3)
is(x, 3, "rawset x = 3");
x=2
is(x, 2, "set x = 2 (allowed)");

local function catch_error(msg)
  return msg;
end

local res, msg = xpcall(function()
  y=1 -- cannot define `y'
end, catch_error)

is(res, false, "expected assignemnt to fail");
like(msg, "cannot redefine global variable `y'", "error is correct");

