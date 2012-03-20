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

static unsigned int compute_string_hash(const char *str, size_t l)
{
  unsigned int h = (unsigned int)l; /* seed */
  size_t step = (l >> 5) + 1; /* if string is too long, don't hash it all */
  size_t l1;

  for (l1 = l; l1 >= step; l1 -= step) {
    h = h ^ ( (h << 5) + (h >> 2) + ((unsigned char)str[l1-1]));
  }

  return h;
}

/* given mtptr, a pointer to a GCheader representing
 * a table, look inside it for the "@type" field that holds
 * the type name and return that string */
static char *resolve_mt_name(const struct gimli_ana_api *api, GCheader *mtptr)
{
  Table t;
  unsigned int h;
  int snode;
  int off;
  Node *np;

  if (!mtptr) {
    return NULL;
  }

  if (api->read_mem(mtptr, &t, sizeof(t)) != sizeof(t)) {
    return NULL;
  }

  h = compute_string_hash("@type", strlen("@type"));
  snode = twoto(t.lsizenode);
  off = h & (snode - 1);

  np = t.node + off;
  do {
    Node n;

    if (api->read_mem(np, &n, sizeof(n)) != sizeof(n)) {
      return NULL;
    }

    if (n.i_key.nk.tt == LUA_TSTRING) {
      char *k = api->read_string(((TString*)n.i_key.nk.value.gc) + 1);

      if (!k) {
        return NULL;
      }

      if (!strcmp(k, "@type")) {
        free(k);
        return api->read_string(((TString*)n.i_val.value.gc) + 1);
      }
      free(k);
    }

    np = n.i_key.nk.next;
  } while (np);
  return NULL;
}

static int show_tvalue(const struct gimli_ana_api *api,
    TValue *tvp, int limit)
{
  TValue tv;

  if (api->read_mem(tvp, &tv, sizeof(tv)) != sizeof(tv)) {
    return 0;
  }

  switch (tv.tt) {
    case luat_nil:
      printf("nil\n");
      return 1;
    case luat_bool:
      printf("bool %s\n", tv.value.b ? "true" : "false");
      return 1;
    case luat_ludata:
      printf("lightuserdata\n");
      return 1;
    case luat_num:
      printf("number %f\n", tv.value.n);
      return 1;
    case luat_str:
    {
      char *str = api->read_string(((TString*)tv.value.gc) + 1);
      printf("string %p %s\n", tv.value.gc, str);
      free(str);
      return 1;
    }
    case luat_func:
      printf("func %p\n", tv.value.gc);
      return 1;
    case luat_table:
    {
      char *mt;
      Table t;

      printf("table %p", tv.value.gc);
      if (api->read_mem(tv.value.gc, &t, sizeof(t)) != sizeof(t)) {
        printf("\n");
        return 1;
      }
      mt = resolve_mt_name(api, t.metatable);
      if (mt) {
        printf(" mt %s", mt);
        free(mt);
      }
      printf("\n");
      return 1;
    }
    case luat_udata:
    {
      Udata ud;
      void *ptr;
      char *mt;

      printf("userdata %p", tv.value.p);
      if (api->read_mem(tv.value.p, &ud, sizeof(ud)) != sizeof(ud)) {
        printf("\n");
        return 1;
      }
      mt = resolve_mt_name(api, ud.uv.metatable);
      if (ud.uv.is_user_ptr && api->read_mem(((Udata*)tv.value.p) + 1,
            &ptr, sizeof(ptr)) == sizeof(ptr)) {
        printf(" userptr -> %s %p", mt ? mt : "", ptr);
      } else if (mt) {
        printf(" mt %s", mt);
      }
      free(mt);
      printf("\n");

      return 1;
    }
    case luat_thread:
      printf("thread %p\n", tv.value.gc);
      return 1;
    case luat_proto:
      printf("proto %p\n", tv.value.gc);
      return 1;
    default:
      printf("type: %d\n", tv.tt);
      return 0;
  }
}

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
  int lframeno = 1;

  /* not the cleanest way to do this, since we're assuming knoweldge of
   * typedefs defined in other modules. We'd need a typedef resolving
   * API to make this clean.  For now, we explicitly look for both the
   * real lua_State type and the well-known scpt_thread typedef */
  if (strcmp(datatype, "lua_State *") && strcmp(datatype, "scpt_thread *")) {
    return GIMLI_ANA_CONTINUE;
  }

  ret = dump_lua_state ? GIMLI_ANA_CONTINUE : GIMLI_ANA_SUPPRESS;

  if (api->read_mem(varaddr, &L, sizeof(L)) != sizeof(L)) {
    printf("failed to read lua_State\n");
    return ret;
  }

  if (last_ci && last_ci == L.ci) {
    /* already rendered this trace in an earlier call */
    printf("  %s %s @ %p [deref'ed above]\n",
        datatype, varname, varaddr);
    return ret;
  }
  last_ci = L.ci;

  printf("  * Lua call stack: (may be interleaved with following C frames)\n");

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

    printf("  #L%d ", lframeno++);

    if (cl.c.isC) {
      char buf[1024];
      char file[1024];
      const char *sym;
      int line;

      if (!api->get_source_info(cl.c.f, file, sizeof(file), &line)) {
        line = -1;
      }
      sym = api->sym_name(cl.c.f, buf, sizeof(buf));
      printf("[C:%p] ", cl.c.f);
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
      struct LocVar lv;
      int n, sn;

      if (api->read_mem(cl.l.p, &p, sizeof(p)) != sizeof(p)) {
        printf("failed to read Proto\n");
        break;
      }

      pc = (ci.savedpc - p.code) - 1;

      if (p.source) {
        char *src = api->read_string(p.source + 1);
        int line;

        api->read_mem(p.lineinfo + pc, &line, sizeof(line));
        printf("%s:%d @ pc=%d\n", src + 1, line, pc);
        free(src);
      } else {
        printf("[VM]\n");
      }

      /* print out locals */
      for (sn = 0, n = 0; n < p.sizelocvars; n++) {
        char *varname;
        int startline, endline;
        TValue val;

        if (api->read_mem(p.locvars + n, &lv, sizeof(lv)) != sizeof(lv)) {
          break;
        }
        if (lv.startpc > pc) {
          /* this local is not yet valid in this frame */
          continue;
        }

        varname = api->read_string(((TString*)lv.varname) + 1);
        api->read_mem(p.lineinfo + lv.startpc, &startline, sizeof(startline));
        api->read_mem(p.lineinfo + lv.endpc, &endline, sizeof(endline));
        printf("    local %s [lines: %d - %d] ", varname, startline, endline);
        free(varname);

        /* we can read it from the stack at offset sn from the ci.base */
        show_tvalue(api, ci.base + sn, 16);

        /* stack offset for the local */
        sn++;
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

