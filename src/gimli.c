/*
 * Copyright (c) 2012 Message Systems, Inc. All rights reserved
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF MESSAGE SYSTEMS
 * The copyright notice above does not evidence any
 * actual or intended publication of such source code.
 *
 * Redistribution of this material is strictly prohibited.
 *
 */

#include "thrlua.h"
#include "/opt/msys/gimli/include/libgimli_ana.h"

static CallInfo *last_ci = NULL;
static int dump_lua_state = 0;

/* This function intercepts the usual glider printing of lua_State
 * variables in the stack trace.
 *
 * When it finds a lua_State, it pulls out the lua call stack.
 *
 * By default, we suppress dumping the lua_State internals as they
 * are not globally useful to see, but settings GIMLI_LUA_VERBOSE
 * in the environment will allow it to be printed.
 */

static int before_var(
  const struct gimli_ana_api *api, const char *object, int tid,
  int frameno, void *pcaddr, void *context,
  const char *datatype, const char *varname,
  void *varaddr, uint64_t varsize)
{
  lua_State L;
  CallInfo ci;
  CallInfo *cip;
  Closure cl;
  TValue stk;
  int ret;

  if (strcmp(datatype, "lua_State *")) return GIMLI_ANA_CONTINUE;

  ret = dump_lua_state ? GIMLI_ANA_CONTINUE : GIMLI_ANA_SUPPRESS;

  if (last_ci == L.ci) {
    /* already rendered this trace in an earlier call */
    return ret;
  }
  last_ci = L.ci;

  printf("  * Lua call stack: (may be interleaved with following C frames)\n");

  if (api->read_mem(varaddr, &L, sizeof(L)) != sizeof(L)) {
    printf("failed to read lua_State\n");
    return ret;
  }

  /* now read out the call frames */
  for (cip = L.ci; cip && cip > L.base_ci; cip--) {
    if (api->read_mem(cip, &ci, sizeof(ci)) != sizeof(ci)) {
      printf("couldn't read next ci\n");
      break;
    }
    if (api->read_mem(ci.func, &stk, sizeof(stk)) != sizeof(stk)) {
      printf("couldn't read func from ci\n");
      break;
    }
    if (api->read_mem(stk.value.gc, &cl, sizeof(cl)) != sizeof(cl)) {
      printf("couldn't read closure from ci\n");
      break;
    }

    if (cl.c.isC) {
      char buf[1024];
      char file[1024];
      const char *sym;
      int line;

      if (!api->get_source_info(cl.c.f, file, sizeof(file), &line)) {
        line = -1;
      }
      sym = api->sym_name(cl.c.f, buf, sizeof(buf));
      printf("  [C:%p] ", cl.c.f);
      if (cl.c.fname) {
        char *l = api->read_string((void*)cl.c.fname);
        printf("\"%s\" ", l);
        free(l);
      }
      printf("\n    %s", sym);
      if (line >= 0) {
        printf("\n    %s:%d", file, line);
      }
      printf("\n");
    } else {
      Proto p;
      int pc;

      if (api->read_mem(cl.l.p, &p, sizeof(p)) != sizeof(p)) {
        printf("failed to read Proto\n");
        break;
      }

      pc = ci.savedpc - p.code;

      if (p.source) {
        char *src = api->read_string(p.source + 1);
        int line;

        api->read_mem(p.lineinfo + pc, &line, sizeof(line));
        printf("  %s:%d @ pc=%d\n", src + 1, line, pc);
        free(src);
      } else {
        printf("  [VM]\n");
      }
    }
  }
  printf("\n");

  if (ret == GIMLI_ANA_SUPPRESS) {
    printf("  %s %s @ %p (export GIMLI_LUA_VERBOSE=1 to deref)\n",
        datatype, varname, varaddr);
  }
  return ret;
}

static struct gimli_ana_module ana = {
  GIMLI_ANA_API_VERSION,
  NULL, /* perform trace */
  NULL, /* begin_thread_trace */
  NULL, /* before print frame */
  before_var, /* before print frame var */
  NULL, /* after print frame var */
  NULL, /* after print frame */
  NULL, /* end thread trace */
};

struct gimli_ana_module *gimli_ana_init(const struct gimli_ana_api *api)
{
  const char *dump = getenv("GIMLI_LUA_VERBOSE");
  if (dump && !strcmp(dump, "1")) {
    dump_lua_state = 1;
  }
  return &ana;
}

/* vim:ts=2:sw=2:et:
 */

