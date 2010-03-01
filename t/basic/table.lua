-- vim:ts=2:sw=2:et:ft=lua:
require('tap');
plan(9);

local t = {1, 2, 3};
local res;

res = '';
for k, v in pairs(t) do
  res = res .. string.format("%s %s\n", k, v);
end

is(res, "1 1\n2 2\n3 3\n", 'pairs result');

local resi = '';
table.foreach(t, function(k, v)
  resi = resi .. string.format("%s %s\n", k, v);
end)
is(resi, res, 'table.foreach gives same results');

res = '';
for k, v in ipairs(t) do
  res = res .. string.format("%s %s\n", k, v);
end

is(res, "1 1\n2 2\n3 3\n", 'ipairs result');

local resi = '';
table.foreachi(t, function(k, v)
  resi = resi .. string.format("%s %s\n", k, v);
end)
is(resi, res, 'table.foreachi gives same results');

table.remove(t, 2);
is_deeply(t, {1, 3}, 'removed 2');
t.foo = 'bar';
is_deeply(t, {1, 3, foo = 'bar'}, 'added string key');
table.remove(t, 1);
is_deeply(t, {foo = 'bar', 3}, 'removed 1');
res, err = pcall(function()
  table.remove(t, 'foo');
end);
like(err, 'number expected');
t.foo = nil;
is_deeply(t, {3}, 'removed foo');


