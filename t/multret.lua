-- vim:ts=2:sw=2:et:ft=lua:
require('Test.More');
plan(4);

local instr = "HTTP/1.1 200 OK"
local pattern = [[%s*HTTP/1.[0,1]%s+([0-9]+)%s+(.-)%s*$]]

function test1()
  return instr and string.match(instr, pattern);
end

function test2()
  if instr then
    return string.match(instr, pattern)
  end
  return nil
end

local code1, desc1 = test1()
is(code1, "200", "code 200");
is(desc1, nil, "nil; mult-ret truncated to boolean and ignored");

local code2, desc2 = test2()
is(code2, "200", "code 200");
is(desc2, "OK", "OK");
