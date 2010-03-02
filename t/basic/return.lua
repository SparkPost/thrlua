require 'Test.More'

plan(4);

function a(val)
  return val
end

function b(val)
  return a(val)
end

function c(val)
  ok(b(val) == val, "return value survived");
end

c(true)
c(false)
c(1 > 2);
c(2 > 1);

