-- vim:ts=2:sw=2:et:ft=lua:
require('Test.More');
plan(24);

x = 10
do
  local x = x
  is(x, 10);
  x = x + 1
  do
    local x = x + 1
    is(x, 12);
  end
  is(x, 11);
end
is(x, 10);

a = {}
local x = 20
for i = 1, 10 do
  local y = 0
  a[i] = function ()
    y = y + 1;
    return x + y
  end
end

for i = 1, 10 do
  is(a[i](), 21);
  is(a[i](), 22);
end


