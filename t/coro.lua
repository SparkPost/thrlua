require("Test.More");
plan(1);


c = coroutine.wrap(function ()
	return 42;
end)

is(c(), 42, "invoke a coro");



