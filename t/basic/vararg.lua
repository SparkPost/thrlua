-- vim:ts=2:sw=2:et:ft=lua:
require('tap');
plan(14);

function vonly(...)
  return table.concat({...}, ", ");
end

function fixed1(one,...)
  return table.concat({...}, ", ");
end

function fixed2(one,...)
  return table.concat({one,...}, ", ");
end

function fixed3(one,...)
  return table.concat({one,...,6}, ", ");
end


function notv(a,b,c,d,e)
  return string.format("%d, %d, %d, %d, %d", a, b, c, d, e);
end

is(notv(1,2,3,4,5), "1, 2, 3, 4, 5", "got all numbers for notv");
is(vonly(1,2,3,4,5), "1, 2, 3, 4, 5", "got all numbers for vonly");
is(fixed1(1,2,3,4,5), "2, 3, 4, 5", "got 2-5");
is(fixed2(1,2,3,4,5), "1, 2, 3, 4, 5", "got all numbers for fixed2");
is(fixed3(1,2,3,4,5), "1, 2, 6", "multret truncates to first element");

function f(a, b)
  return a .. ", " .. tostring(b);
end

function g(a, b, ...)
  return a .. ", " .. tostring(b) .. ", " .. table.concat({...}, ", ");
end

function r()
  return 1, 2, 3
end

is(f(3), "3, nil", "pass 1");
is(f(3, 4), "3, 4", "pass 2");
is(f(3, 4, 5), "3, 4", "pass 3");
is(f(r(), 10), "1, 10", "pass multret func and 1 param");
is(f(r()), "1, 2", "pass multret");
is(g(3), "3, nil, ");
is(g(3, 4), "3, 4, ");
is(g(3, 4, 5, 8), "3, 4, 5, 8");
is(g(5, r()), "5, 1, 2, 3");


