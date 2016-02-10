-- vim:ts=2:sw=2:et:ft=lua:
require("strict")
require('Test.More')
plan(4)

_TLS["fred"] = true
is(_TLS["fred"], true, "_TLS and strict compatible")

_OSTLS.wilma = true
is(_OSTLS.wilma, true, "_OSTLS and strict compatible")

local status, err = pcall(function() does_not_exist = "flurble" end)
is(status, false)
like(err, "undeclared variable 'does_not_exist'", "undefined variable errors")
