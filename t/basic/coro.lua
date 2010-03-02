-- vim:ts=2:sw=2:et:ft=lua:
require('Test.More');
plan(17);

function foo(a)
  return coroutine.yield(2*a);
end

co = coroutine.create(function (a, b)
  local r = foo(a + 1)
  local r, s = coroutine.yield(a + b, a - b);
  return b, 'end';
end);
is(type(co), 'thread');

is_deeply({coroutine.resume(co, 1, 10)}, {true, 4});
is(coroutine.status(co), 'suspended');
is_deeply({coroutine.resume(co, 'r')}, {true, 11, -9});
is_deeply({coroutine.resume(co, 'x', 'y')}, {true, 10, 'end'});
is_deeply({coroutine.resume(co, 'x', 'y')}, {false, 'cannot resume dead coroutine'});
is(coroutine.status(co), 'dead');

is(coroutine.running(), nil, 'on main coro');

co = coroutine.create(function ()
  is(coroutine.running(), co, 'not on main coro');
  is(coroutine.status(co), 'running', 'gump');
end)

coroutine.resume(co);

co = coroutine.create(function ()
  local other = coroutine.create(function ()
    is(coroutine.status(co), 'normal');
  end);
  coroutine.resume(other);
end);
coroutine.resume(co);

wrap = coroutine.wrap(function ()
  coroutine.yield(2);
  return 'done';
end);
is(type(wrap), 'function');
is(wrap(), 2, 'called wrapper yields 2');
is(wrap(), 'done', 'called wrapper returned done');
res, err = pcall(wrap);
is(res, false, 'wrapper should be dead');
is(err, 'cannot resume dead coroutine');

res, err = pcall(function()
  coroutine.create(false)
end);
like(err, 'Lua function expected');


