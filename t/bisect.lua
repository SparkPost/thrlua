require("Test.More")

plan(2)

-- bisection method for solving non-linear equations

delta=1e-6	-- tolerance

function bisect(f,a,b,fa,fb)
 local c=(a+b)/2
-- diag(n .. " c=" .. c .. " a=" .. a .. " b=" .. b)
 if c==a or c==b or math.abs(a-b)<delta then return c,b-a end
 n=n+1
 local fc=f(c)
 if fa*fc<0 then return bisect(f,a,c,fa,fc) else return bisect(f,c,b,fc,fb) end
end

-- find root of f in the inverval [a,b]. needs f(a)*f(b)<0
function solve(f,a,b)
 n=0
 local z,e=bisect(f,a,b,f(a),f(b))
 diag(string.format("after %d steps, root is %.17g with error %.1e, f=%.1e",n,z,e,f(z)))
 is(n, 20, "takes 20 steps to solve")
 cmp_ok(math.abs(1.3247179985046387 - z), "<", delta, "root is approx. correct")
end

-- our function
function f(x)
 return x*x*x-x-1
end

-- find zero in [1,2]
solve(f,1,2)
