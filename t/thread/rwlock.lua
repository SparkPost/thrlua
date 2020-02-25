-- vim:ts=2:sw=2:et:ft=lua:
require('Test.More')
require('strict')
plan(4)

local counter = 0
local rwlock = thread.rwlock()

is(rwlock:rdlock(), true, "main thread can lock for reads")
is(rwlock:unlock(), true, "main thread can unlock for reads")

is(rwlock:wrlock(), true, "main thread can lock for writes")
is(rwlock:unlock(), true, "main thread can unlock for writes")
