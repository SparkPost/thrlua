require 'Test.More'
plan(1);

function norm (x, y)
  --[[
  local a = x ^ 2
  local b = y ^ 2
  local c = a + b
  return c ^ 0.5
  ]]
  return (x^2 + y^2)^0.5
end

function twice (x)
    return 2*x
end

n = norm(3.4, 1.0)
like(twice(n), '^7%.088', "function dofile")


