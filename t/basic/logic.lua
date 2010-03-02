-- vim:ts=2:sw=2:et:ft=lua:
require('Test.More');
plan(8);

is(10 or 20, 10, 'should be 10');
is(10 or error(), 10, 'should be 10');
is(nil or 'a', 'a', 'should be a');
is(nil and 10, nil, 'should be nil');
is(false and error(), false, 'should be false');
is(false and nil, false, 'should be false');
is(false or nil, nil, 'should be nil');
is(10 and 20, 20, 'should be 20');

