-- vim:ts=2:sw=2:et:ft=lua:
require('Test.More');
plan(1);

local limit = 1000000;

local c = thread.condition();
local d = thread.condition();
local dcount = 0;
local q = {};

-- this thread consumes items generated from the main thread
local function consumer(id)
  while true do
    c:acquire()
    local item;
    repeat
      item = table.remove(q, 1)
      if not item then
        c:wait();
      end
    until item;
    c:release();

    d:acquire()
    dcount = dcount + 1
    d:signal()
    d:release()

    if dcount % 1024 == 0 then
      collectgarbage 'globaltrace'
    end


  end
end

diag("starting consumers")
for i = 1, 3 do
  thread.create(consumer, i, false);
end

-- This is the producer

function produce(item)
  c:acquire()
  table.insert(q, item);
  c:signal()
  c:release()
end

diag("producing items")
for i = 1, limit do
  produce(tostring(i))
end
collectgarbage 'collect'

-- Now wait until we get all 100 items processed

d:acquire()
while dcount != limit do
  d:wait()
end
d:release()

is(dcount, limit, "all items produced");

