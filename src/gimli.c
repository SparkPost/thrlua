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
static int table_dump_limit = 16;

static gimli_hash_t derefed = NULL;

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
static char *resolve_mt_name(gimli_proc_t proc, GCheader *mtptr)
{
  Table t;
  unsigned int h;
  int snode;
  int off;
  Node *np;

  if (!mtptr) {
    return NULL;
  }

  if (gimli_read_mem(proc, (gimli_addr_t)mtptr, &t, sizeof(t)) != sizeof(t)) {
    return NULL;
  }

  h = compute_string_hash("@type", strlen("@type"));
  snode = twoto(t.lsizenode);
  off = h & (snode - 1);

  np = t.node + off;
  do {
    Node n;

    if (gimli_read_mem(proc, (gimli_addr_t)np, &n, sizeof(n)) != sizeof(n)) {
      return NULL;
    }

    if (n.i_key.nk.tt == LUA_TSTRING) {
      char *k = gimli_read_string(proc, (gimli_addr_t)(((TString*)n.i_key.nk.value.gc) + 1));

      if (!k) {
        return NULL;
      }

      if (!strcmp(k, "@type")) {
        free(k);
        return gimli_read_string(proc, (gimli_addr_t)(((TString*)n.i_val.value.gc) + 1));
      }
      free(k);
    }

    np = n.i_key.nk.next;
  } while (np);
  return NULL;
}

static int show_tvalue(gimli_proc_t proc, TValue *tvp, int limit);

static const char indent_str[] =
"                                                                           ";

/* Table is in the target address space */
static int show_table(gimli_proc_t proc, Table *tp, int limit)
{
  int i;
  int printed = 0;
  int indent = 2 + ((table_dump_limit - limit + 1) * 2);
  Table t;
  char ptrbuf[64];
  void *ptr;

  snprintf(ptrbuf, sizeof(ptrbuf), "%p", tp);
  if (gimli_hash_find(derefed, ptrbuf, &ptr)) {
    printf("[table %p deref'ed above]", tp);
    return 1;
  }
  gimli_hash_insert(derefed, ptrbuf, tp);

  if (gimli_read_mem(proc, (gimli_addr_t)tp, &t, sizeof(t)) != sizeof(t)) {
    return 0;
  }

  if (indent >= sizeof(indent_str) - 2) {
    indent = sizeof(indent_str) - 2;
  }

  if (limit <= 1) {
    printf("{ ... }");
    return 1;
  }

  printf("{ ");
  for (i = 0; i < t.sizearray; i++) {
    TValue val;

    if (gimli_read_mem(proc, (gimli_addr_t)(t.array + i), &val, sizeof(val)) != sizeof(val)) {
      break;
    }

    if (ttisnil(&val)) {
      continue;
    }

    if (!printed) {
      printed = 1;
      printf("\n");
    }
    printf("%.*s[%d] = ", indent, indent_str, i + 1);
    show_tvalue(proc, t.array + i, limit - 1);
    printf("\n");
  }

  for (i = twoto(t.lsizenode) - 1; i >= 0; i--) {
    Node node;

    if (gimli_read_mem(proc, (gimli_addr_t)(t.node + i), &node, sizeof(node)) != sizeof(node)) {
      break;
    }
    if (ttisnil(gval(&node))) {
      continue;
    }

    if (!printed) {
      printed = 1;
      printf("\n");
    }
    printf("%.*s[", indent, indent_str);
    show_tvalue(proc, (TValue*)(
          ((uintptr_t)(t.node + i)) + sizeof(TValue)), limit - 1);
    printf("] = ");
    show_tvalue(proc, (TValue*)(t.node + i), limit - 1);
    printf("\n");
  }
  if (printed) {
    indent -= 4;
    if (indent < 4) indent = 4;
    printf("%.*s", indent, indent_str);
  }
  printf("}");

  return 1;
}

