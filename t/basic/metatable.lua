-- vim:ts=2:sw=2:et:ft=lua:
require('tap');
plan(31);

a = {1, 2, 3};
mt = {};
function mt.__add(a, b)
  if type(b) == 'table' then
    return 5 + a;
  end
  return 3 + b; 
end
function mt.__sub(a, b)
  if type(b) == 'table' then
    return 5 - a;
  end
  return 3 - b; 
end
function mt.__mul(a, b)
  if type(b) == 'table' then
    return 5 * a;
  end
  return 3 * b; 
end
function mt.__div(a, b)
  if type(b) == 'table' then
    return 5 / a;
  end
  return 3 / b; 
end
function mt.__mod(a, b)
  if type(b) == 'table' then
    return 5 % a;
  end
  return 3 % b; 
end
function mt.__pow(a, b)
  if type(b) == 'table' then
    return 5 ^ a;
  end
  return 3 ^ b; 
end
function mt.__unm(a, b)
  if type(b) == 'table' then
    return -5;
  end
  return -3;
end
function mt.__concat(a, b)
  if type(b) == 'table' then
    return '5' .. a;
  end
  return '3' .. b; 
end
function mt.__len(a)
  error("this should never be invoked");
end
function mt.__eq(a, b)
  if type(b) == 'table' then
    return 5 == a;
  end
  return 3 == b; 
end
function mt.__lt(a, b)
  if type(b) == 'table' then
    return a < 5;
  end
  return b < 3; 
end
function mt.__le(a, b)
  if type(b) == 'table' then
    return a <= 5;
  end
  return b <= 3;
end
function mt.__index(a, b)
  if type(b) == 'table' then
    return 'idx_' .. a;
  end
  return 'idx_' .. b;
end

function mt.__newindex(tbl, k, v)
  rawset(tbl, k, v * 2);
end

function mt.__call(tbl, ...)
  return 'call:' .. table.concat({...}, ',');
end


setmetatable(a, mt);

is(1 + 1, 2);
is(a + 2, 5);
is(2 + a, 7);

is(1 - 1, 0);
is(a - 2, 1);
is(2 - a, 3);

is(1 * 1, 1);
is(a * 2, 6);
is(2 * a, 10);

is(3 / 2, 1.5);
is(a / 2, 1.5);
is(2 / a, 2.5);

is(3 % 2, 1);
is(a % 2, 1);
is(a % 10, 3);
is(2 % a, 1);
is(10 % a, 5);

is(3 ^ 2, 9);
is(a ^ 2, 9);
is(2 ^ a, 25);

is(-a, -5);

is(2 .. 1, '21');
is(a .. "a", '3a');
is("b" .. a, '5b');

is(#'hello', 5);
res, err = pcall(function()
  return #2
end);
is(res, false);
like(err, 'attempt to get length of a number value');

is(#a, 3, "three elements contained in table a");

is(a.foo, 'idx_foo');

a.bar = 21;
is(a.bar, 42);

is(a(1, 2, 3), 'call:1,2,3');

