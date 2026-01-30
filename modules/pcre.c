/*
 * Copyright (c) 2007-2010 Message Systems, Inc. All rights reserved
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF MESSAGE SYSTEMS
 * The copyright notice above does not evidence any
 * actual or intended publication of such source code.
 *
 * Redistribution of this material is strictly prohibited.
 *
 */
#include "thrlua.h"
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "pcre.h"

#define DO_MATCH 1
#define DO_REPLACE 2
#define DO_SPLIT 3

/*
matches, errstr, errnum = pcre.match(subject, pattern);

Matches text in a string based on a regex.

subject: the string to match against
pattern: the PCRE pattern specifying the match
matches: nil if no matches were found, otherwise a table consisting
  of the following indices:

  0 = complete matched portion of the string
  1 = 1st captured subexpression
  2 = 2nd captured subexpression
  and so on

  If named captures, (?P<name>....), were used in the pattern, then
  the names will be also be keys in the table, with their values set
  to the value of the appropriate subexpression.

errstr: if there was a failure during compilation of the regex, this
  string wil contain the error message.

errnum: this will contain a numeric representation of the error condition.
*/

/*
newstr, errstr, errnum = pcre.replace(
  subject, pattern, replacement [,limit]);

Replaces text in a string based on a regex.

subject: the string to match against
pattern: the PCRE pattern specifying the match
replacement: a string specifying the replacement for the pattern.
  The replacement string can contain backreferences in a couple of forms:
  $0 is replaced by the completed matched portion of the subject
  $1 is replaced by the value of the 1st captured subexpression
  $2 is replaced by the value of the 2nd captured subexpression
  and so on.

  You may also use \0 \{0} and ${0} as synonyms for $0.

  \\ is replaced by a literal backslash and \$ is replaced by a literal $.

  If named captures were used, then ${name} will be replaced by the
  value of the subexpression named "name".

  Alternatively, replacement can be a closure, in which case it will
  be invoked for each match and passed a table of results structured
  exactly as the return value from pcre.match.  The closure can
  return nil or false to indicate that the original text should be
  preserved, otherwise it must return a string that will be used as
  the replacement.  Any error encountered in the closure will not be
  caught by pcre.replace; it will be propagated onwards.

limit: an optional upper bound on the number of replacements that should
  occur.  If not specified, there is no limit.

newstr: the original subject string with patterns replaced.
errstr: if there was a failure during compilation of the regex, this
  string wil contain the error message.

errnum: this will contain a numeric representation of the error condition.
*/

/*
array, errstr, errnum = pcre.split(
  subject, pattern [,limit]);

Splits a string into an array based on a regex.

subject: the string to match against
pattern: the PCRE pattern specifying the match

limit: an optional upper bound on the number of items
  returned in the array.
  If not specified, there is no limit.

array: the original subject string split into substrings based on the pattern.
  This function will never generate empty strings if the pattern matches
  two or more consecutive substrings in the subject.

errstr: if there was a failure during compilation of the regex, this
  string wil contain the error message.

errnum: this will contain a numeric representation of the error condition.
*/


