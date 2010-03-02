require("Test.More");
plan(36);

is("\x00", "\000", "NUL byte");
is("\x01", "\001", "1 byte");
is("\x01\x02", "\001\002", "1, 2");
is("\u0001", "\001", "1 byte");
is("\u00a9", "\xc2\xa9", "copyright");
is("\u0000a9", "\xc2\xa9", "copyright");
is("\u8000", "\xe8\x80\x80");
is("\u108000", "\xf4\x88\x80\x80");
is("\u308000", "\xf8\x8c\x88\x80\x80");
is("\u70008000", "\xfd\xb0\x80\x88\x80\x80");

-- these are tautological but are here for coverage purposes
is("\a", "\a");
is("\b", "\b");
is("\f", "\f");
is("\n", "\n");
is("\r", "\r");
is("\t", "\t");
is("\v", "\v");
is("\n\r\na", "\n\r\na");
is([[one
two
three]], "one\ntwo\nthree");

function lex(str)
  local x, e = loadstring(str);
  if x == nil then
    return e
  end
  return true
end

like(lex("--[[\n[[\nfoo\n]]\n]]"), 'nesting of.*is deprecated');
like(lex("--[[\nfoo"), 'unfinished long comment');
like(lex("a = [[\nfoo"), 'unfinished long string');
like(lex("a = \"foo"), 'unfinished string');
like(lex("a = \"foo\n"), 'unfinished string');
like(lex("a = \"foo\r"), 'unfinished string');
like(lex("a = \"\\xyo\""), 'expected hex digit');
like(lex("a = \"\\xao\""), 'expected hex digit');
like(lex("a = \"\\u0\""), 'expected either 4, 6 or 8 hex digits for unicode escape');
like(lex("a = \"\\u01\""), 'expected either 4, 6 or 8 hex digits for unicode escape');
like(lex("a = \"\\u012\""), 'expected either 4, 6 or 8 hex digits for unicode escape');
like(lex("a = \"\\u01234\""), 'expected either 4, 6 or 8 hex digits for unicode escape');
like(lex("a = \"\\u0123456\""), 'expected either 4, 6 or 8 hex digits for unicode escape');
is(lex("a = 1;\r\nb=2"), true, "lexed with \\r");

ok(1 != 2, 'testing != operator lexing');
ok(!false, 'testing unary not operator lexing');

cmp_ok(.2e5, '>', 1000);



