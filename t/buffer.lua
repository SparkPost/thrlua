-- vim:ts=2:sw=2:et:ft=lua:
require('Test.More')
require('buffer')
plan(8)

b = buffer.new(8192);
ok(b, "made a new buffer")
is(#b, 0, "len is zero")
is(b:write("hello"), 5, "wrote 5 chars")
is(#b, 5, "len is 5")
is(b:string(), "hello", "got hello from string method")
is(b:string(0, 1), "h", "got h as a substring")
is(b:string(1, 3), "el", "got el as a substring")
is(b:string(1), "ello", "got ello as a substring")
is(b:string(-1), nil, "range check")
is(b:string(100), nil, "range check")
is(b:string(0, 100), nil, "range check")
is(tostring(b), "hello", "got our string back out")

s = b:slice(0, 3);
ok(s, "made a slice")
is(#s, 3, "slice is 3")
is(tostring(s), "hel", "slice is hel")
is(s:write("foo", 0), 3, "wrote three")
is(tostring(s), "foo", "slice is foo")
is(tostring(b), "foolo", "writing to slice changed original buffer")

copy = buffer.new(4)
is(#copy, 0, "new buffer is empty")
is(b:copy(copy), 4, "copied 4 bytes (truncated)")
is(tostring(copy), "fool", "got right 4 bytes")
is(b:write("hello", 8192), 0, "range check")

bs = buffer.new("from string")
ok(bs, "made buffer from string")
is(#bs, 11, "got right length")
is(tostring(bs), "from string", "compares right")


