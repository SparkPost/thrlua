-- vim:ts=2:sw=2:et:ft=lua:
require("Test.More");
require("curl");

plan(8);

local res, err, code;

c = curl.new();
res, err, code = c:setopt(curl.OPT_URL, "https://svn.messagesystems.com")
ok(res, "set url")


local payload = '';
res, err, code = c:setopt(curl.OPT_WRITEFUNCTION, function (h, data)
	payload = payload .. data;
	return #data
end)
ok(res, "set callback")

res, err, code = c:perform()
ok(res, "executed fetch")

like(payload, "<html>", "there's html in there");

is(curl.escape("hello there"), "hello%20there", "escape functions");
is(curl.unescape("hello%20there"), "hello there", "unescape");

-- test that the __gc method is safe to call after close
-- triggers
ok(c:close(), "called close");
c = nil;
ok(collectgarbage('collect'), 'collected')


