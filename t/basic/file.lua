-- vim:ts=2:sw=2:et:ft=lua:
require('tap');
plan(87);

isnt(io.stdin, nil);
is(io.type(io.stdin), 'file');
isnt(io.stdout, nil);
is(io.type(io.stdout), 'file');
isnt(io.stderr, nil);
is(io.type(io.stderr), 'file');

is(io.type('woot'), nil, 'woot is not a file handle');

is(io.stdout:write("# diag via io.stdout\n"), true, 'wrote diag');

o = io.tmpfile();
is(io.type(o), 'file');
is(o:write(123, "\n"), true, 'wrote a number at the start');
is(o:write("one\n"), true, 'wrote line');
is(o:write("two\n"), true, 'wrote line');
is(o:write("three\n"), true, 'wrote line');
is(o:flush(), true, 'flushed');
is(o:seek(), 18, 'at end of file');

res, err = pcall(function ()
  o:seek(5);
end)
is(res, false, 'cant call seek with a single numeric arg');
like(err, 'bad argument');

is(o:seek('set', 5), 5, 'initiate seek to 5');
is(o:seek(), 5, 'actually at 5');
is(o:read('*a'), "ne\ntwo\nthree\n", 'data is correct for position 5');

is(o:seek('set'), 0, 'set defaults to start');
is(o:read('*n'), 123, 'read a number out');
is(o:seek(), 3, 'now at position 3');
is(o:seek('cur', -4), nil, 'cant wind back 4 places');
is(o:seek('cur', -3), 0, 'wound back 3 places');
is(o:read(), "123", 'read the first line');
is(o:read(3), "one", 'read three characters');
is(o:seek(), 7, 'at position 7');
is(o:read(), '', 'read newline at the end of the second line');
is(o:seek('end'), 18, 'jumped to end');
is(o:seek('end', -6), 12, 'jumped to 12; 6 from end');
is(o:seek('set'), 0, 'rewind');
l = {};
for line in o:lines() do
  table.insert(l, line);
end
is_deeply(l, {'123', 'one', 'two', 'three' }, "read data via iterator");

-- relatively lame tests for buffering
is(o:setvbuf('no'), true, 'turn off buffering');
is(o:seek('end'), 18, 'move to end');
is(o:write("four\n"), true, 'added a line');
is(o:setvbuf('full', 8), true, 'full buffering with tiny buffer');
is(o:write("five\nsix\nsev"), true, 'wrote some more lines');
is(o:setvbuf('line'), true, 'now with line buffering');
is(o:write("en\neight\nnine\nten\n"), true, "added more lines");

l = {};
is(o:seek('set'), 0, 'rewind');
for line in o:lines() do
  table.insert(l, line);
end
is_deeply(l, {'123', 'one', 'two', 'three',
  'four', 'five', 'six', 'seven', 'eight', 'nine', 'ten'},
  "read data via iterator");

is(io.output(), io.stdout, 'default output is stdout');
is(io.write("# diag via default output\n"), true, 'wrote a diag');
is(io.output(o), o, 'default output is now the o file');
is(io.write("eleven\n"), true, 'wrote eleventh line via write');
l = {};
is(o:seek('set'), 0, 'rewind');
for line in o:lines() do
  table.insert(l, line);
end
is_deeply(l, {'123', 'one', 'two', 'three',
  'four', 'five', 'six', 'seven', 'eight', 'nine', 'ten', 'eleven'},
  "read data via iterator");

is(o:seek('set'), 0, 'rewind');
is(io.input(), io.stdin, 'default input is stdin');
is(io.input(o), o, 'changed default input');
is(io.read(), '123', 'read first line from default input');

l = {};
is(o:seek('set'), 0, 'rewind');
for line in io.lines() do
  table.insert(l, line);
end
is_deeply(l, {'123', 'one', 'two', 'three',
  'four', 'five', 'six', 'seven', 'eight', 'nine', 'ten', 'eleven'},
  "read data via iterator");
is(o:write("no newline at end"), true, "wrote no newline ending");
is(o:seek('end', -17), 60, 'wind back');
is(o:read('*l'), 'no newline at end', 'got final line');

res, err = pcall(function()
  o:read('foo')
end);
is(res, false, 'expect error');
like(err, 'invalid option');
res, err = pcall(function()
  o:read('*x')
end);
is(res, false, 'expect error');
like(err, 'invalid format');

is(o:close(), true, 'closed file');
is(io.type(o), 'closed file', 'handle is closed');
res, err = pcall(function()
  o:seek('set');
end);
is(res, false, 'expect error');
like(err, 'attempt to use a closed file');

o = io.tmpfile();
o:write("foo\nbar\n");
o:seek('set');
res, err = pcall(function()
  for line in o:lines() do
    o:close();
  end
end);
is(res, false, 'expect error');
like(err, 'file is already closed');

o = io.tmpfile();
io.output(o);
is(io.flush(), true, 'flushed default output');
like(tostring(o), 'file');
is(io.close(), true, 'closed default output');
is(io.type(o), 'closed file', 'io.close really did close o');
is(tostring(o), 'file (closed)');

o, err = io.open('this file should not exist');
is(o, nil, 'failed to open');
like(err, 'No such file or directory', err);

o = io.popen('/bin/ls /bin/ls');
is(io.type(o), 'file');
like(o:read('*a'), '/bin/ls', 'read the ls output');
is(o:close(), true, 'closed pipe');

name = os.tmpname();
like(name, "tmp", "made temporary name " .. name);
o = io.popen('/bin/cat >' .. name, 'w');
is(io.type(o), 'file', 'pipe opened');
ok(o:write("write via pipe\n"), 'wrote to pipe');
is(o:close(), true, 'closed pipe');

o = io.open(name);
is(o:read('*a'), "write via pipe\n", "saw the data we wrote via cat");

for line in io.lines(name) do
  is(line, "write via pipe", "read from temp file via io.lines");
end

o = io.tmpfile();
ok(o, "got tmp file");
ok(o:write(string.rep('a', 1024)), "write some numbers");
ok(o:seek('set'), 'rewind');
data = {o:read(10,10,10,10,10)};
is_deeply(data, {
    'aaaaaaaaaa',
    'aaaaaaaaaa',
    'aaaaaaaaaa',
    'aaaaaaaaaa',
    'aaaaaaaaaa'
}, '5 chunks of 10 a');

