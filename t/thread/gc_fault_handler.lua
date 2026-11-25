-- vim:ts=2:sw=2:et:ft=lua:
-- What a host's fault handler sees: GC trace threads have an alternate
-- signal stack, and lua_thread_blocks_others() is set exactly while other
-- Lua threads may be waiting on the calling thread.
require('Test.More');

local ok_load, gctest = pcall(require, 'gctest');
if not ok_load or not gctest.probe_during then
  skip_all('needs the gctest module (Linux)');
  return;
end

local trace_threads = gctest.probe_trace_threads();
if #trace_threads == 0 then
  skip_all('no lua-gtrace threads (trace threads or thread names disabled)');
  return;
end

plan(7);

local function all(f)
  for _, t in ipairs(trace_threads) do
    if not f(t) then return false end
  end
  return true;
end

ok(all(function(t) return t.onstack end),
  'trace threads handle signals on an alternate stack');
ok(all(function(t) return t.altstack_size >= 1024 * 1024 end),
  'the alternate stack is at least 1 MiB');
ok(all(function(t) return not t.blocks_others end),
  'an idle trace thread blocks no one');

is(gctest.blocks_others(), false, 'a thread running Lua blocks no one');

-- Lots of objects, so that the work below takes long enough to be probed.
local function make(n)
  local t = {};
  for i = 1, n do t[i] = {} end
  return t;
end

-- The probes are timing based: retry a few times before failing.
local function seen_blocking(f)
  local seen, probes = 0, 0;
  for _ = 1, 5 do
    seen, probes = gctest.probe_during(f);
    if seen > 0 then break end
  end
  return seen > 0, probes;
end

local keep = make(500000);
local seen = seen_blocking(function()
  collectgarbage('globaltrace');
end);
ok(seen, 'the thread running a global trace blocks others');

-- a finished coroutine's objects are handed to the thread that resumed it
-- under the lock that creating Lua threads takes
seen = seen_blocking(function()
  local co = coroutine.create(function()
    local t = make(500000);
    keep.from_co = t;
  end);
  coroutine.resume(co);
end);
ok(seen, 'handing over a finished coroutine blocks others');

is(gctest.blocks_others(), false, 'and blocks no one once done');
