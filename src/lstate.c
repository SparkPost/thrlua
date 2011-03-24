/*
** $Id: lstate.c,v 2.36.1.2 2008/01/03 15:20:39 roberto Exp $
** Global State
** See Copyright Notice in lua.h
*/

#define lstate_c
#define LUA_CORE

#include "thrlua.h"

void lua_lock(lua_State *L)
{
  int r;

  /* when entering an interpreter, ensure that the current thread
   * is added to the list of those that will be stopped when we
   * need to stop the world */
  luaC_get_per_thread();

  do {
    r = pthread_mutex_lock(&L->lock);
  } while (r == EINTR || r == EAGAIN);

  if (r) {
    luaL_error(L, "lua_lock(%p) failed with errno %d: %s\n",
      L, r, strerror(r));
  }
}

void lua_unlock(lua_State *L)
{
  int r;
  do {
    r = pthread_mutex_unlock(&L->lock);
  } while (r == EINTR || r == EAGAIN);
  if (r) {
    luaL_error(L, "lua_unlock(%p) failed with errno %d: %s\n",
      L, r, strerror(r));
  }
}

static void stack_init (lua_State *L1, lua_State *L) {
  /* initialize CallInfo array */
  L1->base_ci = luaM_newvector(L, LUA_MEM_CALLINFO, BASIC_CI_SIZE, CallInfo);
  L1->ci = L1->base_ci;
  L1->size_ci = BASIC_CI_SIZE;
  L1->end_ci = L1->base_ci + L1->size_ci - 1;
  /* initialize stack array */
  L1->stack = luaM_newvector(L, LUA_MEM_STACK, BASIC_STACK_SIZE + EXTRA_STACK, TValue);
  L1->stacksize = BASIC_STACK_SIZE + EXTRA_STACK;
  L1->top = L1->stack;
  L1->stack_last = L1->stack+(L1->stacksize - EXTRA_STACK)-1;
  /* initialize first ci */
  L1->ci->func = L1->top;
  setnilvalue(L1->top++);  /* `function' entry for this `ci' */
  L1->base = L1->ci->base = L1->top;
  L1->ci->top = L1->top + LUA_MINSTACK;
}


static void freestack(lua_State *L1)
{
  luaM_freearray(L1, LUA_MEM_CALLINFO, L1->base_ci, L1->size_ci, CallInfo);
  luaM_freearray(L1, LUA_MEM_STACK, L1->stack, L1->stacksize, TValue);
}

static int do_tls_access(lua_State *L)
{
  size_t len;
  const char *key = luaL_checklstring(L, 2, &len);

  if (!strcmp(key, "_TLS")) {
    lua_pushvalue(L, LUA_TLSINDEX);
    return 1;
  }
  if (!strcmp(key, "_OSTLS")) {
    global_State *g = G(L);
    thr_State *pt = luaC_get_per_thread();

    lua_checkstack(L, 3);

    /* push table of per os-thread data */
    lua_pushvalue(L, LUA_OSTLSINDEX);
    lua_assert(lua_type(L, -1) == LUA_TTABLE);

    /* each OS thread is keyed by a lightudata(pt) */
    /* push the key */
    lua_pushlightuserdata(L, pt);
    lua_rawget(L, -2);

    if (lua_isnil(L, -1)) {
      lua_newtable(L);
      /* copy table to store in g->ostls */
      lua_pushlightuserdata(L, pt);
      lua_pushvalue(L, -2);
      lua_rawset(L, LUA_OSTLSINDEX);
      /* this leaves the new table on the top of stack */
    } /* else: existing table is on top of stack */

    lua_assert(lua_type(L, -1) == LUA_TTABLE);

    return 1;
  }
  lua_pushnil(L);
  return 1;
}

static void stringtable_init(lua_State *L)
{
  L->strt.hash = luaM_realloc(L, LUA_MEM_STRING_TABLE, NULL, 0,
                    MINSTRTABSIZE * sizeof(struct stringtable_node*));
  memset(L->strt.hash, 0, MINSTRTABSIZE * sizeof(struct stringtable_node*));
  L->strt.size = MINSTRTABSIZE;
}

