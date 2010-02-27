require("tap");
plan(17)

-- function closures are powerful

-- traditional fixed-point operator from functional programming
Y = function (g)
      local a = function (f) return f(f) end
      return a(function (f)
                 return g(function (x)
                             local c=f(f)
                             return c(x)
                           end)
               end)
end


-- factorial without recursion
F = function (f)
      return function (n)
               if n == 0 then return 1
               else return n*f(n-1) end
             end
    end

factorial = Y(F)   -- factorial is the fixed point of F

-- now test it
t = {
[0] = 1.000000;
[1] = 1.000000;
[2] = 2.000000;
[3] = 6.000000;
[4] = 24.000000;
[5] = 120.000000;
[6] = 720.000000;
[7] = 5040.000000;
[8] = 40320.000000;
[9] = 362880.000000;
[10] = 3628800.000000;
[11] = 39916800.000000;
[12] = 479001600.000000;
[13] = 6227020800.000000;
[14] = 87178291200.000000;
[15] = 1307674368000.000000;
[16] = 20922789888000.000000;
};

for x=0,16 do
  local f = factorial(x)
--	io.write(string.format("[%d] = %f;\n", x, f))
  is(f, t[x], x .. "! is " .. t[x]);
end


