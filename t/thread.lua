-- vim:ts=2:sw=2:et:ft=lua:
require('Test.More');
plan(10);

local counter = 0;
local m = thread.mutex();

is(m:lock(), true, "main thread owns mutex");

function dostuff(t)
  for i = 0, 10 do
    counter = counter + 1;
    thread.sleep(1);
  end
  is(counter, 11, "thread sees counter value 11");
end

local t = thread.create(function (t)
  --[[ We deliberately attempt to unlock a mutex that we don't
  --   own.  Expect to see one such error in the DRD output
  --]]
  error_like(function()
      local r = m:unlock();
      if r == true then
        error("platform doesn't check errors in mutexes");
      end
      error("got error number" .. tostring(t));
    end, "failed to unlock mutex", "unlock an unowned mutex");
  is(type(t), "userdata", "thread sees thread userdata");
  is(m:lock(), true, "got mutex from main thread");
  is(m:unlock(), true, "unlocked mutex in other thread");
  dostuff("other");
end);

-- the thread is waiting on our mutex; let it go
is(m:unlock(), true, "unlocked mutex in main thread");
-- wait for thread to complete
is(t:join(), true, "joined thread");
is(type(t), "userdata", "got a thread userdata");
is(counter, 11, "main sees counter value 11");

