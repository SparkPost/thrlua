-- vim:ts=2:sw=2:et:ft=lua:
require("Test.More");
require("curl");
require("thread");
require("socket");
require("pcre");

plan(68);

local res, err, code;

-- set up a very simple http server for testing purposes.
-- We'll let the kernel assign a port for us
local http_sock = socket.tcp();
res, code, err = http_sock:bind("127.0.0.1");
ok(res, "bound http server");
res, code, err = http_sock:listen();
ok(res, "http server is listening");

-- This table defines the http server functions available
local http_server_functions = {
  GET = {
    ["/hello"] = "<html>hello</html>",
    ["/customHeader"] = function (s, req, hdrs, body)
      s:write("HTTP/1.0 200 OK\r\nConnection: close\r\n\r\n")
      is(hdrs["x-my-header"], "woot", "found woot");
      is(hdrs["x-other"], "thing", "found other thing");
    end,
  },
  TESTREADER = {
    ["/reader"] = function (s, req, hdrs, body)
      s:write("HTTP/1.0 200 OK\r\nConnection: close\r\n\r\n")
      is(body, "therehello", "got correct body payload")
      s:write("all done\r\n")
    end,
  },
}

-- parse http request line and headers into a structured form
function parse_headers(hdrstr)
  local h = {}

  -- logical line continuations -> physical lines
  hdrstr = pcre.replace(hdrstr, "\r\n\\s+", " ")
  local lines = pcre.split(hdrstr, "\r\n")

  -- first line is the request type
  local req = table.remove(lines, 1)
  req = pcre.match(req, "^(\\S+)\\s+(\\S+)\\s+HTTP/1")
  table.remove(req, 0)

  for _, line in ipairs(lines) do
    local m = pcre.match(line, "^([^:]+)\\s*:\\s*(.*)\\s*$")
    if m then
      local k = string.lower(m[1])
      local v = m[2]
      h[k] = v
      -- diag(k .. " => " .. v)
    end
  end

  return req, h
end

function serve_request(s)
  -- got a new client
  local hdrs, req
  local body = nil
  local text = ""
  local bodylen = 0

  while true do
    text = text .. tostring(s:read());
    if not body then
      local spos, epos = string.find(text, "\r\n\r\n", 1, true)

      if spos then
        -- found the end of the headers, start of the body
        req, hdrs = parse_headers(string.sub(text, 1, spos - 1))
        body = string.sub(text, epos + 1)
        text = ""

        if hdrs.expect and hdrs.expect == "100-continue" then
          s:write("HTTP/1.1 100 Continue\r\n")
        end

        if hdrs["content-length"] then
          bodylen = tonumber(hdrs["content-length"])
          if body then
            bodylen = bodylen - #body
          else
            body = ""
          end
        end

        break;
      end
    end
  end

  while bodylen > 0 do
    local d = tostring(s:read(bodylen))
    body = body .. d
    bodylen = bodylen - #d
  end

  if req[2] == "/QUIT" then
    -- shut it down
    s:write("HTTP/1.0 200 OK\r\nConnection: close\r\n\r\nfoo\r\n");
    s:close()
    return false
  end

  -- find the function or static content that we should return

  if http_server_functions[req[1]] and
      http_server_functions[req[1]][req[2]] then
    local f = http_server_functions[req[1]][req[2]]

    if type(f) == "string" then
      s:write("HTTP/1.0 200 OK\r\nConnection: close\r\n\r\n")
      s:write(f)
      s:close()
      return true
    end

    -- otherwise, call it
    f(s, req, hdrs, body)
    s:close()
    return true
  end

  s:write("HTTP/1.0 404 not found\r\nConnection: close\r\n\r\n")
  s:write("VERB: " .. req[1] .. "\r\n")
  s:write("URI: " .. req[2] .. "\r\n")
  s:close()
  return true
end

local http_thread = thread.create(function ()
  while true do
    local s, code, err = http_sock:accept()
    if not s then
      diag("accept failed " .. err)
    else
      local st, res = pcall(serve_request, s)
      if not st then
        diag("Error in server:" .. err)
      else
        if not res then
          -- quit signal
          break
        end
      end
    end
  end
end);
ok(http_thread, "spawned http thread");

local urlbase = "http://" .. http_sock:sockname();

c = curl.new();
res, err, code = c:setopt(curl.OPT_URL, urlbase .. "/hello");
ok(res, "set url")

local payload = '';
function accumulate_payload(h, data)
  payload = payload .. data
  return #data
end
res, err, code = c:setopt(curl.OPT_WRITEFUNCTION, accumulate_payload)
ok(res, "set callback")

res, err, code = c:perform()
ok(res, "executed fetch")
is(c:getinfo(curl.INFO_RESPONSE_CODE), 200, "got a 200 response")
is(payload, "<html>hello</html>", "there's html in there");

-- Want to see what happens when we know that we have no cookies;
-- does getinfo do the right thing with the slist?
is(c:getinfo(curl.INFO_COOKIELIST), nil, "no cookies for you")

-- How about SSL? We should always have some engine available on modern
-- systems, so this exercises the non-empty slist code
local engines = c:getinfo(curl.INFO_SSL_ENGINES);
isnt(engines, {}, "non-empty set of engines from OpenSSL")
diag(table.concat(engines, ", "))

-- test "double" type
cmp_ok(c:getinfo(curl.INFO_TOTAL_TIME), '>', 0,
  "request took a non-zero amount of time")

