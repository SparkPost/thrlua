-- vim:ts=2:sw=2:et:
require('tap');
plan(49);

is(math.abs(-1), 1);
is(math.sin(30), -0.98803162409286, 'sin');
is(math.sinh(30), 5343237290762.2, 'sinh');
is(math.cos(30),  0.15425144988758, 'cos');
is(math.cosh(30), 5343237290762.2, 'cosh');
is(math.tan(30), -6.4053311966463, 'tan');
is(math.tanh(30), 1, 'tanh');

is(math.asin(0.9), 1.1197695149986, 'asin');
is(math.acos(0.9),  0.45102681179626, 'acos');
is(math.atan(0.9), 0.73281510178651, 'atan');
is(math.atan2(20, 10), 1.1071487177941, 'atan2');

is(math.ceil(1.5), 2, 'ceil');
is(math.floor(1.5), 1, 'floor');

is_deeply({math.modf(1.5)}, {1, 0.5}, 'modf');
is(math.fmod(10, 3), 1, 'fmod');
is(math.sqrt(9), 3, 'sqrt');

is(math.pow(2, 2), 4, 'pow');
is(math.log(3), 1.0986122886681, 'log');
is(math.log10(3), 0.47712125471966, 'log10');
is(math.exp(2, 3), 7.3890560989307, 'exp');
is(math.rad(30), 0.5235987755983, 'rad');
is(math.deg(0.5235987755983), 30, 'deg');
is(math.frexp(2, 3), 0.5, 'frexp');
is(math.ldexp(2, 3), 16, 'ldexp');

is(math.min(5, 6, 7), 5, 'min');
is(math.min(7, 5, 6, 7), 5, 'min');
is(math.max(5, 6, 7), 7, 'max');

cmp_ok(math.random(), '>=', 0, 'random');
math.randomseed(os.clock())
for i = 1, 10 do
  n = math.random(5, 10);
  cmp_ok(n, '>=', 5, 'random: lower ' .. n);
  cmp_ok(n, '<=', 10, 'random: upper ' .. n);
end

res, err = pcall(function()
  math.random(1, 2, 3)
end);
like(err, 'wrong number of arguments');

