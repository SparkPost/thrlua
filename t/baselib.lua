-- vim:ts=2:sw=2:et:ft=lua:
require('tap');
plan(28);

is(tonumber('0'), 0);
is(tonumber('10'), 10);
is(tonumber('10', 16), 16);

res, err = pcall(function()
  tonumber('10', 1)
end);
like(err, "base out of range");

res, err = pcall(function()
  tonumber('10', 37)
end);
like(err, "base out of range");


is(tonumber('lemon'), nil);

is(tonumber('10 '), 10);
is(tonumber('10 foo'), nil);

cmp_ok(gcinfo(), '>', 0);
cmp_ok(collectgarbage('count'), '>', 0);
is(type(collectgarbage('step')), 'boolean');

f = loadfile('t/baselib.lua');
is(type(f), 'function');

f = load(function()
 return nil
end)
is(type(f), 'function');

res, err = pcall(function()
  dofile('a file that does not exist');
end)
like(err, 'No such file or directory');

name = os.tmpname();
o = io.open(name, 'w');
o:write('return 42;');
o:close();
is(dofile(name), 42);

is_deeply({unpack({1,2,3})}, {1, 2, 3});
is_deeply({unpack({1,2,3,4,5}, 3, 4)}, {3,4});

is_deeply({select(1, 4, 5, 6)}, {4, 5, 6});
is_deeply({select(2, 4, 5, 6)}, {5, 6});
is_deeply({select(3, 4, 5, 6)}, {6});
is(select('#', 4, 5, 6), 3);
res, err = pcall(function()
  select(-123, 1, 2, 3)
end)
like(err, 'index out of range');

t = {};
like(tostring(t), 'table:');

-- undocumented, tested here for coverage purposes only
p = newproxy();
is(type(p), 'userdata');
is(getmetatable(p), nil);

p = newproxy(true);
is(type(p), 'userdata');
isnt(getmetatable(p), nil);

mt = {};
setmetatable(t, mt);
res, err = pcall(function()
  p = newproxy(t);
end);
like(err, 'boolean or proxy expected');