static int perform_regex(lua_State *thr, int mode)
{
  const char *pattern, *subject, *replacement, *repend;
  size_t patlen, subjlen, replen = 0;
  const char *error = NULL;
  int erroroffset;
  int rc, i;
  int *ovector = NULL;
  size_t size;
  pcre *re = NULL;
  pcre_extra *ree = NULL;
  int name_cnt = 0;
  int capture_cnt = 0;
  luaL_Buffer retbuf;
  int start_offset = 0;
  int exopts = 0;
  int name_size = 0;
  unsigned char *name_table;
  int repl_limit = -1;

  subject = luaL_checklstring(thr, 1, &subjlen);
  pattern = luaL_checklstring(thr, 2, &patlen);
  if (mode == DO_REPLACE) {
    int rtype = lua_type(thr, 3);
    luaL_argcheck(thr, rtype == LUA_TNUMBER || rtype == LUA_TSTRING ||
      rtype == LUA_TFUNCTION, 3, "expected string/function");
    if (rtype != LUA_TFUNCTION) {
      replacement = luaL_checklstring(thr, 3, &replen);
    } else {
      replacement = NULL;
    }
    repl_limit = luaL_optint(thr, 4, -1);
    luaL_buffinit(thr, &retbuf);
    repend = replacement + replen;
  }
  if (mode == DO_SPLIT) {
    /* for split, the limit is the number of returned strings */
    repl_limit = luaL_optint(thr, 3, -1);
    if (repl_limit != -1) {
      repl_limit--;
    }
  }

  if (!patlen) {
    lua_pushnil(thr);
    lua_pushliteral(thr, "an empty pattern was provided");
    lua_pushinteger(thr, 0);
    return 3;
  }

  re = pcre_compile(pattern, PCRE_UTF8, &error, &erroroffset, NULL);
  if (!re) {
    lua_pushnil(thr);
    lua_pushstring(thr, error);
    lua_pushinteger(thr, erroroffset);
    return 3;
  }
  ree = pcre_study(re, 0, &error);

  pcre_fullinfo(re, ree, PCRE_INFO_CAPTURECOUNT, &capture_cnt);
  pcre_fullinfo(re, ree, PCRE_INFO_NAMECOUNT, &name_cnt);
  pcre_fullinfo(re, ree, PCRE_INFO_NAMETABLE, &name_table);
  pcre_fullinfo(re, ree, PCRE_INFO_NAMEENTRYSIZE, &name_size);

  size = (capture_cnt + 1) * 3;

  ovector = malloc(size * sizeof(int));
  if (!ovector) {
    if (re) pcre_free(re);
    if (ree) pcre_free(ree);
    luaL_error(thr, "failed to allocate ovector for pcre");
    return 0;
  }

  LUAI_TRY_BLOCK(thr) {

    if (mode == DO_SPLIT) {
      /* arbitrarily guess at probably 8 resultant items in the common case */
      lua_createtable(thr, 8, 0);
      i = 1;
    }

    do {
      rc = pcre_exec(re, ree, subject, subjlen, start_offset,
          exopts, ovector, size);

      if (rc < 0 && mode == DO_MATCH) {
        lua_pushnil(thr);
        break;
      }

      if (mode == DO_MATCH || (mode == DO_REPLACE && replacement == NULL)) {
        if (mode == DO_REPLACE) {
          /* push callback function */
          lua_pushvalue(thr, 3);
        }
        /* note that the 0th match is the input string.
         * the 1st and up are the subpatterns */
        lua_createtable(thr, rc + name_cnt, 0);
        for (i = 0; i < rc; i++) {
          lua_pushinteger(thr, i);
          lua_pushlstring(thr,
              (char*)(subject + ovector[2*i]),
              ovector[2*i+1] - ovector[2*i]);
          lua_settable(thr, -3);
        }

        /* for named captures, also populate the table with name => value
         * mapping */
        if (name_cnt > 0) {
          unsigned char *tabptr;

          tabptr = name_table;
          for (i = 0; i < name_cnt; i++) {
            int n = (tabptr[0] << 8) | tabptr[1];

            /* the capture name */
            lua_pushstring(thr, (char*)(tabptr + 2));
            /* the captured value */
            lua_pushlstring(thr,
                (char*)(subject + ovector[2*n]),
                ovector[2*n+1] - ovector[2*n]);

            lua_settable(thr, -3);
            tabptr += name_size;
          }
        }
      }

      if (mode == DO_REPLACE || mode == DO_SPLIT) {
        const char *match = NULL;
        size_t matchlen;

        if (rc > 0 && repl_limit != 0) {
          const char *walk;

          if (mode == DO_SPLIT) {
            if (ovector[0] - start_offset) {
              lua_pushinteger(thr, i++);
              lua_pushlstring(thr,
                  subject + start_offset, ovector[0] - start_offset);
              lua_settable(thr, -3);

              if (repl_limit != -1) {
                repl_limit--;
              }
            }

          } else { /* DO_REPLACE */
            /* copy out portion before the match */
            luaL_addlstring(&retbuf,
                subject + start_offset, ovector[0] - start_offset);

            if (replacement) {
              /* and substitute back references from the matched portion */
              walk = replacement;
              while (walk < repend) {
                if (walk[0] == '\\' || walk[0] == '$') {
                  int in_brace = 0;
                  int bref = 0;
                  char name[128];

                  if (walk > replacement && walk[-1] == '\\') {
                    /* quoted; not a backref */
                    luaL_addchar(&retbuf, walk[0]);
                    walk++;
                    continue;
                  }
                  if (walk[0] == '$') {
                    walk++;
                    if (walk[0] == '{') {
                      in_brace++;
                      walk++;
                    }
                  } else {
                    walk++;
                  }

                  if (walk[0] >= '0' && walk[0] <= '9') {
                    name[0] = '\0';
                    while (walk < repend) {
                      if (in_brace && walk[0] == '}') {
                        walk++;
                        break;
                      }
                      if (walk[0] >= '0' && walk[0] <= '9') {
                        bref = (bref * 10) + (walk[0] - '0');
                        walk++;
                        continue;
                      }
                      break;
                    }
                  } else if (in_brace) {
                    /* a named capture */
                    bref = 0;
                    while (walk < repend && bref < sizeof(name) - 1) {
                      if (walk[0] == '}') {
                        walk++;
                        break;
                      }
                      name[bref++] = walk[0];
                      walk++;
                    }
                    /* handle case where buffer limit was reached */
                    if (bref >= sizeof(name) - 1 && walk < repend) {
                      if (walk[0] != '}') {
                        /* name was truncated, skip remaining and warn */
                        luaL_error(thr, "thrlua:perform_regex buffer overflow. Truncated max %zu\n", sizeof(name));
                        while (walk < repend && walk[0] != '}') {
                          walk++;
                        }
                      }
                      /* always skip the closing brace if present */
                      if (walk < repend && walk[0] == '}') {
                        walk++;
                      }
                    }
                    name[bref] = '\0';
                  } else {
                    /* invalid */
                    continue;
                  }

                  if (name[0]) {
                    bref = pcre_get_stringnumber(re, name);
                  }

                  if (bref >= 0 && bref < rc) {
                    match = subject + ovector[bref<<1];
                    matchlen = ovector[(bref<<1)+1] - ovector[bref<<1];
                    luaL_addlstring(&retbuf, match, matchlen);
                  }
                } else {
                  luaL_addchar(&retbuf, walk[0]);
                  walk++;
                }
              }
            } else {
              /* we already prepped the callback; we're pasing the
               * table of matches to the callback and expect it to
               * return the replacement string.  Alternatively,
               * it can return nil or false to indicate that the
               * replacement not occur; we'll retain the matched text. */
              lua_call(thr, 1, 1);
              if (!lua_toboolean(thr, -1)) { /* nil or false */
                lua_pop(thr, 1); /* throw away nil/false */
                match = subject + ovector[0];
                matchlen = ovector[1] - ovector[0];
                luaL_addlstring(&retbuf, match, matchlen);
              } else if (!lua_isstring(thr, -1)) {
                luaL_error(thr, "invalid replacement value (a %s)",
                    luaL_typename(thr, -1));
              } else {
                match = luaL_checklstring(thr, -1, &matchlen);
                luaL_addlstring(&retbuf, match, matchlen);
                lua_pop(thr, 1); /* throw away string */
              }

            }
            if (repl_limit != -1) {
              repl_limit--;
            }
          }

        } else if (rc == PCRE_ERROR_NOMATCH || repl_limit == 0) {
          /* if we previously set PCRE_NOTEMPTY after a null match,
           * we need to advance the start offset and continue. */
          if (exopts && start_offset < subjlen) {
            ovector[0] = start_offset;
            ovector[1] = start_offset + 1;
            /* advance by 1 character */
            luaL_addchar(&retbuf, subject[start_offset]);
          } else {
            /* this is the end of the string */
            if (mode == DO_REPLACE) {
              luaL_addlstring(&retbuf, subject + start_offset,
                  subjlen - start_offset);
            } else if (mode == DO_SPLIT) {
              if (subjlen - start_offset) {
                lua_pushinteger(thr, i++);
                lua_pushlstring(thr,
                    subject + start_offset, subjlen - start_offset);
                lua_settable(thr, -3);
              }
            }
            break;
          }
        } else {
          break;
        }
        /* if we matched an empty string, mimic the perl /g option by
         * setting PCRE_NOTEMPTY and try the match again at the same
         * point.  If this fails (picked up above), advance by 1 char */
        exopts = (ovector[1] == ovector[0]) ?
          (PCRE_NOTEMPTY | PCRE_ANCHORED) : 0;

        /* advance to next unmatched portion of the subject */
        start_offset = ovector[1];
      }
    } while (mode != DO_MATCH);

    if (mode == DO_REPLACE) {
      luaL_pushresult(&retbuf);
    }
  } LUAI_TRY_FINALLY(thr) {
    free(ovector);
    if (re) pcre_free(re);
    if (ree) pcre_free(ree);
  } LUAI_TRY_END(thr);

  return 1;
}

static int do_match(lua_State *thr)
{
  return perform_regex(thr, DO_MATCH);
}

static int do_replace(lua_State *thr)
{
  return perform_regex(thr, DO_REPLACE);
}

static int do_split(lua_State *thr)
{
  return perform_regex(thr, DO_SPLIT);
}

/* these functions are in the pcre namespace */
static const luaL_reg funcs[] = {
  { "match", do_match },
  { "replace", do_replace },
  { "split", do_split },
  { NULL, NULL }
};

int luaopen_pcre(lua_State *thr)
{
  luaL_register(thr, "pcre", funcs);
  return 1;
}

/* vim:ts=2:sw=2:et:
*/
