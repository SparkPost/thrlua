-- vim:ts=2:sw=2:et:ft=lua:
require('Test.More');
plan(4);

local counter = 0;

function dostuff(t)
  for i = 0, 10 do
    counter = counter + 1;
    thread.sleep(1);
  end
  is(counter, 11, "thread sees counter value 11");
end

local t = thread.create(function (t)
  diag(t);
  is(type(t), "userdata", "thread sees thread userdata");
  dostuff("other");
end);

t:join();
is(type(t), "userdata", "got a thread userdata");
is(counter, 11, "main sees counter value 11");

