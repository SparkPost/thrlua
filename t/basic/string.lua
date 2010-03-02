-- vim:ts=2:sw=2:et:
require('Test.More');
plan(43);

is(string.char(65, 32, 66), 'A B');

local haystack = "find the needle in the haystack";

is_deeply({string.find(haystack, 'needle')}, {10, 15});
is_deeply({string.find(haystack, 'ne*dle')}, {10, 15});
is_deeply({string.find(haystack, 'needle', 1, true)}, {10, 15});
is_deeply({string.find(haystack, 'the', 1, true)}, {6, 8});
is_deeply({string.find(haystack, 'the', 9, true)}, {20, 22});

is(string.format('%q', 'hello'), '"hello"');
is(string.format('%s', 'hello'), 'hello');
is(string.format('%c', 65), 'A');
is(string.format('%d', 65), '65');
is(string.format('%d', 1.5), '1');
is(string.format('%i', 65), '65');
is(string.format('%i', 1.5), '1');
is(string.format('%u', 65), '65');
is(string.format('%u', 1.5), '1');
is(string.format('%e', 1.5), '1.500000e+00');
is(string.format('%E', 1.5), '1.500000E+00');
is(string.format('%f', 1.5), '1.500000');
is(string.format('%g', 1.5), '1.5');
is(string.format('%G', 1.5), '1.5');
is(string.format('%o', 5), '5');
is(string.format('%o', 8), '10');
is(string.format('%x', 242), 'f2');
is(string.format('%X', 242), 'F2');

m = {};
for w in string.gmatch(haystack, '%a+') do
  table.insert(m, w);
end
is_deeply(m, {
  'find', 'the', 'needle', 'in', 'the', 'haystack'
});

m = {};
s = "from=world, to=Lua";
for k, v in string.gmatch(s, "(%w+)=(%w+)") do
  m[k] = v;
end
is_deeply(m, {
  from = 'world',
  to = 'Lua'
});

is(string.gsub("hello world", "(%w+)", "%1 %1"),
  "hello hello world world");

is(string.gsub("hello world", "%w+", "%0 %0", 1),
     "hello hello world");
     
is(string.gsub("hello world from Lua", "(%w+)%s*(%w+)", "%2 %1"),
     "world hello Lua from");

function env(name)
  if name == 'HOME' then
    return '/Users/wez';
  elseif name == 'USER' then
    return 'wez';
  end
end

is(string.gsub("home = $HOME, user = $USER", "%$(%w+)", env),
    "home = /Users/wez, user = wez");
     
is(string.gsub("4+5 = $return 4+5$", "%$(.-)%$",
  function (s)
    return loadstring(s)()
  end),
  "4+5 = 9");
    
local t = {name="lua", version="5.1"}
is_deeply({string.gsub("$name-$version.tar.gz", "%$(%w+)", t)},
     {"lua-5.1.tar.gz", 2});

is(string.len('hello'), #'hello');
is(string.len('hello'), 5);

is(string.lower('HELLO'), 'hello');

is_deeply(
  {string.match("home = $HOME, user = $USER", "(%w+) = %$(%w+)")},
  {'home', 'HOME'});

is_deeply(
  {string.match("home = $HOME, user = $USER", "(%w+) = %$(%w+)", 10)},
  {'user', 'USER'});

is(string.rep('a', 3), 'aaa');
is(string.reverse('hello'), 'olleh');
is(string.sub(haystack, 23), ' haystack');
is(string.sub(haystack, 24, 26), 'hay');
is(string.sub(haystack, -5), 'stack');
is(string.upper('hello'), 'HELLO');

