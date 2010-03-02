require 'Test.More'
plan(3);

function bnorm (x, y)
  local a = x ^ 2
  local b = y ^ 2
  return (a + b) ^ 0.5
end
function norm (x, y)
  return (x^2 + y^2)^0.5
end
function cnorm (x, y)
  return ((x^2) + (y^2))^0.5
end
function twice (x)
    return 2*x
end

n = norm(3.4, 1.0)
like(twice(n), '^7%.088');

n = bnorm(3.4, 1.0)
like(twice(n), '^7%.088');

n = cnorm(3.4, 1.0)
like(twice(n), '^7%.088');