/*
** open parts that may cause memory-allocation errors
*/
static void f_luaopen (lua_State *L, void *ud) {
  global_State *g = G(L);
  UNUSED(ud);
  stack_init(L, L);  /* init stack */

  stringtable_init(L);
  sethvalue(L, gt(L), luaH_new(L, 0, 2));  /* table of globals */
  setobj2n(L, &g->l_globals, gt(L));
  g->memerr = luaS_newliteral(L, MEMERRMSG);
  lua_assert(iscollectable(&g->l_globals));
  sethvalue(L, registry(L), luaH_new(L, 0, 2));  /* registry */
  luaT_init(L);
  luaX_init(L);
  luaZ_initbuffer(g, &L->buff);

  /* wire up _TLS and _OSTLS */
  luaL_newmetatable(L, "_G.TLS");
  lua_pushcclosure2(L, "_G.__index:TLS", do_tls_access, 0);
  lua_setfield(L, -2, "__index");
  lua_setmetatable(L, LUA_GLOBALSINDEX);

  if (G(L)->on_state_create) {
    G(L)->on_state_create(L);
  }
}


static void preinit_state (lua_State *L, global_State *g)
{
  pthread_mutexattr_t mattr;

  G(L) = g;
  L->allowhook = 1;
  L->openupval.u.l.prev = &L->openupval;
  L->openupval.u.l.next = &L->openupval;
  setnilvalue(gt(L));

  pthread_mutexattr_init(&mattr);
  pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&L->lock, &mattr);
  pthread_mutexattr_destroy(&mattr);

  L->Black = &L->B0;
  L->White = &L->B1;
}


static void close_state (lua_State *L) {
  global_State *g = G(L);
  luaF_close(L, L->stack);  /* close all upvalues for this thread */
  freestack(L);
  luaZ_freebuffer(L, &L->buff);
  luaM_freemem(L, LUA_MEM_THREAD, L, sizeof(*L));
}

lua_State *luaE_newthread (lua_State *L) {
  lua_State *L1 = luaC_newobj(L, LUA_TTHREAD);
  preinit_state(L1, G(L));
  stack_init(L1, L);  /* init stack */
  setobj2n(L, gt(L1), gt(L));  /* share table of globals */
  L1->hookmask = L->hookmask;
  L1->basehookcount = L->basehookcount;
  L1->hook = L->hook;
  resethookcount(L1);
  stringtable_init(L1);
  luaZ_initbuffer(G(L), &L1->buff);

  lua_lock(G(L)->mainthread);
  L1->prev = G(L)->mainthread;
  G(L)->mainthread->next = L1;
  lua_unlock(G(L)->mainthread);

  if (G(L1)->on_state_create) {
    G(L1)->on_state_create(L1);
  }
  return L1;
}

void luaE_flush_stringtable(lua_State *L)
{
  int i;

  for (i = 0; i < L->strt.size; i++) {
    struct stringtable_node *n;

    while (L->strt.hash[i]) {
      n = L->strt.hash[i];
      L->strt.hash[i] = n->next;
      luaM_freemem(L, LUA_MEM_STRING_TABLE_NODE, n, sizeof(*n));
    }
  }
}


void luaE_freethread (lua_State *L, lua_State *L1) {
  if (G(L1)->on_state_finalize) {
    G(L1)->on_state_finalize(L1);
  }
  luaF_close(L1, L1->stack);  /* close all upvalues for this thread */
  lua_assert(L1->openupval.u.l.next == &L1->openupval);
  freestack(L1);
  luaE_flush_stringtable(L1);
  luaM_realloc(L1, LUA_MEM_STRING_TABLE, L1->strt.hash,
    L1->strt.size * sizeof(struct stringtable_node*), 0);
  pthread_mutex_destroy(&L1->lock);
  luaZ_freebuffer(L1, &L1->buff);
  if (L1 != G(L1)->mainthread) {
    luaM_freemem(L, LUA_MEM_THREAD, L1, sizeof(lua_State));
  }
}

#if 0
LUA_API lua_State *lua_newstate (lua_Alloc falloc, void *fud) {
  struct lua_StateParams p;

  memset(&p, 0, sizeof(p));
  p.allocfunc = falloc;
  p.allocdata = fud;
  return lua_newglobalstate(&p);
}
#endif

LUA_API lua_State *(lua_newglobalstate)(struct lua_StateParams *p)
{
  int i;
  lua_State *L;
  global_State *g;
 
  g = luaC_newglobal(p);
  if (!g) {
    return NULL;
  }
  L = (lua_State*)(g + 1);
  if (L == NULL) {
    /* FIXME: leak */
    return NULL;
  }
  memset(L, 0, sizeof(*L) + g->extraspace);
  L->gch.tt = LUA_TTHREAD;
  /* L->black is implicitly 0, so this object is implicitly black */
  ck_pr_inc_32(&L->gch.ref);
  preinit_state(L, g);
  g->mainthread = L;
  g->panic = NULL;

  if (luaD_rawrunprotected(L, f_luaopen, NULL) != 0) {
    /* memory allocation error: free partial state */
    close_state(L);
    L = NULL;
  }
  return L;
}

/* vim:ts=2:sw=2:et:
 */
