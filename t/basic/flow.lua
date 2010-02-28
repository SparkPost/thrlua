require('tap');
plan(7);

if true then
  ok(true, "if true");
end

if false then
  ok(false, "don't get here");
elseif false then
  ok(false, "don't get here either");
else
  ok(true, "should get here");
end

g = true

if not g then
  ok(false, "don't get here");
elseif not g then
  ok(false, "don't get here");
elseif g then
  ok(true, "should get here");
else
  ok(false, "don't get here");
end

function take_expr(expr)
  if expr then
	diag("expr is true");
  else
	diag("expr is false");
  end
  return expr
end

if take_expr(true) then
	ok(true, "expr is true");
else
	ok(false, "expr should be true");
end

if take_expr(1 > 2) then
	ok(false, "one is not bigger than two");
else
	ok(true, "two is smaller than one");
end

local x = take_expr(1 > 2);
if x == false then
	ok(true, "two is smaller than one");
else
	ok(false, "one is not bigger than two");
end

local y = tostring(1 > 2);
diag(y);
if y == 'false' then
	ok(true, "two is smaller than one");
else
	ok(false, "one is not bigger than two");
end


diag("end");

