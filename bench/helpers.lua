
pcall(function()
	require 'posix'
end)

if not posix then
	print("faking posix; you may want to try LUA_CPATH='.libs/?.so'")
	posix = {}
	function posix.gettimeofday()
		local t = os.time();
		print("os.time is", t)
		return t, 0;
	end
else
	print "attach now"
--	posix.sleep(10)
end

function difftime(ss, su, es, eu)
  local u = eu - su;
  local s = es - ss;
  if u < 0 then
	s = s - 1
	u = u + 1000000
  end
  return s, u
end

function run(testname, testfunc)
	print("starting", testname)
	local nthreads = os.getenv("THREADS")
	if not nthreads then
		nthreads = 1
	else
		nthreads = tonumber(nthreads)
	end
	if nthreads > 1 and not thread then
		error("this environment does not support the thread interface")
	end

	local ss, su = posix.gettimeofday()
	if nthreads > 1 then
		local t = {}
		for i = 1, nthreads do
			table.insert(t, thread.create(testfunc))
		end
		for i = 1, nthreads do
			t[i]:join()
		end
	else
		testfunc()
	end
	local es, eu = posix.gettimeofday()

	local ds, du = difftime(ss, su, es, eu)
	print(string.format("Runtime %d.%d", ds, du))
end

