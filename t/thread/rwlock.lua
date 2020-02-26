-- vim:ts=2:sw=2:et:ft=lua:
require('Test.More')
require('strict')
local posix = require("posix")
plan(83)

local rwlock

-- TEST GROUP 1: Verify basic functionality.
ok(1, "TEST GROUP 1: THE BASICS")

rwlock = thread.rwlock() -- new lock, please
is(rwlock:rdlock(), true, "main thread can lock for reads")
is(rwlock:unlock(), true, "main thread can unlock for reads")

rwlock = thread.rwlock() -- new lock, please
rwlock:rdlock()
is(rwlock:rdlock(), true, "main thread can lock twice for reads")
rwlock:unlock()
is(rwlock:unlock(), true, "main thread can unlock twice for reads")

rwlock = thread.rwlock() -- new lock, please
is(rwlock:wrlock(), true, "main thread can lock for writes")
is(rwlock:unlock(), true, "main thread can unlock for writes")

-- Verify that unlocking an unlocked rwlock doesn't fault/crash.
-- It may throw a Lua error, depending on what pthread_rwlock_unlock()
-- returns. On Linux, man page says: "Results are undefined
-- if the read-write lock rwlock is not held by the calling thread."
-- Wrap it in a pcall() to cope with case where it would throw a Lua error.
rwlock = thread.rwlock() -- new lock, please
pcall(function() rwlock:unlock() end)

-- Check GC
rwlock = nil
collectgarbage()

-- TEST GROUP 2: Spawn two threads, one for testing the read lock,
-- one for testing the write lock. Use posix.sleep()
-- to ensure that the writer is blocked when the reader has the read lock,
-- and vice-versa.
ok(1, "TEST GROUP 2: READ VS. WRITE LOCKS")

local counter = 0
rwlock = thread.rwlock() -- new lock, please

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

-- TEST GROUP 3: Verify that there are no race conditions
-- with multiple writers. Verify that one writer
-- with a lock blocks the other writers,
-- and that the writers execute once the wrlock is released.
ok(1, "TEST GROUP 3: COMPETING WRITERS")
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
      is(counter, val, "Counter not clobbered by other writers " .. i)
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

-- TEST GROUP 4: Verify that there are no race conditions
-- with multiple readers. Verify that multiple readers
-- can read values concurrently and do not block each other.
ok(1, "TEST GROUP 4: CONCURRENT READERS")

-- Check global variable "counter" has expected value.
function check_unexpected(expected_counter)
  local unexpected = 0
  local logged = false
  for j = 1, 100000 do
    local val = counter
    if val ~= expected_counter then
      unexpected = unexpected + 1
      if not logged then
        print("EXPECTED", expected_counter, "GOT", val)
        logged = true
      end
    end
  end
  return unexpected
end

local expected_counter = math.random()
counter = expected_counter
rwlock = thread.rwlock() -- new lock, please

local n_readers = 16
local readers = {}
local readers_results = {}
local readers_start_tv, readers_end_tv

readers_start_rv = { posix.gettimeofday() }
for i = 1, n_readers do
  readers[i] = thread.create(
    function()
      local start_tv, end_tv
      local unexpected = 0

      rwlock:rdlock()
      start_tv = { posix.gettimeofday() }
      unexpected = check_unexpected(expected_counter)
      end_tv = { posix.gettimeofday() }
      -- Sleep so lock is held for long enough that we can tell
      -- if rdlock is causing each thread to block until previous thread
      -- has unlocked.
      posix.sleep(1)
      rwlock:unlock()

      readers_results[i] = {
        start_tv = start_tv,
        end_tv = end_tv,
        unexpected = unexpected
      }
    end)
end

for i = 1, #readers do
  is(readers[i]:join(), true, "joined reader " .. i)
end
readers_end_rv = { posix.gettimeofday() }

-- Verify that there were no unexpected results,
-- and the threads started roughly
-- around the same time.
local function tv_in_ms(tv)
  return (tv[1] * 1000) + (tv[2] / 1000)
end

-- Use a small fuzz here -- less than the posix.sleep() above.
-- This allows us to detect if read locks can be grabbed
-- concurrently, or are serialized. Parallel operation is expected.
-- If the start times are all offset by e.g.: 1 second,
-- that would imply that the read locks were acquired in serial,
-- one thread after another.
local start_with_fuzz_ms = tv_in_ms(readers_start_rv) + 100 -- ms

for i = 1, #readers do
  local results = readers_results[i]
  local results_start_ms = tv_in_ms(results.start_tv)

  is(results.unexpected, 0, "Reads got expected value " .. i)
  cmp_ok(results_start_ms, "<=", start_with_fuzz_ms,
      "Started at expected time " .. i)
end

-- Check GC
rwlock = nil
readers = nil
collectgarbage()

-- TEST GROUP 5: Verify that there are no race conditions
-- with multiple readers and one writer. Verify that the writer
-- cannot modify the counter when the readers are reading.
ok(1, "TEST GROUP 5: COMPETING WRITER AND READERS")

local initial_counter = 12345678
local final_counter = 87654321
counter = initial_counter
rwlock = thread.rwlock() -- new lock, please

-- After allowing reads to grab the rdlock, this will be used
-- to force the readers to wait on the writer, so that the writer
-- can grab the wrlock and change the value.
local wait_on_writer_lock = thread.mutex()

n_readers = 4
readers = {}
readers_results = {}

for i = 1, n_readers do
  readers[i] = thread.create(
    function()
      local initial_unexpected = 0
      local final_unexpected = 0

      -- Check that the writer is unable to modify
      -- the counter while the read lock is held.
      rwlock:rdlock()
      initial_unexpected = check_unexpected(initial_counter)
      rwlock:unlock()

      -- Wait for the writer to grab the write lock
      -- and start pushing updates.
      wait_on_writer_lock:lock()
      wait_on_writer_lock:unlock()

      -- This should block until the writer has finished
      -- writing and released the write lock.
      -- If it doesn't block for some reason,
      -- we should see a different counter value than expected.
      rwlock:rdlock()
      final_unexpected = check_unexpected(final_counter)
      rwlock:unlock()

      readers_results[i] = {
        initial_unexpected = initial_unexpected,
        final_unexpected = final_unexpected
      }
    end)
end

writer = thread.create(
  function()
    -- We should not be able to get the write lock here
    -- until all the readers have dropped their read locks.
    -- We then block the readers so this thread can grab
    -- the write lock.
    wait_on_writer_lock:lock()
    rwlock:wrlock()
    wait_on_writer_lock:unlock()

    -- Rather than just setting the final counter value,
    -- use increments to keep this thread busy for a while.
    counter = 0
    for i = 1, final_counter do
      counter = counter + 1
    end

    rwlock:unlock()
  end)

is(writer:join(), true, "joined writer")
for i = 1, #readers do
  is(readers[i]:join(), true, "joined reader " .. i)
end

is(counter, final_counter, "Final counter")
for i = 1, #readers do
  local results = readers_results[i]
  is(results.initial_unexpected, 0, "Initial counter expected " .. i)
  is(results.final_unexpected, 0, "Final counter expected " .. i)
end

-- Check GC
rwlock = nil
readers = nil
writer = nil
collectgarbage()
