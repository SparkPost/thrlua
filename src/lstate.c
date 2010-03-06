/*
** $Id: lstate.c,v 2.36.1.2 2008/01/03 15:20:39 roberto Exp $
** Global State
** See Copyright Notice in lua.h
*/

#define lstate_c
#define LUA_CORE

#include "thrlua.h"

/*
** Main thread combines a thread state and the global state
*/
typedef struct LG {
  lua_State l;
  global_State g;
} LG;
  


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
  sethvalue(L, registry(L), luaH_new(L, 0, 2));  /* registry */
  luaT_init(L);
  luaX_init(L);
  g->GCthreshold = 4*g->totalbytes;
}


static void preinit_state (lua_State *L, global_State *g)
{
  memset(L, 0, sizeof(*L));
  G(L) = g;
  L->allowhook = 1;
  L->openupval.u.l.prev = &L->openupval;
  L->openupval.u.l.next = &L->openupval;

  setnilvalue(gt(L));
}


static void close_state (lua_State *L) {
  global_State *g = G(L);
  luaF_close(L, L->stack);  /* close all upvalues for this thread */
//  luaC_freeall(L);  /* collect all objects */
  lua_assert(g->rootgc == obj2gco(L));
  freestack(L, L);
  lua_assert(g->totalbytes == sizeof(LG));
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
  lua_assert(iswhite(obj2gco(L1)));
  return L1;
}


void luaE_freethread (lua_State *L, lua_State *L1) {
  luaF_close(L1, L1->stack);  /* close all upvalues for this thread */
  lua_assert(L1->openupval == NULL);
  freestack(L, L1);
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
  if (L == NULL) return NULL;
  scpt_atomic_inc(&L->gch.ref);
#if 0
  L->next = NULL;
  L->tt = LUA_TTHREAD;
  g->currentwhite = bit2mask(WHITE0BIT, FIXEDBIT);
  L->marked = luaC_white(g);
  set2bits(L->marked, FIXEDBIT, SFIXEDBIT);
#endif
  preinit_state(L, g);
  g->mainthread = L;
#if 0
  g->uvhead.u.l.prev = &g->uvhead;
  g->uvhead.u.l.next = &g->uvhead;
  g->GCthreshold = 0;  /* mark it as unfinished state */
#endif
  setnilvalue(registry(L));
  g->panic = NULL;
#if 0
  g->gcstate = GCSpause;
  g->rootgc = obj2gco(L);
  g->sweepstrgc = 0;
  g->sweepgc = &g->rootgc;
  g->gray = NULL;
  g->grayagain = NULL;
  g->weak = NULL;
  g->tmudata = NULL;
#endif
  g->totalbytes = sizeof(LG);
#if 0
  g->gcpause = LUAI_GCPAUSE;
  g->gcstepmul = LUAI_GCMUL;
  g->gcdept = 0;
  for (i=0; i<NUM_TAGS; i++) g->mt[i] = NULL;
#endif
  if (luaD_rawrunprotected(L, f_luaopen, NULL) != 0) {
    /* memory allocation error: free partial state */
    close_state(L);
    L = NULL;
  }
  return L;
}


static void callallgcTM (lua_State *L, void *ud) {
  UNUSED(ud);
  luaC_callGCTM(L);  /* call GC metamethods for all udata */
}


LUA_API void lua_close (lua_State *L) {
  L = G(L)->mainthread;  /* only the main thread can be closed */
  lua_lock(L);
  luaF_close(L, L->stack);  /* close all upvalues for this thread */
//  luaC_separateudata(L, 1);  /* separate udata that have GC metamethods */
  L->errfunc = 0;  /* no error function during GC metamethods */
#if 0
  do {  /* repeat until no more errors */
    L->ci = L->base_ci;
    L->base = L->top = L->ci->base;
    L->nCcalls = L->baseCcalls = 0;
  } while (luaD_rawrunprotected(L, callallgcTM, NULL) != 0);
#endif
  lua_assert(G(L)->tmudata == NULL);
  close_state(L);
}

/* vim:ts=2:sw=2:et:
 */
