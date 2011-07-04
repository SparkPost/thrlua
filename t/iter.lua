-- vim:ts=2:sw=2:et:ft=lua:
require('Test.More');
plan(8)

t = {"one", "two"}
got = {}
for _, v in ipairs(t) do
  table.insert(got, v)
end

is(#got, 2, "2 elements")
is(got[1], "one", "1: one")
is(got[2], "two", "2: two")

-- Test the error case (no metatable)
got = {}
error_like(function ()
  for _, v in t do -- note that the table is used in the "in" clause
    table.insert(got, v)
  end
end, "attempt to call a table value", "can't call a table")

is(#got, 0, "0 elements")

got = {}
setmetatable(t, { __iter = ipairs });
for _, v in t do -- note that the table is used in the "in" clause
  table.insert(got, v)
end

is(#got, 2, "2 elements")
is(got[1], "one", "1: one")
is(got[2], "two", "2: two")


