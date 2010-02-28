-- vim:ts=2:sw=2:et:ft=lua:
require('tap');
plan(4);

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