static int show_tvalue(gimli_proc_t proc, TValue *tvp, int limit)
{
  TValue tv;

  if (limit <= 0) {
    return 0;
  }

  if (gimli_read_mem(proc, (gimli_addr_t)tvp, &tv, sizeof(tv)) != sizeof(tv)) {
    return 0;
  }

  switch (tv.tt) {
    case luat_nil:
      printf("nil");
      return 1;
    case luat_bool:
      printf("bool %s", tv.value.b ? "true" : "false");
      return 1;
    case luat_ludata:
      printf("lightuserdata %p", tv.value.p);
      return 1;
    case luat_num:
      printf("number %f", tv.value.n);
      return 1;
    case luat_str:
    {
      char *str = gimli_read_string(proc, (gimli_addr_t)(((TString*)tv.value.gc) + 1));
      printf("string %p \"%s\"", tv.value.gc, str);
      free(str);
      return 1;
    }
    case luat_func:
      printf("func %p", tv.value.gc);
      return 1;
    case luat_table:
    {
      char *mt;
      Table t;

      printf("table %p ", tv.value.gc);
      if (gimli_read_mem(proc, (gimli_addr_t)tv.value.gc, &t, sizeof(t)) != sizeof(t)) {
        return 1;
      }
      mt = resolve_mt_name(proc, t.metatable);
      if (mt) {
        printf("mt %s ", mt);
        free(mt);
      } else if (t.metatable) {
        printf("mt %p ", t.metatable);
        show_table(proc, (Table*)t.metatable, limit - 1);
        printf(" ");
      }
      show_table(proc, (Table*)tv.value.gc, limit - 1);
      return 1;
    }
    case luat_udata:
    {
      Udata ud;
      void *ptr;
      char *mt;

      printf("userdata %p", tv.value.p);
      if (gimli_read_mem(proc, (gimli_addr_t)tv.value.p, &ud, sizeof(ud)) != sizeof(ud)) {
        return 1;
      }
      mt = resolve_mt_name(proc, ud.uv.metatable);
      if (ud.uv.is_user_ptr && gimli_read_mem(proc, (gimli_addr_t)(((Udata*)tv.value.p) + 1),
            &ptr, sizeof(ptr)) == sizeof(ptr)) {
        printf(" userptr -> %s %p", mt ? mt : "", ptr);
      } else if (mt) {
        printf(" mt %s", mt);
      }
      free(mt);

      return 1;
    }
    case luat_thread:
      printf("thread %p", tv.value.gc);
      return 1;
    case luat_proto:
      printf("proto %p", tv.value.gc);
      return 1;
    default:
      printf("type: %d", tv.tt);
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

static gimli_iter_status_t print_lua_State(gimli_proc_t proc,
    gimli_stack_frame_t frame,
    const char *varname, gimli_type_t t, gimli_addr_t varaddr,
    int depth, void *arg)
{
  lua_State L;
  CallInfo ci;
  CallInfo *cip;
  Closure cl;
  TValue stk;
  gimli_iter_status_t ret;
  int lframeno = 1;
  const char *datatype = gimli_type_declname(t);

  if (!varname) varname = "";
  ret = dump_lua_state ? GIMLI_ITER_CONT : GIMLI_ITER_STOP;

  if (gimli_read_mem(proc, varaddr, &L, sizeof(L)) != sizeof(L)) {
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

  printf("  * Lua call stack: (interleaved with surrounding C frames)\n");

  /* now read out the call frames */
  for (cip = L.ci; cip && cip > L.base_ci; cip--) {
    if (gimli_read_mem(proc, (gimli_addr_t)cip, &ci, sizeof(ci)) != sizeof(ci)) {
      printf("couldn't read next ci\n");
      break;
    }
    if (gimli_read_mem(proc, (gimli_addr_t)ci.func, &stk, sizeof(stk)) != sizeof(stk)) {
      printf("couldn't read func from ci\n");
      break;
    }
    if (gimli_read_mem(proc, (gimli_addr_t)stk.value.gc, &cl, sizeof(cl)) != sizeof(cl)) {
      printf("couldn't read closure from ci\n");
      break;
    }

    printf("  #L%d ", lframeno++);

    if (cl.c.isC) {
      char buf[1024];
      char file[1024];
      const char *sym;
      uint64_t line;

      if (!gimli_determine_source_line_number(proc, (gimli_addr_t)cl.c.f,
            file, sizeof(file), &line)) {
        line = -1;
      }
      sym = gimli_pc_sym_name(proc, (gimli_addr_t)cl.c.f, buf, sizeof(buf));
      printf("[C:%p] ", cl.c.f);
      if (cl.c.fname) {
        char *l = gimli_read_string(proc, (gimli_addr_t)cl.c.fname);
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

      if (gimli_read_mem(proc, (gimli_addr_t)cl.l.p, &p, sizeof(p)) != sizeof(p)) {
        printf("failed to read Proto\n");
        break;
      }

      pc = (ci.savedpc - p.code) - 1;

      if (p.source) {
        char *src = gimli_read_string(proc, (gimli_addr_t)(p.source + 1));
        int line;

        gimli_read_mem(proc, (gimli_addr_t)(p.lineinfo + pc), &line, sizeof(line));
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

        if (gimli_read_mem(proc, (gimli_addr_t)(p.locvars + n), &lv, sizeof(lv)) != sizeof(lv)) {
          break;
        }
        if (lv.startpc > pc) {
          /* this local is not yet valid in this frame */
          continue;
        }

        varname = gimli_read_string(proc, (gimli_addr_t)(((TString*)lv.varname) + 1));
        gimli_read_mem(proc, (gimli_addr_t)(p.lineinfo + lv.startpc), &startline, sizeof(startline));
        gimli_read_mem(proc, (gimli_addr_t)(p.lineinfo + lv.endpc), &endline, sizeof(endline));
        printf("    local %s [lines: %d - %d] ", varname, startline, endline);
        free(varname);

        /* we can read it from the stack at offset sn from the ci.base */
        if (show_tvalue(proc, ci.base + sn, table_dump_limit)) {
          printf("\n");
        }

        /* stack offset for the local */
        sn++;
      }
    }
  }
  if (lframeno == 1) printf("  <inactive Lua stack>");
  printf("\n");

  if (ret == GIMLI_ANA_SUPPRESS) {
    printf("  %s %s @ %p (export GIMLI_LUA_VERBOSE=1 to deref)\n",
        datatype, varname, varaddr);
  }
  return ret;
}

static const char *lua_state_typenames[] = { "struct lua_State" };

int gimli_module_init(int api_version)
{
  const char *dump = getenv("GIMLI_LUA_VERBOSE");
  if (dump && !strcmp(dump, "1")) {
    dump_lua_state = 1;
  }
  dump = getenv("GIMLI_LUA_RECURSION_DEPTH");
  if (dump) {
    int val = atoi(dump);
    if (val > 0) {
      table_dump_limit = val;
    }
  }
  derefed = gimli_hash_new(NULL);

  gimli_module_register_var_printer_for_types(lua_state_typenames, 1,
      print_lua_State, NULL);

  return 1;
}

/* vim:ts=2:sw=2:et:
 */

