-- vim:ts=2:sw=2:et:ft=lua:
require('Test.More');
plan(20);

local counter = 0;
local m = thread.mutex();

is(m:lock(), true, "main thread owns mutex");

is(type(_TLS), "table", "_TLS is present as a table");
is(type(_OSTLS), "table", "_OSTLS is present as a table");

_TLS.n = "main";
is(_TLS.n, "main", "main thread sees main in _TLS");

_OSTLS.n = "main";
is(_OSTLS.n, "main", "main thread sees main in _OSTLS");

function dostuff(t)
  for i = 0, 10 do
    counter = counter + 1;
    thread.sleep(1);
  end
  is(counter, 11, "thread sees counter value 11");
end

local start_sync = thread.mutex();
start_sync:lock()

local t = thread.create(function (t)
  start_sync:lock()

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

  isnt(_TLS.n, "main", "other thread doesn't see main in _TLS");
  _TLS.n = "other";
  isnt(_OSTLS.n, "main", "other thread doesn't see main in _OSTLS");
  _OSTLS.n = "other";

  local co = coroutine.create(function ()
    is(_TLS.n, nil, "coroutine sees no value in _TLS");
    is(_OSTLS.n, "other", "coroutine sees other in _OSTLS");
  end);
  coroutine.resume(co);

  is(type(t), "userdata", "thread sees thread userdata");
  is(start_sync:unlock(), true, "unlocked mutex in other thread");
  dostuff("other");
end);

start_sync:unlock()

-- wait for thread to complete
is(t:join(), true, "joined thread");
is(type(t), "userdata", "got a thread userdata");
is(counter, 11, "main sees counter value 11");

is(_TLS.n, "main", "_TLS is still set to main");
is(_OSTLS.n, "main", "_OSTLS is still set to main");

-- more threads with upvalues

local uv1 = 0
local uv2 = 0
local uvm = thread.mutex();

local t = {};

local function upvaltestfunc(me)
  -- want to demonstrate that increments on uv1, which are
  -- done without a mutex, may end up with indeterminate end
  -- results, whereas uv2, which is protected by the uvm mutex
  -- has a consistent end value
  -- We also want to exercise the collector on each thread

  -- unreliable counter (not thread safe)
  for i = 0, 100 do
    uv1 = uv1 + 1;
  end
  -- coordinated counter (uses mutex)
  for i = 0, 100 do
    uvm:lock()
    uv2 = uv2 + 1;
    uvm:unlock();
  end
  for i = 0, 10 do
    local tbl = {};
    for j = 0, 15 do
      tbl[j] = string.rep(tostring(i), i);
    end
  end
end

for i = 1, 4 do
  table.insert(t, thread.create(upvaltestfunc));
end

for _, thr in pairs(t) do
  thr:join();
end

is(uv2, 404, "steady counter value");
cmp_ok(uv1, '<=', uv2, "unreliable value " .. uv1);

