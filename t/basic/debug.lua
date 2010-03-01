-- vim:ts=2:sw=2:et:ft=lua:
require('tap');
plan(21);

is(type(debug.getregistry()), 'table');

local lemon = 42;

function foo(a)
  return a + lemon
end

is(foo(1), 43);
is(debug.setupvalue(foo, 1, 23), 'lemon');
is(lemon, 23);
is_deeply({debug.getupvalue(foo, 1)}, {'lemon', 23});
is(foo(1), 24);

t = {};
mt = {};
ok(debug.setmetatable(t, mt));
is(getmetatable(t), mt);
is(debug.getmetatable(t), mt);
e = debug.getfenv(foo);
is(e, _G);
is(debug.setfenv(foo, e), foo);

is(debug.setlocal(1, 1, 42), 'lemon');
is(lemon, 42);
is(foo(1), 43);

function a1()
  local x = debug.traceback();
  return x;
end

function a2()
  local x = a1();
  return x;
end

function a3()
  local x = a2();
  return x;
end

tb = a3();
like(tb, 'stack traceback');
like(tb, 'a3');
like(tb, 'a2');
like(tb, 'a1');

fn, mask, count = debug.gethook()
is(fn, nil);
is(mask, '');
is(count, 0);

