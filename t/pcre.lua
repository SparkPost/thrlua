-- vim:ts=2:sw=2:et:ft=lua:
require("tap");
require("pcre");

plan(75);

matches, e = pcre.match("hello there", "^(\\S+)\\s+(\\S+)$");

is(e, nil, "no error condition");
-- lua nuance; arrays start with the "1"st element, and this influences
-- the count.  The match above populates elements 0, 1, 2; this manifests
-- as a table with 2 elements; the concat routine concats elements 1 and 2.
-- Element 0 can be explicitly accessed, but it otherwise invisible.
is(#matches, 2, "2 items in returned match");
is(matches[0], "hello there", "0th element is captured");
is(table.concat(matches, ";"), "hello;there", "sub patterns captured");


matches, e = pcre.match("hello there", "^(?P<one>\\S+)\\s+(?P<two>\\S+)$");

is(e, nil, "no error condition");
is(#matches, 2, "2 items in returned match");
is(matches[0], "hello there", "0th element is captured");
is(table.concat(matches, ";"), "hello;there", "sub patterns captured");

ip = "";
for name, val in ipairs(matches) do
  ip = ip .. name .. ";" .. val .. ";"
end
is(ip, "1;hello;2;there;", "ipairs iterates numerics");

names = {}
for name, _ in pairs(matches) do
  table.insert(names, name);
end

function stringy_sort(a, b)
  a = tostring(a);
  b = tostring(b);
  return a < b;
end

table.sort(names, stringy_sort);
ip = "";
for _, name in ipairs(names) do
  val = matches[name];
  ip = ip .. name .. ";" .. val .. ";"
end
is(ip, "0;hello there;1;hello;2;there;one;hello;two;there;",
  "pairs iterates all");


matches, e = pcre.match("hello there", "\\S+\\s+\\S+");

is(e, nil, "no error condition");
is(#matches, 0, "0 items in returned match");

if matches then
  ok(1, "matches evaluates to true");
else
  ok(0, "matches did not evaluate true");
end

res = pcre.replace("the quick brown", "quick", "slow");
is(res, "the slow brown", "simple replacement");

res = pcre.replace("the quick quick brown", "quick", "slow");
is(res, "the slow slow brown", "replacement of two occurrences");

res = pcre.replace("the quick lemon quick quick brown", "quick", "slow");
is(res, "the slow lemon slow slow brown", "replacement of three occurrences");

res = pcre.replace("the quick lemon quick quick brown", "(qu)ick", "$1slow");
is(res, "the quslow lemon quslow quslow brown", "replacement with $numeric backref");

res = pcre.replace("the quick lemon quick quick brown", "(qu)ick", "\\1slow");
is(res, "the quslow lemon quslow quslow brown", "replacement with \\numeric backref");

res = pcre.replace("the quick lemon quick quick brown", "(qu)ic(k)", "$1sl$2ow");
is(res, "the quslkow lemon quslkow quslkow brown", "replacement with two $numeric backref");

res = pcre.replace("the quick lemon quick quick brown", "(qu)ic(k)", "${1}sl${2}ow");
is(res, "the quslkow lemon quslkow quslkow brown", "replacement with two $numeric backref, using braces");


res = pcre.replace("the quick lemon quick quick brown", "(?P<name>qu)ic(k)", "${name}sl$2ow");
is(res, "the quslkow lemon quslkow quslkow brown", "replacement with named backref");

res = pcre.replace("the quick lemon quick quick brown", "(?P<name>qu)ic(k)", "${name}sl$2ow", 2);
is(res, "the quslkow lemon quslkow quick brown", "replacement with limit");



res = pcre.replace("the quick lemon quick quick brown",
  "(?P<name>qu)ic(k)",
  function (t)
    is(t[0], "quick", "$0 is correct");
    is(t["name"], "qu", "$name is correct");
    is(t[2], "k", "$2 is correct");
    return nil;
  end);
is(res, "the quick lemon quick quick brown", "callback operated correctly");

res = pcre.replace("the quick lemon quick quick brown",
  "(?P<name>qu)ic(k)",
  function (t)
    is(t[0], "quick", "$0 is correct");
    is(t["name"], "qu", "$name is correct");
    is(t[2], "k", "$2 is correct");
    return "foo";
  end);
is(res, "the foo lemon foo foo brown", "callback operated correctly");

res = pcre.split("the quick brown fox", "\\s+");
is(#res, 4, "split into 4 strings");
is(res[1], "the", "the");
is(res[2], "quick", "quick");
is(res[3], "brown", "brown");
is(res[4], "fox", "fox");

res = pcre.split("the quick brown fox", "\\s+", 4);
is(res[1], "the", "the");
is(res[2], "quick", "quick");
is(res[3], "brown", "brown");
is(res[4], "fox", "fox");

res = pcre.split("the quick brown fox", "\\s+", 5);
is(res[1], "the", "the");
is(res[2], "quick", "quick");
is(res[3], "brown", "brown");
is(res[4], "fox", "fox");

res = pcre.split("the quick brown fox", "\\s+", 3);
is(#res, 3, "split with limit");
is(res[1], "the", "the");
is(res[2], "quick", "quick");
is(res[3], "brown fox", "brown fox");

res = pcre.split("the quick brown fox", "\\s+", 2);
is(#res, 2, "split with limit");
is(res[1], "the", "the");
is(res[2], "quick brown fox", "3 words");


res = pcre.split("the quick brown fox", "\\s+", 1);
is(#res, 1, "split with limit");
is(res[1], "the quick brown fox", "full string");



res = pcre.split("the quick brown fox", "(\\s+|brown\\s+)");
is(#res, 3, "split into 3 strings");
is(res[1], "the", "the");
is(res[2], "quick", "quick");
is(res[3], "fox", "fox");

res, err, code = pcre.match("empty pattern", "");
is(res, nil, 'empty pattern returns nil');
is(err, 'an empty pattern was provided');
is(code, 0);

res, err, code = pcre.match("bogus pattern", "asd(");
is(res, nil, 'bogus pattern returns nil');
is(err, 'missing )', 'got reason from pcre');
is(code, 4, 'offset is 4');

is(pcre.replace("hello there", "l(o)", "hey \\\\1 \\1"),
  'helhey \\1 o there',
  "quoted backref");
