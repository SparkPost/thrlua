/*
** $Id: lstate.c,v 2.36.1.2 2008/01/03 15:20:39 roberto Exp $
** Global State
** See Copyright Notice in lua.h
*/

#define lstate_c
#define LUA_CORE

#include "thrlua.h"

static void stack_init (lua_State *L1, lua_State *L) {
  /* initialize CallInfo array */
  L1->base_ci = luaM_newvector(L, BASIC_CI_SIZE, CallInfo);
  L1->ci = L1->base_ci;
  L1->size_ci = BASIC_CI_SIZE;
  L1->end_ci = L1->base_ci + L1->size_ci - 1;
  /* initialize stack array */
  L1->stack = luaM_newvector(L, BASIC_STACK_SIZE + EXTRA_STACK, TValue);
  L1->stacksize = BASIC_STACK_SIZE + EXTRA_STACK;
  L1->top = L1->stack;
  L1->stack_last = L1->stack+(L1->stacksize - EXTRA_STACK)-1;
  /* initialize first ci */
  L1->ci->func = L1->top;
  setnilvalue(L1->top++);  /* `function' entry for this `ci' */
  L1->base = L1->ci->base = L1->top;
  L1->ci->top = L1->top + LUA_MINSTACK;
}


static void freestack (lua_State *L, lua_State *L1) {
  luaM_freearray(L, L1->base_ci, L1->size_ci, CallInfo);
  luaM_freearray(L, L1->stack, L1->stacksize, TValue);
}


/*
** open parts that may cause memory-allocation errors
*/
static void f_luaopen (lua_State *L, void *ud) {
  /* FIXME: some of this should be moved to the global init */
  global_State *g = G(L);
  UNUSED(ud);
  stack_init(L, L);  /* init stack */
  sethvalue(L, gt(L), luaH_new(L, 0, 2));  /* table of globals */
  setobj2n(L, &g->l_globals, gt(L));
  lua_assert(iscollectable(&g->l_globals));
  sethvalue(L, registry(L), luaH_new(L, 0, 2));  /* registry */
  luaT_init(L);
  luaX_init(L);
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
  pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_ERRORCHECK);
  pthread_mutex_init(&L->lock, &mattr);
  pthread_mutexattr_destroy(&mattr);
}


static void close_state (lua_State *L) {
  global_State *g = G(L);
  luaF_close(L, L->stack);  /* close all upvalues for this thread */
//  luaC_freeall(L);  /* collect all objects */
//  lua_assert(g->rootgc == obj2gco(L));
  freestack(L, L);
//  lua_assert(g->totalbytes == sizeof(LG));
  luaM_freemem(NULL, L, sizeof(*L));
}

lua_State *luaE_newthreadG(global_State *g)
{
  lua_State *L1 = luaC_newobj(g, LUA_TTHREAD);
  preinit_state(L1, g);
  stack_init(L1, L1);  /* init stack */
  setobj2n(L1, gt(L1), &g->l_globals);  /* share table of globals */
  return L1;
}

lua_State *luaE_newthread (lua_State *L) {
  lua_State *L1 = luaC_newobj(G(L), LUA_TTHREAD);
//  luaC_link(L, obj2gco(L1), LUA_TTHREAD);
  preinit_state(L1, G(L));
  stack_init(L1, L);  /* init stack */
  setobj2n(L, gt(L1), gt(L));  /* share table of globals */
  L1->hookmask = L->hookmask;
  L1->basehookcount = L->basehookcount;
  L1->hook = L->hook;
  resethookcount(L1);
//  lua_assert(iswhite(obj2gco(L1)));
  return L1;
}


void luaE_freethread (lua_State *L, lua_State *L1) {
  luaF_close(L1, L1->stack);  /* close all upvalues for this thread */
  lua_assert(L1->openupval.u.l.next == &L1->openupval);
  freestack(L, L1);
  pthread_mutex_destroy(&L1->lock);
  luaM_freemem(L, L1, sizeof(lua_State));
}


LUA_API lua_State *lua_newstate (lua_Alloc falloc, void *fud) {
  int i;
  lua_State *L;
  global_State *g;
 
  g = luaC_newglobal(falloc, fud);
  if (!g) {
    return NULL;
  }
  L = luaC_newobj(g, LUA_TTHREAD);
  if (L == NULL) {
    /* FIXME: leak */
    return NULL;
  }
  scpt_atomic_inc(&L->gch.ref);
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
