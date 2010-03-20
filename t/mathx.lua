-- test mathx library
require('Test.More');
require"mathx"

plan(33);

function f(x)
 return tostring(x),math.isfinite(x),math.isnan(x),math.isnormal(x),math.fpclassify(x)
end

is_deeply({f(0)}, {'0', true, false, false, 'zero'});
is_deeply({f(1/-math.infinity)}, {'-0', true, false, false, 'zero'});
is_deeply({f(1/0)}, {'inf', false, false, false, 'inf'});
is_deeply({f(0/0)}, {'nan', false, true, false, 'nan'});
is_deeply({f(math.infinity)}, {'inf', false, false, false, 'inf'});
is_deeply({f(math.nan)}, {'nan', false, true, false, 'nan'});
is_deeply({f(3.45)}, {'3.45', true, false, true, 'normal'});

x=1
while x~=0 do
 y,x=x,x/2
 if not math.isnormal(x) then break end
end
is_deeply({f(y)}, {'2.2250738585072e-308', true, false, true, 'normal'});
is_deeply({f(x)}, {'1.1125369292536e-308', true, false, false, 'subnormal'});
is_deeply({f(math.nextafter(x,1))}, {'1.1125369292536e-308', true, false, false, 'subnormal'});
is_deeply({f(math.nextafter(0,1))}, {'4.9406564584125e-324', true, false, false, 'subnormal'});

while x~=0 do
 y,x=x,x/2
end
is_deeply({f(y)}, {'4.9406564584125e-324', true, false, false, 'subnormal'});
is_deeply({f(x)}, {'0', true, false, false, 'zero'});

function f(x)
 local a=math.nextafter(x,-1/0)
 local b=math.nextafter(x, 1/0)
 return a,x,b,x-a,b-x
end
is_deeply({f(1)}, {1, 1, 1, 1.1102230246252e-16, 2.2204460492503e-16});
is_deeply({f(0)}, {-4.9406564584125e-324, 0, 4.9406564584125e-324, 4.9406564584125e-324, 4.9406564584125e-324});

is(math.fmax(1,math.nan,2), 2);
is(tostring(math.max(math.nan,1,2)), 'nan');
is(math.fmin(1,math.nan,2), 1);
is(tostring(math.min(math.nan,1,2)), 'nan');

is(math.log2(32), 5);
is(math.exp2(5), 32);
is(math.cbrt(2), 1.2599210498949);
is(math.gamma(6+1), 720);
is(math.hypot(5,12), 13);
is(math.fma(3,2,1), 7);

function f(x)
	return x,math.floor(x),math.trunc(x),math.round(x),math.ceil(x)
end
is_deeply({f(-1.2)}, {-1.2, -2, -1, -1, -1});
is_deeply({f(-1.7)}, {-1.7, -2, -1, -2, -1});
is_deeply({f(1.2)}, {1.2, 1, 1, 1, 2});
is_deeply({f(1.7)}, {1.7, 1, 1, 2, 2});

function f(x,y)
	return x,y,x%y,math.remainder(x,y)
end
is_deeply({f(13,3)}, {13, 3, 1, 1});
is_deeply({f(13,-3)}, {13, -3, -2, 1});
is_deeply({f(-13,3)}, {-13, 3, 2, -1});
is_deeply({f(-13,-3)}, {-13, -3, -1, -1});

