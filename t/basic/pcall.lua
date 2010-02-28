-- vim:ts=2:sw=2:et:ft=lua:
require('tap');
plan(8);

res, err = pcall(function()
 return "ok";
end);
is(res, true, "expected true return from pcall");
is(err, "ok", "expected string 'ok' return value");

res, err = pcall(function()
 error("bleep");
 return "ok";
end);
is(res, false, "expected false return from pcall");
like(err, "bleep", "expected bleep string in error message");

res, err = pcall(function()
 assert(false);
 return "ok";
end);
like(err, 'assertion failed');

res, err = pcall(function()
 assert(false, 'woot');
 return "ok";
end);
like(err, 'woot');

res, err = pcall(function()
 assert(true, 'woot');
 return "ok";
end);
is(res, true);
is(err, 'ok');

