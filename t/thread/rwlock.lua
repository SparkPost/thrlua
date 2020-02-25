-- vim:ts=2:sw=2:et:ft=lua:
require('Test.More')
require('strict')
local posix = require("posix")
plan(18)

local counter = 0
local rwlock = thread.rwlock()

is(rwlock:rdlock(), true, "main thread can lock for reads")
is(rwlock:unlock(), true, "main thread can unlock for reads")

is(rwlock:wrlock(), true, "main thread can lock for writes")
is(rwlock:unlock(), true, "main thread can unlock for writes")

-- TEST GROUP 1: Spawn two threads, one for testing the read lock,
-- one for testing the write lock. Use posix.sleep()
-- to ensure that the writer is blocked when the reader has the read lock,
-- and vice-versa.
diag("TEST GROUP 1: READ VS. WRITE LOCKS")

local reader = thread.create(
  function()
    rwlock:rdlock()
    is(counter, 0, "initial counter value")
    posix.sleep(2) -- ensure that read lock held when writer tries wrlock
    is(counter, 0, "still initial counter value")
    rwlock:unlock()

    -- writer should be able to lock here.
    -- Going straight into a read lock doesn't seem to allow
    -- the writer to grab the lock, resumably because it's biased
    -- in favour of readers?
    posix.sleep(1)

    rwlock:rdlock()
    is(counter, 1, "counter increment seen")
    rwlock:unlock()
  end)

local writer = thread.create(
  function()
    posix.sleep(1) -- let reader grab lock

    rwlock:wrlock()
    counter = counter + 1
    is(counter, 1, "counter incremented")
    rwlock:unlock()
  end)

is(reader:join(), true, "joined reader")
is(writer:join(), true, "joined writer")

-- Check GC
rwlock = nil
reader = nil
writer = nil
collectgarbage()

-- Try to verify that there are no race conditions
-- with multiple writers. Verify that one writer
-- with a lock blocks the other writers,
-- and that the writers execute once the wrlock is released.
diag("TEST GROUP 2: COMPETING WRITERS")
counter = 0
rwlock = thread.rwlock() -- new lock, please

local main_writer = thread.create(
  function()
    rwlock:wrlock()
    local val = 12345
    counter = val
    posix.sleep(3) -- block other writers for a while
    is(counter, val, "Counter not clobbered by other writers, main")
    rwlock:unlock()
  end)

local n_other_writers = 3
local other_writers = {}
for i = 1, n_other_writers do
  other_writers[i] = thread.create(
    function()
      posix.sleep(1) -- let main writer grab wrlock
      rwlock:wrlock()
      local val = i * 100000
      counter = val
      posix.sleep(1) -- block other writers for a while
      is(counter, val, "Counter not clobbered by other writers, " .. i)
      rwlock:unlock()
    end)
end

is(main_writer:join(), true, "joined main writer")
for i = 1, #other_writers do
  is(other_writers[i]:join(), true, "joined other writer " .. i)
end

-- Check GC
rwlock = nil
main_writer = nil
other_writers = nil
collectgarbage()

-- XXX: CONCURRENT READERS
-- XXX: COMPETING WRITER AND READERS