-- test "string" type
is(c:getinfo(curl.INFO_EFFECTIVE_URL), urlbase .. "/hello",
  "effective URL matches")

-- test error case
res, err, code = c:getinfo(0)
is(res, nil, "getinfo 0 gives nil")
is(err, "unknown CURLINFO type: 0")
is(code, curl.BAD_FUNCTION_ARGUMENT)

res, err, code = c:setopt(curl.OPT_WRITEDATA, { "boom!" });
ok(res, "set data")
res, err, code = c:setopt(curl.OPT_WRITEFUNCTION, function (h, data)
  is(type(h), "table", "got data param")
  is(h[1], "boom!", "it's definitely our table");
	return #data
end)
ok(res, "set callback")
res, err, code = c:perform()
ok(res, "executed fetch")
res, err, code = c:setopt(curl.OPT_WRITEFUNCTION, accumulate_payload)
ok(res, "restored callback")

res, err, code = c:setopt(curl.OPT_VERBOSE, true)
ok(res, "set verbose = true")
res, err, code = c:setopt(curl.OPT_VERBOSE, false)
ok(res, "set verbose = false")
error_like(function ()
  c:setopt(curl.OPT_VERBOSE, 42)
end, "boolean value expected")

ok(c:setopt(curl.OPT_HTTPHEADER, "Bah: humbug"),
  "set headers that we will replace")
error_like(function ()
  c:setopt(curl.OPT_HTTPHEADER)
end, "Invalid number of arguments")
c:setopt(curl.OPT_HTTPHEADER, "X-My-Header: woot", "X-Other: thing")
c:setopt(curl.OPT_URL, urlbase .. "/customHeader")
res, err, code = c:perform()
ok(res, "executed customHeader test")
ok(c:setopt(curl.OPT_HTTPHEADER, nil), "clear custom headers")

-- Check that we can track (almost) all supported data types for callbacks
local dt = {
  nil,
  true,
  false,
  {"table"},
  function () return "func"; end,
  c,
  42,
  "hello"
}
c:setopt(curl.OPT_URL, urlbase .. "/hello")
for _, d in pairs(dt) do
  c:setopt(curl.OPT_WRITEDATA, d)
  c:setopt(curl.OPT_WRITEFUNCTION, function (h, data)
    is(h, d, "data-type matches (" .. type(d) .. ")")
    return #data
  end)
  res, err, code = c:perform()
  ok(res, "executed fine")
end
c:setopt(curl.OPT_WRITEFUNCTION, accumulate_payload)
c:setopt(curl.OPT_WRITEDATA, nil)

-- Try setting a number option
ok(c:setopt(curl.OPT_DNS_CACHE_TIMEOUT, 60), "set DNS timeout")

ok(c:setopt(curl.OPT_PROGRESSFUNCTION,
  function (h, dltotal, dlnow, ultotal, ulnow)
    diag(string.format("progress: dl:%d/%d ul:%d/%d",
      dlnow, dltotal, ulnow, ultotal))
  end), "set prog func")
ok(c:setopt(curl.OPT_NOPROGRESS, false), "enable progress")
ok(c:perform(), "execute with progress");
ok(c:setopt(curl.OPT_PROGRESSFUNCTION, nil), "turn off prog func")
ok(c:setopt(curl.OPT_NOPROGRESS, true), "disable progress")

-- and a reader func
local read_table = { "hello", "there" }
ok(c:setopt(curl.OPT_READFUNCTION, function (h, size)
  local d = table.remove(read_table)
  if d then
    return d
  end
  return nil
end), "set a reader function")
c:setopt(curl.OPT_CUSTOMREQUEST, "TESTREADER")
c:setopt(curl.OPT_VERBOSE, false)
c:setopt(curl.OPT_UPLOAD, true)
c:setopt(curl.OPT_INFILESIZE, 10)
c:setopt(curl.OPT_URL, urlbase .. "/reader")
res, err, code = c:perform()
ok(res, "executed reader test")
if not res then
  diag(err)
end

c:setopt(curl.OPT_VERBOSE, false)
c:setopt(curl.OPT_CUSTOMREQUEST, nil)
c:setopt(curl.OPT_UPLOAD, false)
c:setopt(curl.OPT_INFILESIZE, 0)

-- Get coverage on the data parameter handling for callbacks
local cbopts = {
  curl.OPT_WRITEDATA,
  curl.OPT_READDATA,
  curl.OPT_PROGRESSDATA,
  curl.OPT_HEADERDATA,
  curl.OPT_IOCTLDATA,
  curl.OPT_SEEKDATA
};
for _, opt in pairs(cbopts) do
  ok(c:setopt(opt, true), "stupid coverage test for option value " .. opt)
end

-- we're testing that the bindings are functional in that they don't
-- crash; we trust that curl implements the appropriate semantics so
-- we only need these two simple calls
is(curl.escape("hello there"), "hello%20there", "escape functions");
error_like(function () curl.escape({}) end, "bad argument");
error_like(function () curl.escape(nil) end, "bad argument");
is(curl.unescape("hello%20there"), "hello there", "unescape");
error_like(function () curl.unescape({}) end, "bad argument");
error_like(function () curl.unescape(nil) end, "bad argument");


-- tell server to quit
c:setopt(curl.OPT_URL, urlbase .. "/QUIT");
c:perform();

-- test that the __gc method is safe to call after close
-- triggers
is(c:close(), nil, "called close");
-- call it twice
is(c:close(), nil, "called close again");
c = nil;
-- gc will call it too
ok(collectgarbage('collect'), 'collected')

