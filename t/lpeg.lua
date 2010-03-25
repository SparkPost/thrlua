#!/usr/bin/env lua5.1

-- $Id: test.lua,v 1.70 2008/10/09 20:16:45 roberto Exp $

require 'Test.More';
plan(5999);

local m = require"lpeg"

any = m.P(1)
space = m.S" \t\n"^0

local function checkeq (x, y, p)
if p then diag(x,y) end
  if type(x) ~= "table" then ok(x == y)
  else
    for k,v in pairs(x) do checkeq(v, y[k], p) end
    for k,v in pairs(y) do checkeq(v, x[k], p) end
  end
end


mt = getmetatable(m.P(1))


local allchar = {}
for i=0,255 do allchar[i + 1] = i end
allchar = string.char(unpack(allchar))
ok(#allchar == 256)

local function cs2str (c)
  return m.match(m.Cs((c + m.P(1)/"")^0), allchar)
end

local function eqcharset (c1, c2)
  ok(cs2str(c1) == cs2str(c2))
end


ok(type(m.version()) == "string")
diag("version " .. m.version())
ok(m.type("alo") ~= "pattern")
ok(m.type(io.input) ~= "pattern")
ok(m.type(m.P"alo") == "pattern")

-- tests for some basic optimizations
ok(m.match(m.P(false) + "a", "a") == 2)
ok(m.match(m.P(true) + "a", "a") == 1)
ok(m.match("a" + m.P(false), "b") == nil)
ok(m.match("a" + m.P(true), "b") == 1)

ok(m.match(m.P(false) * "a", "a") == nil)
ok(m.match(m.P(true) * "a", "a") == 2)
ok(m.match("a" * m.P(false), "a") == nil)
ok(m.match("a" * m.P(true), "a") == 2)

ok(m.match(#m.P(false) * "a", "a") == nil)
ok(m.match(#m.P(true) * "a", "a") == 2)
ok(m.match("a" * #m.P(false), "a") == nil)
ok(m.match("a" * #m.P(true), "a") == 2)


-- tests for locale
do
  ok(m.locale(m) == m)
  local x = m.locale()
  for n,v in pairs(x) do
    ok(type(n) == "string")
    eqcharset(v, m[n])
  end
end



ok(m.match(3, "aaaa"))
ok(m.match(4, "aaaa"))
ok(not m.match(5, "aaaa"))
ok(m.match(-3, "aa"))
ok(not m.match(-3, "aaa"))
ok(not m.match(-3, "aaaa"))
ok(not m.match(-4, "aaaa"))
ok(m.P(-5):match"aaaa")

ok(m.match("a", "alo") == 2)
ok(m.match("al", "alo") == 3)
ok(not m.match("alu", "alo"))
ok(m.match(true, "") == 1)

digit = m.S"0123456789"
upper = m.S"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
lower = m.S"abcdefghijklmnopqrstuvwxyz"
letter = m.S"" + upper + lower
alpha = letter + digit + m.R()

eqcharset(m.S"", m.P(false))
eqcharset(upper, m.R("AZ"))
eqcharset(lower, m.R("az"))
eqcharset(upper + lower, m.R("AZ", "az"))
eqcharset(upper + lower, m.R("AZ", "cz", "aa", "bb", "90"))
eqcharset(digit, m.S"01234567" + "8" + "9")
eqcharset(upper, letter - lower)
eqcharset(m.S(""), m.R())
ok(cs2str(m.S("")) == "")

eqcharset(m.S"\0", "\0")
eqcharset(m.S"\1\0\2", m.R"\0\2")
eqcharset(m.S"\1\0\2", m.R"\1\2" + "\0")
eqcharset(m.S"\1\0\2" - "\0", m.R"\1\2")

word = alpha^1 * (1 - alpha)^0

ok((word^0 * -1):match"alo alo")
ok(m.match(word^1 * -1, "alo alo"))
ok(m.match(word^2 * -1, "alo alo"))
ok(not m.match(word^3 * -1, "alo alo"))

ok(not m.match(word^-1 * -1, "alo alo"))
ok(m.match(word^-2 * -1, "alo alo"))
ok(m.match(word^-3 * -1, "alo alo"))

eos = m.P(-1)

ok(m.match(digit^0 * letter * digit * eos, "1298a1"))
ok(not m.match(digit^0 * letter * eos, "1257a1"))

b = {
  [1] = "(" * (((1 - m.S"()") + #m.P"(" * m.V(1))^0) * ")"
}

ok(m.match(b, "(al())()"))
ok(not m.match(b * eos, "(al())()"))
ok(m.match(b * eos, "((al())()(é))"))
ok(not m.match(b, "(al()()"))

ok(not m.match(letter^1 - "for", "foreach"))
ok(m.match(letter^1 - ("for" * eos), "foreach"))
ok(not m.match(letter^1 - ("for" * eos), "for"))

function basiclookfor (p)
  return m.P {
    [1] = p + (1 * m.V(1))
  }
end

function caplookfor (p)
  return basiclookfor(p:C())
end

ok(m.match(caplookfor(letter^1), "   4achou123...") == "achou")
a = {m.match(caplookfor(letter^1)^0, " two words, one more  ")}
checkeq(a, {"two", "words", "one", "more"})

ok(m.match( basiclookfor((#m.P(b) * 1) * m.Cp()), "  (  (a)") == 7)

a = {m.match(m.C(digit^1 * m.Cc"d") + m.C(letter^1 * m.Cc"l"), "123")}
checkeq(a, {"123", "d"})

a = {m.match(m.C(digit^1) * "d" * -1 + m.C(letter^1 * m.Cc"l"), "123d")}
checkeq(a, {"123"})

a = {m.match(m.C(digit^1 * m.Cc"d") + m.C(letter^1 * m.Cc"l"), "abcd")}
checkeq(a, {"abcd", "l"})

a = {m.match(m.Cc(10,20,30) * 'a' * m.Cp(), 'aaa')}
checkeq(a, {10,20,30,2})
a = {m.match(m.Cp() * m.Cc(10,20,30) * 'a' * m.Cp(), 'aaa')}
checkeq(a, {1,10,20,30,2})
a = m.match(m.Ct(m.Cp() * m.Cc(10,20,30) * 'a' * m.Cp()), 'aaa')
checkeq(a, {1,10,20,30,2})
a = m.match(m.Ct(m.Cp() * m.Cc(7,8) * m.Cc(10,20,30) * 'a' * m.Cp()), 'aaa')
checkeq(a, {1,7,8,10,20,30,2})
a = {m.match(m.Cc() * m.Cc() * m.Cc(1) * m.Cc(2,3,4) * m.Cc() * 'a', 'aaa')}
checkeq(a, {1,2,3,4})

a = {m.match(m.Cp() * letter^1 * m.Cp(), "abcd")}
checkeq(a, {1, 5})


t = {m.match({[1] = m.C(m.C(1) * m.V(1) + -1)}, "abc")}
checkeq(t, {"abc", "a", "bc", "b", "c", "c", ""})


-- test for small capture boundary
for i = 250,260 do
  ok(#m.match(m.C(i), string.rep('a', i)) == i)
  ok(#m.match(m.C(m.C(i)), string.rep('a', i)) == i)
end


-- tests for any*n
for n = 1, 550 do
  local x_1 = string.rep('x', n - 1)
  local x = x_1 .. 'a'
  ok(not m.P(n):match(x_1))
  ok(m.P(n):match(x) == n + 1)
  ok(n < 4 or m.match(m.P(n) + "xxx", x_1) == 4)
  ok(m.C(n):match(x) == x)
  ok(m.C(m.C(n)):match(x) == x)
  ok(m.P(-n):match(x_1) == 1)
  ok(not m.P(-n):match(x))
  ok(n < 13 or m.match(m.Cc(20) * ((n - 13) * m.P(10)) * 3, x) == 20)
  local n3 = math.floor(n/3)
  ok(m.match(n3 * m.Cp() * n3 * n3, x) == n3 + 1)
end

ok(m.P(0):match("x") == 1)
ok(m.P(0):match("") == 1)
ok(m.C(0):match("x") == "")
ok(m.match(m.Cc(0) * m.P(10) + m.Cc(1) * "xuxu", "xuxu") == 1)
ok(m.match(m.Cc(0) * m.P(10) + m.Cc(1) * "xuxu", "xuxuxuxuxu") == 0)
ok(m.match(m.C(m.P(2)^1), "abcde") == "abcd")
p = m.Cc(0) * 1 + m.Cc(1) * 2 + m.Cc(2) * 3 + m.Cc(3) * 4


-- test for alternation optimization
ok(m.match(m.P"a"^1 + "ab" + m.P"x"^0, "ab") == 2)
ok(m.match((m.P"a"^1 + "ab" + m.P"x"^0 * 1)^0, "ab") == 3)
ok(m.match(m.P"ab" + "cd" + "" + "cy" + "ak", "98") == 1)
ok(m.match(m.P"ab" + "cd" + "ax" + "cy", "ax") == 3)
ok(m.match("a" * m.P"b"^0 * "c"  + "cd" + "ax" + "cy", "ax") == 3)
ok(m.match((m.P"ab" + "cd" + "ax" + "cy")^0, "ax") == 3)
ok(m.match(m.P(1) * "x" + m.S"" * "xu" + "ay", "ay") == 3)
ok(m.match(m.P"abc" + "cde" + "aka", "aka") == 4)
ok(m.match(m.S"abc" * "x" + "cde" + "aka", "ax") == 3)
ok(m.match(m.S"abc" * "x" + "cde" + "aka", "aka") == 4)
ok(m.match(m.S"abc" * "x" + "cde" + "aka", "cde") == 4)
ok(m.match(m.S"abc" * "x" + "ide" + m.S"ab" * "ka", "aka") == 4)
ok(m.match("ab" + m.S"abc" * m.P"y"^0 * "x" + "cde" + "aka", "ax") == 3)
ok(m.match("ab" + m.S"abc" * m.P"y"^0 * "x" + "cde" + "aka", "aka") == 4)
ok(m.match("ab" + m.S"abc" * m.P"y"^0 * "x" + "cde" + "aka", "cde") == 4)
ok(m.match("ab" + m.S"abc" * m.P"y"^0 * "x" + "ide" + m.S"ab" * "ka", "aka") == 4)
ok(m.match("ab" + m.S"abc" * m.P"y"^0 * "x" + "ide" + m.S"ab" * "ka", "ax") == 3)
ok(m.match(m.P(1) * "x" + "cde" + m.S"ab" * "ka", "aka") == 4)
ok(m.match(m.P(1) * "x" + "cde" + m.P(1) * "ka", "aka") == 4)
ok(m.match(m.P(1) * "x" + "cde" + m.P(1) * "ka", "cde") == 4)
ok(m.match(m.P"eb" + "cd" + m.P"e"^0 + "x", "ee") == 3)
ok(m.match(m.P"ab" + "cd" + m.P"e"^0 + "x", "abcd") == 3)
ok(m.match(m.P"ab" + "cd" + m.P"e"^0 + "x", "eeex") == 4)
ok(m.match(m.P"ab" + "cd" + m.P"e"^0 + "x", "cd") == 3)
ok(m.match(m.P"ab" + "cd" + m.P"e"^0 + "x", "x") == 1)
ok(m.match(m.P"ab" + "cd" + m.P"e"^0 + "x" + "", "zee") == 1)
ok(m.match(m.P"ab" + "cd" + m.P"e"^1 + "x", "abcd") == 3)
ok(m.match(m.P"ab" + "cd" + m.P"e"^1 + "x", "eeex") == 4)
ok(m.match(m.P"ab" + "cd" + m.P"e"^1 + "x", "cd") == 3)
ok(m.match(m.P"ab" + "cd" + m.P"e"^1 + "x", "x") == 2)
ok(m.match(m.P"ab" + "cd" + m.P"e"^1 + "x" + "", "zee") == 1)

pi = "3.14159 26535 89793 23846 26433 83279 50288 41971 69399 37510"
ok(m.match(m.Cs((m.P"1" / "a" + m.P"5" / "b" + m.P"9" / "c" + 1)^0), pi) ==
  m.match(m.Cs((m.P(1) / {["1"] = "a", ["5"] = "b", ["9"] = "c"})^0), pi))

-- tests for capture optimizations
ok(m.match((m.P(3) +  4 * m.Cp()) * "a", "abca") == 5)
t = {m.match(((m.P"a" + m.Cp()) * m.P"x")^0, "axxaxx")}
checkeq(t, {3, 6})

-- test for table captures
t = m.match(m.Ct(letter^1), "alo")
checkeq(t, {})

t, n = m.match(m.Ct(m.C(letter)^1) * m.Cc"t", "alo")
ok(n == "t" and table.concat(t) == "alo")

t = m.match(m.Ct(m.C(m.C(letter)^1)), "alo")
ok(table.concat(t, ";") == "alo;a;l;o")

t = m.match(m.Ct(m.C(m.C(letter)^1)), "alo")
ok(table.concat(t, ";") == "alo;a;l;o")

t = m.match(m.Ct(m.Ct((m.Cp() * letter * m.Cp())^1)), "alo")
ok(table.concat(t[1], ";") == "1;2;2;3;3;4")

t = m.match(m.Ct(m.C(m.C(1) * 1 * m.C(1))), "alo")
checkeq(t, {"alo", "a", "o"})


-- tests for groups
p = m.Cg(1)   -- no capture
ok(p:match('x') == 'x')
p = m.Cg(m.P(true)/function () end * 1)   -- no value
ok(p:match('x') == 'x')
p = m.Cg(m.Cg(m.Cg(m.C(1))))
ok(p:match('x') == 'x')
p = m.Cg(m.Cg(m.Cg(m.C(1))^0) * m.Cg(m.Cc(1) * m.Cc(2)))
t = {p:match'abc'}
checkeq(t, {'a', 'b', 'c', 1, 2})

-- test for non-pattern as arguments to pattern functions

p = { ('a' * m.V(1))^-1 } * m.P'b' * { 'a' * m.V(2); m.V(1)^-1 }
ok(m.match(p, "aaabaac") == 7)

-- a large table capture
t = m.match(m.Ct(m.C('a')^0), string.rep("a", 10000))
ok(#t == 10000 and t[1] == 'a' and t[#t] == 'a')


-- test for errors
local function checkerr (msg, ...)
  ok(m.match({ m.P(msg) + 1 * m.V(1) }, select(2, pcall(...))))
end

checkerr("rule '1' is left recursive", m.match, { m.V(1) * 'a' }, "a")
checkerr("stack overflow", m.match, m.C('a')^0, string.rep("a", 50000))
checkerr("rule '1' outside a grammar", m.match, m.V(1), "")
checkerr("rule 'hiii' outside a grammar", m.match, m.V('hiii'), "")
checkerr("rule 'hiii' is not defined", m.match, { m.V('hiii') }, "")

-- test for non-pattern as arguments to pattern functions

p = { ('a' * m.V(1))^-1 } * m.P'b' * { 'a' * m.V(2); m.V(1)^-1 }
ok(m.match(p, "aaabaac") == 7)

-- a large table capture
t = m.match(m.Ct(m.C('a')^0), string.rep("a", 10000))
ok(#t == 10000 and t[1] == 'a' and t[#t] == 'a')


-- test for errors
local function checkerr (msg, ...)
  ok(m.match({ m.P(msg) + 1 * m.V(1) }, select(2, pcall(...))))
end

checkerr("rule '1' is left recursive", m.match, { m.V(1) * 'a' }, "a")
checkerr("stack overflow", m.match, m.C('a')^0, string.rep("a", 50000))
checkerr("rule '1' outside a grammar", m.match, m.V(1), "")
checkerr("rule 'hiii' outside a grammar", m.match, m.V('hiii'), "")
checkerr("rule 'hiii' is not defined", m.match, { m.V('hiii') }, "")
checkerr("rule <a table> is not defined", m.match, { m.V({}) }, "")


local V = m.V

local Space = m.S(" \n\t")^0
local Number = m.C(m.R("09")^1) * Space
local FactorOp = m.C(m.S("+-")) * Space
local TermOp = m.C(m.S("*/")) * Space
local Open = "(" * Space
local Close = ")" * Space


local function f_factor (v1, op, v2, d)
  ok(d == nil)
  if op == "+" then return v1 + v2
  else return v1 - v2
  end
end


local function f_term (v1, op, v2, d)
  ok(d == nil)
  if op == "*" then return v1 * v2
  else return v1 / v2
  end
end

G = m.P{ "Exp",
  Exp = m.Cf(V"Factor" * m.Cg(FactorOp * V"Factor")^0, f_factor);
  Factor = m.Cf(V"Term" * m.Cg(TermOp * V"Term")^0, f_term);
  Term = Number / tonumber  +  Open * V"Exp" * Close;
}

G = Space * G * -1

for _, s in ipairs{" 3 + 5*9 / (1+1) ", "3+4/2", "3+3-3- 9*2+3*9/1-  8"} do
  ok(m.match(G, s) == loadstring("return "..s)())
end


-- test for grammars (errors deep in calling non-terminals)
g = m.P{
  [1] = m.V(2) + "a",
  [2] = "a" * m.V(3) * "x",
  [3] = "b" * m.V(3) + "c"
}

ok(m.match(g, "abbbcx") == 7)
ok(m.match(g, "abbbbx") == 2)


-- tests for \0
ok(m.match(m.R("\0\1")^1, "\0\1\0") == 4)
ok(m.match(m.S("\0\1ab")^1, "\0\1\0a") == 5)
ok(m.match(m.P(1)^3, "\0\1\0a") == 5)
ok(not m.match(-4, "\0\1\0a"))
ok(m.match("\0\1\0a", "\0\1\0a") == 5)
ok(m.match("\0\0\0", "\0\0\0") == 4)
ok(not m.match("\0\0\0", "\0\0"))


-- tests for predicates
ok(not m.match(-m.P("a") * 2, "alo"))
ok(m.match(- -m.P("a") * 2, "alo") == 3)
ok(m.match(#m.P("a") * 2, "alo") == 3)
ok(m.match(##m.P("a") * 2, "alo") == 3)
ok(not m.match(##m.P("c") * 2, "alo"))
ok(m.match(m.Cs((##m.P("a") * 1 + m.P(1)/".")^0), "aloal") == "a..a.")
ok(m.match(m.Cs((#((#m.P"a")/"") * 1 + m.P(1)/".")^0), "aloal") == "a..a.")
ok(m.match(m.Cs((- -m.P("a") * 1 + m.P(1)/".")^0), "aloal") == "a..a.")
ok(m.match(m.Cs((-((-m.P"a")/"") * 1 + m.P(1)/".")^0), "aloal") == "a..a.")



-- tests for Tail Calls

-- create a grammar for a simple DFA for even number of 0s and 1s
-- finished in '$':
--
--  ->1 <---0---> 2
--    ^           ^
--    |           |
--    1           1
--    |           |
--    V           V
--    3 <---0---> 4
--
-- this grammar should keep no backtracking information

p = m.P{
  [1] = '0' * m.V(2) + '1' * m.V(3) + '$',
  [2] = '0' * m.V(1) + '1' * m.V(4),
  [3] = '0' * m.V(4) + '1' * m.V(1),
  [4] = '0' * m.V(3) + '1' * m.V(2),
}

ok(p:match(string.rep("00", 10000) .. "$"))
ok(p:match(string.rep("01", 10000) .. "$"))
ok(p:match(string.rep("011", 10000) .. "$"))
ok(not p:match(string.rep("011", 10001) .. "$"))



-- tests for optional start position
ok(m.match("a", "abc", 1))
ok(m.match("b", "abc", 2))
ok(m.match("c", "abc", 3))
ok(not m.match(1, "abc", 4))
ok(m.match("a", "abc", -3))
ok(m.match("b", "abc", -2))
ok(m.match("c", "abc", -1))
ok(m.match("abc", "abc", -4))   -- truncate to position 1

ok(m.match("", "abc", 10))   -- empty string is everywhere!
ok(m.match("", "", 10))
ok(not m.match(1, "", 1))
ok(not m.match(1, "", -1))
ok(not m.match(1, "", 0))


-- basic tests for external C function

ok(m.match(m.span("abcd"), "abbbacebb") == 7)
ok(m.match(m.span("abcd"), "0abbbacebb") == 1)
ok(m.match(m.span("abcd"), "") == 1)


-- tests for argument captures
ok(not pcall(m.Carg, 0))
ok(not pcall(m.Carg, -1))
ok(not pcall(m.Carg, 2^18))
ok(not pcall(m.match, m.Carg(1), 'a', 1))
ok(m.match(m.Carg(1), 'a', 1, print) == print)
x = {m.match(m.Carg(1) * m.Carg(2), '', 1, 10, 20)}
checkeq(x, {10, 20})

ok(m.match(m.Cmt(m.Cg(m.Carg(3), "a") *
                     m.Cmt(m.Cb("a"), function (s,i,x)
                                        ok(s == "a" and i == 1);
                                        return i, x+1
                                      end) *
                     m.Carg(2), function (s,i,a,b,c)
                                  ok(s == "a" and i == 1 and c == nil);
				  return i, 2*a + 3*b
                                end) * "a",
               "a", 1, false, 100, 1000) == 2*1001 + 3*100)


-- tests for Lua functions

t = {}
s = ""
p = function (s1, i) ok(s == s1); t[#t + 1] = i; return nil end
s = "hi, this is a test"
ok(m.match(((p - m.P(-1)) + 2)^0, s) == string.len(s) + 1)
ok(#t == string.len(s)/2 and t[1] == 1 and t[2] == 3)

ok(not m.match(p, s))

p = mt.__add(function (s, i) return i end, function (s, i) return nil end)
ok(m.match(p, "alo"))

p = mt.__mul(function (s, i) return i end, function (s, i) return nil end)
ok(not m.match(p, "alo"))


t = {}
p = function (s1, i) ok(s == s1); t[#t + 1] = i; return i end
s = "hi, this is a test"
ok(m.match((m.P(1) * p)^0, s) == string.len(s) + 1)
ok(#t == string.len(s) and t[1] == 2 and t[2] == 3)

t = {}
p = m.P(function (s1, i) ok(s == s1); t[#t + 1] = i;
                         return i <= s1:len() and i + 1 end)
s = "hi, this is a test"
ok(m.match(p^0, s) == string.len(s) + 1)
ok(#t == string.len(s) + 1 and t[1] == 1 and t[2] == 2)

p = function (s1, i) return m.match(m.P"a"^1, s1, i) end
ok(m.match(p, "aaaa") == 5)
ok(m.match(p, "abaa") == 2)
ok(not m.match(p, "baaa"))

ok(not pcall(m.match, function () return 2^20 end, s))
ok(not pcall(m.match, function () return 0 end, s))
ok(not pcall(m.match, function (s, i) return i - 1 end, s))
ok(not pcall(m.match, m.P(1)^0 * function (_, i) return i - 1 end, s))
ok(m.match(m.P(1)^0 * function (_, i) return i end * -1, s))
ok(not pcall(m.match, m.P(1)^0 * function (_, i) return i + 1 end, s))
ok(m.match(m.P(function (s, i) return s:len() + 1 end) * -1, s))
ok(not pcall(m.match, m.P(function (s, i) return s:len() + 2 end) * -1, s))
ok(not m.match(m.P(function (s, i) return s:len() end) * -1, s))
ok(m.match(m.P(1)^0 * function (_, i) return true end, s) ==
       string.len(s) + 1)
for i = 1, string.len(s) + 1 do
  ok(m.match(function (_, _) return i end, s) == i)
end

p = (m.P(function (s, i) return i%2 == 0 and i + 1 end)
  +  m.P(function (s, i) return i%2 ~= 0 and i + 2 <= s:len() and i + 3 end))^0
  * -1
ok(p:match(string.rep('a', 14000)))

-- tests for Function Replacements
f = function (a, ...) if a ~= "x" then return {a, ...} end end

t = m.match(m.C(1)^0/f, "abc")
checkeq(t, {"a", "b", "c"})

t = m.match(m.C(1)^0/f/f, "abc")
checkeq(t, {{"a", "b", "c"}})

t = m.match(m.P(1)^0/f/f, "abc")   -- no capture
checkeq(t, {{"abc"}})

t = m.match((m.P(1)^0/f * m.Cp())/f, "abc")
checkeq(t, {{"abc"}, 4})

t = m.match((m.C(1)^0/f * m.Cp())/f, "abc")
checkeq(t, {{"a", "b", "c"}, 4})

t = m.match((m.C(1)^0/f * m.Cp())/f, "xbc")
checkeq(t, {4})

t = m.match(m.C(m.C(1)^0)/f, "abc")
checkeq(t, {"abc", "a", "b", "c"})

g = function (...) return 1, ... end
t = {m.match(m.C(1)^0/g/g, "abc")}
checkeq(t, {1, 1, "a", "b", "c"})

t = {m.match(m.Cc(nil,nil,4) * m.Cc(nil,3) * m.Cc(nil, nil) / g / g, "")}
t1 = {1,1,nil,nil,4,nil,3,nil,nil}
for i=1,10 do ok(t[i] == t1[i]) end

t = {m.match((m.C(1) / function (x) return x, x.."x" end)^0, "abc")}
checkeq(t, {"a", "ax", "b", "bx", "c", "cx"})

t = m.match(m.Ct((m.C(1) / function (x,y) return y, x end * m.Cc(1))^0), "abc")
checkeq(t, {nil, "a", 1, nil, "b", 1, nil, "c", 1})

-- tests for Query Replacements

ok(m.match(m.C(m.C(1)^0)/{abc = 10}, "abc") == 10)
ok(m.match(m.C(1)^0/{a = 10}, "abc") == 10)
ok(m.match(m.S("ba")^0/{ab = 40}, "abc") == 40)
t = m.match(m.Ct((m.S("ba")/{a = 40})^0), "abc")
checkeq(t, {40})

ok(m.match(m.Cs((m.C(1)/{a=".", d=".."})^0), "abcdde") == ".bc....e")
ok(m.match(m.Cs((m.C(1)/{f="."})^0), "abcdde") == "abcdde")
ok(m.match(m.Cs((m.C(1)/{d="."})^0), "abcdde") == "abc..e")
ok(m.match(m.Cs((m.C(1)/{e="."})^0), "abcdde") == "abcdd.")
ok(m.match(m.Cs((m.C(1)/{e=".", f="+"})^0), "eefef") == "..+.+")
ok(m.match(m.Cs((m.C(1))^0), "abcdde") == "abcdde")
ok(m.match(m.Cs(m.C(m.C(1)^0)), "abcdde") == "abcdde")
ok(m.match(1 * m.Cs(m.P(1)^0), "abcdde") == "bcdde")
ok(m.match(m.Cs((m.C('0')/'x' + 1)^0), "abcdde") == "abcdde")
ok(m.match(m.Cs((m.C('0')/'x' + 1)^0), "0ab0b0") == "xabxbx")
ok(m.match(m.Cs((m.C('0')/'x' + m.P(1)/{b=3})^0), "b0a0b") == "3xax3")
ok(m.match(m.P(1)/'%0%0'/{aa = -3} * 'x', 'ax') == -3)
ok(m.match(m.C(1)/'%0%1'/{aa = 'z'}/{z = -3} * 'x', 'ax') == -3)

ok(m.match(m.Cs(m.Cc(0) * (m.P(1)/"")), "4321") == "0")

ok(m.match(m.Cs((m.P(1) / "%0")^0), "abcd") == "abcd")
ok(m.match(m.Cs((m.P(1) / "%0.%0")^0), "abcd") == "a.ab.bc.cd.d")
ok(m.match(m.Cs((m.P("a") / "%0.%0" + 1)^0), "abcad") == "a.abca.ad")
ok(m.match(m.C("a") / "%1%%%0", "a") == "a%a")
ok(m.match(m.Cs((m.P(1) / ".xx")^0), "abcd") == ".xx.xx.xx.xx")
ok(m.match(m.Cp() * m.P(3) * m.Cp()/"%2%1%1 - %0 ", "abcde") ==
   "411 - abc ")

ok(pcall(m.match, m.P(1)/"%0", "abc"))
ok(not pcall(m.match, m.P(1)/"%1", "abc"))   -- out of range
ok(not pcall(m.match, m.P(1)/"%9", "abc"))   -- out of range

p = m.C(1)
p = p * p; p = p * p; p = p * p * m.C(1) / "%9 - %1"
ok(p:match("1234567890") == "9 - 1")

ok(m.match(m.Cc(print), "") == print)

-- too many captures (just ignore extra ones)
p = m.C(1)^0 / "%2-%9-%0-%9"
ok(p:match"01234567890123456789" == "1-8-01234567890123456789-8")
s = string.rep("12345678901234567890", 20)
ok(m.match(m.C(1)^0 / "%9-%1-%0-%3", s) == "9-1-" .. s .. "-3")

-- string captures with non-string subcaptures
p = m.Cc('alo') * m.C(1) / "%1 - %2 - %1"
ok(p:match'x' == 'alo - x - alo')

ok(not pcall(m.match, m.Cc(true) / "%1", "a"))

-- long strings for string capture
l = 10000
s = string.rep('a', l) .. string.rep('b', l) .. string.rep('c', l)

p = (m.C(m.P'a'^1) * m.C(m.P'b'^1) * m.C(m.P'c'^1)) / '%3%2%1'

ok(p:match(s) == string.rep('c', l) ..
                     string.rep('b', l) .. 
                     string.rep('a', l))

-- accumulator capture
function f (x) return x + 1 end
ok(m.match(m.Cf(m.Cc(0) * m.C(1)^0, f), "alo alo") == 7)

t = {m.match(m.Cf(m.Cc(1,2,3), error), "")}
checkeq(t, {1})
p = m.Cf(m.Ct(true) * m.Cg(m.C(m.R"az"^1) * "=" * m.C(m.R"az"^1) * ";")^0,
         rawset)
t = p:match("a=b;c=du;xux=yuy;")
checkeq(t, {a="b", c="du", xux="yuy"})
 

-- tests for loop checker

local function haveloop (p)
  ok(not pcall(function (p) return p^0 end, m.P(p)))
end

haveloop(m.P("x")^-4)
ok(m.match(((m.P(0) + 1) * m.S"al")^0, "alo") == 3)
ok(m.match((("x" + #m.P(1))^-4 * m.S"al")^0, "alo") == 3)
haveloop("")
haveloop(m.P("x")^0)
haveloop(m.P("x")^-1)
haveloop(m.P("x") + 1 + 2 + m.P("a")^-1)
haveloop(-m.P("ab"))
haveloop(- -m.P("ab"))
haveloop(# #(m.P("ab") + "xy"))
haveloop(- #m.P("ab")^0)
haveloop(# -m.P("ab")^1)
haveloop(#m.V(3))
haveloop(m.V(3) + m.V(1) + m.P('a')^-1)
haveloop({[1] = m.V(2) * m.V(3), [2] = m.V(3), [3] = m.P(0)})
ok(m.match(m.P{[1] = m.V(2) * m.V(3), [2] = m.V(3), [3] = m.P(1)}^0, "abc")
       == 3)
ok(m.match(m.P""^-3, "a") == 1)

local function find (p, s)
  return m.match(basiclookfor(p), s)
end


local function badgrammar (g, exp)
  local err, msg = pcall(m.P, g)
  ok(not err)
  if exp then ok(find(exp, msg)) end
end

badgrammar({[1] = m.V(1)}, "rule '1'")
badgrammar({[1] = m.V(2)}, "rule '2'")   -- invalid non-terminal
badgrammar({[1] = m.V"x"}, "rule 'x'")   -- invalid non-terminal
badgrammar({[1] = m.V{}}, "rule <a table>")   -- invalid non-terminal
badgrammar({[1] = #m.P("a") * m.V(1)}, "rule '1'")
badgrammar({[1] = -m.P("a") * m.V(1)}, "rule '1'")
badgrammar({[1] = -1 * m.V(1)}, "rule '1'")
badgrammar({[1] = 1 * m.V(2), [2] = m.V(2)}, "rule '2'")
badgrammar({[1] = m.P(0), [2] = 1 * m.V(1)^0}, "loop in rule '2'")
badgrammar({ lpeg.V(2), lpeg.V(3)^0, lpeg.P"" }, "rule '2'")
badgrammar({ lpeg.V(2) * lpeg.V(3)^0, lpeg.V(3)^0, lpeg.P"" }, "rule '1'")
badgrammar({ #(lpeg.V(1) * 'a') }, "rule '1'")
badgrammar({ -(lpeg.V(1) * 'a') }, "rule '1'")

ok(m.match({'a' * -lpeg.V(1)}, "aaa") == 2)
ok(m.match({'a' * -lpeg.V(1)}, "aaaa") == nil)


-- simple tests for maximum sizes:
local p = m.P"a"
for i=1,14 do p = p * p end

p = {}
for i=1,100 do p[i] = m.P"a" end
p = m.P(p)


-- strange values for rule labels

p = m.P{ "print",
     print = m.V(print),
     [print] = m.V(_G),
     [_G] = m.P"a",
   }

ok(p:match("a"))

-- initial rule
g = {}
for i = 1, 10 do g["i"..i] =  "a" * m.V("i"..i+1) end
g.i11 = m.P""
for i = 1, 10 do
  g[1] = "i"..i
  local p = m.P(g)
  ok(p:match("aaaaaaaaaaa") == 11 - i + 1)
end


-- tests for back references
ok(not pcall(m.match, m.Cb('x'), ''))
ok(not pcall(m.match, m.Cg(1, 'a') * m.Cb('b'), 'a'))

p = m.Cg(m.C(1) * m.C(1), "k") * m.Ct(m.Cb("k"))
t = p:match("ab")
checkeq(t, {"a", "b"})


t = {}
function foo (p) t[#t + 1] = p; return p .. "x" end

p = m.Cg(m.C(2)    / foo, "x") * m.Cb"x" *
    m.Cg(m.Cb('x') / foo, "x") * m.Cb"x" *
    m.Cg(m.Cb('x') / foo, "x") * m.Cb"x" *
    m.Cg(m.Cb('x') / foo, "x") * m.Cb"x"
x = {p:match'ab'}
checkeq(x, {'abx', 'abxx', 'abxxx', 'abxxxx'})
checkeq(t, {'ab',
            'ab', 'abx',
            'ab', 'abx', 'abxx',
            'ab', 'abx', 'abxx', 'abxxx'})



-- tests for match-time captures

local function id (s, i, ...)
  return true, ...
end

ok(m.Cmt(m.Cs((m.Cmt(m.S'abc' / { a = 'x', c = 'y' }, id) +
              m.R'09'^1 /  string.char +
              m.P(1))^0), id):match"acb98+68c" == "xyb\98+\68y")

p = m.P{'S',
  S = m.V'atom' * space
    + m.Cmt(m.Ct("(" * space * (m.Cmt(m.V'S'^1, id) + m.P(true)) * ")" * space), id),
  atom = m.Cmt(m.C(m.R("AZ", "az", "09")^1), id)
}
x = p:match"(a g () ((b) c) (d (e)))"
checkeq(x, {'a', 'g', {}, {{'b'}, 'c'}, {'d', {'e'}}});

x = {(m.Cmt(1, id)^0):match(string.rep('a', 500))}
ok(#x == 500)
ok(not pcall(m.match, m.Cmt(1, id)^0, string.rep('a', 50000)))

local function id(s, i, x)
  if x == 'a' then return i + 1, 1, 3, 7
  else return nil, 2, 4, 6, 8
  end   
end     

p = ((m.P(id) + m.Cmt(2, id)  + m.Cmt(1, id)))^0
ok(table.concat{p:match('abababab')} == string.rep('137', 4))

local function ref (s, i, x)
  return m.match(x, s, i - x:len())
end

ok(m.Cmt(m.P(1)^0, ref):match('alo') == 4)
ok((m.P(1) * m.Cmt(m.P(1)^0, ref)):match('alo') == 4)
ok(not (m.P(1) * m.Cmt(m.C(1)^0, ref)):match('alo'))

ref = function (s,i,x) return i == tonumber(x) and i, 'xuxu' end

ok(m.Cmt(1, ref):match'2')
ok(not m.Cmt(1, ref):match'1')
ok(m.Cmt(m.P(1)^0, ref):match'03')

function ref (s, i, a, b)
  if a == b then return i, a:upper() end
end

p = m.Cmt(m.C(m.R"az"^1) * "-" * m.C(m.R"az"^1), ref)
p = (any - p)^0 * p * any^0 * -1

ok(p:match'abbbc-bc ddaa' == 'BC')


c = '[' * m.Cg(m.P'='^0, "init") * '[' *
    { m.Cmt(']' * m.C(m.P'='^0) * ']' * m.Cb("init"), function (_, _, s1, s2)
                                               return s1 == s2 end)
       + 1 * m.V(1) } / function () end

ok(c:match'[==[]]====]]]]==]===[]' == 18)
ok(c:match'[[]=]====]=]]]==]===[]' == 14)
ok(not c:match'[[]=]====]=]=]==]===[]')



-------------------------------------------------------------------
-- Tests for 're' module
-------------------------------------------------------------------

require "re"

local match, compile = re.match, re.compile

ok(match("a", ".") == 2)
ok(match("a", "''") == 1)
ok(match("", "!.") == 1)
ok(not match("a", " ! . "))
ok(match("abcde", "  ( . . ) * ") == 5)
ok(match("abbcde", " [a-c] +") == 5)
ok(match("0abbc1de", "'0' [a-c]+ '1'") == 7)
ok(match("0zz1dda", "'0' [^a-c]+ 'a'") == 8)
ok(match("abbc--", " [a-c] + +") == 5)
ok(match("abbc--", " [ac-] +") == 2)
ok(match("abbc--", " [-acb] + ") == 7)
ok(not match("abbcde", " [b-z] + "))
ok(match("abb\"de", '"abb"["]"de"') == 7)
ok(match("abceeef", "'ac'? 'ab'* 'c' {'e'*} / 'abceeef' ") == "eee")
ok(match("abceeef", "'ac'? 'ab'* 'c' { 'f'+ } / 'abceeef' ") == 8)
local t = {match("abceefe", "((&'e' {})? .)*")}
checkeq(t, {4, 5, 7})
local t = {match("abceefe", "((&&'e' {})? .)*")}
checkeq(t, {4, 5, 7})
local t = {match("abceefe", "( ( ! ! 'e' {} ) ? . ) *")}
checkeq(t, {4, 5, 7})
local t = {match("abceefe", "((&!&!'e' {})? .)*")}
checkeq(t, {4, 5, 7})

ok(match("cccx" , "'ab'? ('ccc' / ('cde' / 'cd'*)? / 'ccc') 'x'+") == 5)
ok(match("cdx" , "'ab'? ('ccc' / ('cde' / 'cd'*)? / 'ccc') 'x'+") == 4)
ok(match("abcdcdx" , "'ab'? ('ccc' / ('cde' / 'cd'*)? / 'ccc') 'x'+") == 8)

ok(match("abc", "a <- (. <a>)?") == 4)
b = "balanced <- '(' ([^()] / <balanced>)* ')'"
ok(match("(abc)", b))
ok(match("(a(b)((c) (d)))", b))
ok(not match("(a(b ((c) (d)))", b))

b = compile[[  balanced <- "(" ([^()] / <balanced>)* ")" ]]
ok(b == m.P(b))
ok(b:match"((((a))(b)))")

local g = [[
  S <- "0" <B> / "1" <A> / ""   -- balanced strings
  A <- "0" <S> / "1" <A> <A>      -- one more 0
  B <- "1" <S> / "0" <B> <B>      -- one more 1
]]
ok(match("00011011", g) == 9)

local g = [[
  S <- ("0" <B> / "1" <A>)*
  A <- "0" / "1" <A> <A>
  B <- "1" / "0" <B> <B>
]]
ok(match("00011011", g) == 9)
ok(match("000110110", g) == 9)
ok(match("011110110", g) == 3)
ok(match("000110010", g) == 1)

s = "aaaaaaaaaaaaaaaaaaaaaaaa"
ok(match(s, "'a'^3") == 4)
ok(match(s, "'a'^0") == 1)
ok(match(s, "'a'^+3") == s:len() + 1)
ok(not match(s, "'a'^+30"))
ok(match(s, "'a'^-30") == s:len() + 1)
ok(match(s, "'a'^-5") == 6)
for i = 1, s:len() do
  ok(match(s, string.format("'a'^+%d", i)) >= i + 1)
  ok(match(s, string.format("'a'^-%d", i)) <= i + 1)
  ok(match(s, string.format("'a'^%d", i)) == i + 1)
end
ok(match("01234567890123456789", "[0-9]^3+") == 19)


ok(match("01234567890123456789", "({....}{...}) -> '%2%1'") == "4560123")
t = match("0123456789", "{.}*->{}")
checkeq(t, {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"})
ok(match("012345", "( (..) -> '%0%0' ) -> {}")[1] == "0101")

eqcharset(compile"[]]", "]")
eqcharset(compile"[][]", m.S"[]")
eqcharset(compile"[]-]", m.S"-]")
eqcharset(compile"[-]", m.S"-")
eqcharset(compile"[az-]", m.S"a-z")
eqcharset(compile"[-az]", m.S"a-z")
eqcharset(compile"[a-z]", m.R"az")
eqcharset(compile"[]['\"]", m.S[[]['"]])

eqcharset(compile"[^]]", any - "]")
eqcharset(compile"[^][]", any - m.S"[]")
eqcharset(compile"[^]-]", any - m.S"-]")
eqcharset(compile"[^]-]", any - m.S"-]")
eqcharset(compile"[^-]", any - m.S"-")
eqcharset(compile"[^az-]", any - m.S"a-z")
eqcharset(compile"[^-az]", any - m.S"a-z")
eqcharset(compile"[^a-z]", any - m.R"az")
eqcharset(compile"[^]['\"]", any - m.S[[]['"]])


-- tests for 're' with pre-definitions
defs = {digits = m.R"09", letters = m.R"az"}
e = compile("%letters (%letters / %digits)*", defs)
ok(e:match"x123" == 5)

e = compile([[
  S <- <A>+
  A <- %letters+ <B>
  B <- %digits+
]], defs)

e = compile("{[0-9]+'.'?[0-9]*} -> sin", math)
ok(e:match("2.34") == math.sin(2.34))


function eq (_, _, a, b) return a == b end

c = re.compile([[
  longstring <- '[' {:init: '='* :} '[' <close>
  close <- ']' =init ']' / . <close>
]], {void = void})

ok(c:match'[==[]]===]]]]==]===[]' == 17)
ok(c:match'[[]=]====]=]]]==]===[]' == 14)
ok(not c:match'[[]=]====]=]=]==]===[]')

c = re.compile" '[' {:init: '='* :} '[' (!(']' =init ']') .)* ']' =init ']' !. "

ok(c:match'[==[]]===]]]]==]')
ok(c:match'[[]=]====]=][]==]===[]]')
ok(not c:match'[[]=]====]=]=]==]===[]')

ok(re.find("hi alalo", "{:x:..:} =x") == 4)
ok(re.find("hi alalo", "{:x:..:} =x", 4) == 4)
ok(not re.find("hi alalo", "{:x:..:} =x", 5))
ok(re.find("hi alalo", "'al'", 5) == 6)
ok(re.find("hi aloalolo", "{:x:..:} =x") == 8)
ok(re.find("alo alohi x x", "{:word:%w+:}%W*(=word)!%w") == 11)

ok(re.gsub("alo alo", "[abc]", "x") == "xlo xlo")
ok(re.gsub("alo alo", "%w+", ".") == ". .")
ok(re.gsub("hi, how are you", "[aeiou]", string.upper) ==
               "hI, hOw ArE yOU")

s = 'hi [[a comment[=]=] ending here]] and [=[another]]=]]'
c = re.compile" '[' {:i: '='* :} '[' (!(']' =i ']') .)* ']' { =i } ']' "
ok(re.gsub(s, c, "%2") == 'hi  and =]')
ok(re.gsub(s, c, "%0") == s)
ok(re.gsub('[=[hi]=]', c, "%2") == '=')

ok(re.find("", "!.") == 1)
ok(re.find("alo", "!.") == 4)

function addtag (s, i, t, tag) t.tag = tag; return i, t end

c = re.compile([[
  doc <- <block> !.
  block <- (<start> (<block> / { [^<]+ })* -> {} <end>?) => addtag
  start <- '<' {:tag: [a-z]+ :} '>'
  end <- '</' { =tag } '>'
]], {addtag = addtag})

x = c:match[[
<x>hi<b>hello</b>but<b>totheend</x>]]
checkeq(x, {tag='x', 'hi', {tag = 'b', 'hello'}, 'but',
                     {'totheend'}})

ok(not pcall(compile, "x <- 'a'  x <- 'b'"))
ok(not pcall(compile, "'x' -> x", {x = 3}))


-- tests for look-ahead captures
x = {re.match("alo", "&(&{.}) !{'b'} {&(...)} &{..} {...} {!.}")}
checkeq(x, {"a", "", "al", "alo", ""})

ok(re.match("aloalo", "{~ (((&{'al'}) -> 'A' / (&{%l}) -> '%1')? .)* ~}")
       == "AallooAalloo")

p = re.compile[[
  block <- ({:ident:' '*:} <line>
           ((=ident !' ' <line>) / &(=ident ' ') <block>)*) -> {}
  line <- {[^%nl]*} %nl
]]

t= p:match[[
1
  1.1
  1.2
    1.2.1
    
2
  2.1
]]
checkeq(t, {"1", {"1.1", "1.2", {"1.2.1", "", ident = "    "}, ident = "  "},
            "2", {"2.1", ident = "  "}, ident = ""})


-- nested grammars
p = re.compile[[
       s <- <a> <b> !.
       b <- ( x <- ('b' <x>)? )
       a <- ( x <- 'a' <x>? )
]]

ok(p:match'aaabbb')
ok(p:match'aaa')
ok(not p:match'bbb')
ok(not p:match'aaabbba')

-- testing groups
t = {re.match("abc", "{:S <- {:.:} {<S>} / '':}")}
checkeq(t, {"a", "bc", "b", "c", "c", ""})

t = re.match("1234", "({:a:.:} {:b:.:} {:c:.{.}:}) -> {}")
checkeq(t, {a="1", b="2", c="4"})
t = re.match("1234", "({:a:.:} {:b:{.}{.}:} {:c:{.}:}) -> {}")
checkeq(t, {a="1", b="2", c="4"})
t = re.match("12345", "({:.:} {:b:{.}{.}:} {:{.}{.}:}) -> {}")
checkeq(t, {"1", b="2", "4", "5"})
t = re.match("12345", "({:.:} {:{:b:{.}{.}:}:} {:{.}{.}:}) -> {}")
checkeq(t, {"1", "23", "4", "5"})
t = re.match("12345", "({:.:} {{:b:{.}{.}:}} {:{.}{.}:}) -> {}")
checkeq(t, {"1", "23", "4", "5"})


-- testing pre-defined names
ok(os.setlocale("C") == "C")

function eqlpeggsub (p1, p2)
  local s1 = cs2str(re.compile(p1))
  local s2 = string.gsub(allchar, "[^" .. p2 .. "]", "")
if s1 ~= s2 then print(s1,s2) end
  ok(s1 == s2)
end


eqlpeggsub("%w", "%w")
eqlpeggsub("%a", "%a")
eqlpeggsub("%l", "%l")
eqlpeggsub("%u", "%u")
eqlpeggsub("%p", "%p")
eqlpeggsub("%d", "%d")
eqlpeggsub("%x", "%x")
eqlpeggsub("%s", "%s")

eqlpeggsub("%W", "%W")
eqlpeggsub("%A", "%A")
eqlpeggsub("%L", "%L")
eqlpeggsub("%U", "%U")
eqlpeggsub("%P", "%P")
eqlpeggsub("%D", "%D")
eqlpeggsub("%X", "%X")
eqlpeggsub("%S", "%S")

eqlpeggsub("[%w]", "%w")
eqlpeggsub("[%w_]", "_%w")
eqlpeggsub("[^%w]", "%W")
eqlpeggsub("[%W%S]", "%W%S")

re.updatelocale()


-- testing nested substitutions x string captures

p = re.compile[[
      text <- {~ <item>* ~}
      item <- <macro> / [^()] / '(' <item>* ')'
      arg <- ' '* {~ (!',' <item>)* ~}
      args <- '(' <arg> (',' <arg>)* ')'
      macro <- ('apply' <args>) -> '%1(%2)'
             / ('add' <args>) -> '%1 + %2'
             / ('mul' <args>) -> '%1 * %2'
]]

ok(p:match"add(mul(a,b), apply(f,x))" == "a * b + f(x)")

rev = re.compile[[ R <- (!.) -> '' / ({.} <R>) -> '%2%1']]

ok(rev:match"0123456789" == "9876543210")

