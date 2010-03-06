/*
** $Id: lfunc.c,v 2.12.1.2 2007/12/28 14:58:43 roberto Exp $
** Auxiliary functions to manipulate prototypes and closures
** See Copyright Notice in lua.h
*/

#define lfunc_c
#define LUA_CORE

#include "thrlua.h"

Closure *luaF_newCclosure (lua_State *L, int nelems, Table *e) {
  Closure *c = luaC_newobjv(G(L), LUA_TFUNCTION, sizeCclosure(nelems));
//  luaC_link(L, obj2gco(c), LUA_TFUNCTION);
  c->c.isC = 1;
  c->c.env = &e->gch;
  c->c.nupvalues = cast_byte(nelems);
  return c;
}


Closure *luaF_newLclosure (lua_State *L, int nelems, Table *e) {
  Closure *c = luaC_newobjv(G(L), LUA_TFUNCTION, sizeLclosure(nelems));
//  luaC_link(L, obj2gco(c), LUA_TFUNCTION);
  c->l.isC = 0;
  c->l.env = &e->gch;
  c->l.nupvalues = cast_byte(nelems);
  while (nelems--) c->l.upvals[nelems] = NULL;
  return c;
}


UpVal *luaF_newupval (lua_State *L) {
  UpVal *uv = luaC_newobj(G(L), LUA_TUPVAL);
  uv->v = &uv->u.value;
  setnilvalue(uv->v);
  return uv;
}

#if DEBUG_UPVAL

static void dumpopenupvals(lua_State *L)
{
  UpVal *u;
  printf("Open upvals are now:\n");
  for (u = L->openupval.u.l.next; u != &L->openupval; u = u->u.l.next) {
    lua_assert(u->u.l.prev->u.l.next == u);
    lua_assert(u->u.l.next == NULL || u->u.l.next->u.l.prev == u);
    printf("  upval=%p level=%p\n", u, u->v);
    lua_assert(u->u.l.prev == &L->openupval || u->u.l.prev->v >= u->v);
  }
}
#endif

UpVal *luaF_findupval (lua_State *L, StkId level) {
  global_State *g = G(L);
  UpVal *p;
  UpVal *uv;

#if DEBUG_UPVAL
printf("Looking for upval level %p\n", level);
#endif

  /* we keep the open upvalue list in descending value order.
   * it only contains open upvalues, which by definition refer
   * to items on the stack of the L state.
   *
   * We keep the list using a circular buffer so that an unlink
   * triggered without access to L can occur without having access
   * to L.  The sentinel in the list has a 0 level.
   *
   * We need to find a pre-existing UpVal object that matches
   * the requested stack position (level), or find an insertion
   * point so that we can create one */

  p = L->openupval.u.l.next;
  while (1) {
    lua_assert(p->v != &p->u.value);
    if (p->v == level) {
#if DEBUG_UPVAL
      printf("Found upval %p level %p\n", p, p->v);
#endif
      return p;
    }
    if (p->v < level || p->v == 0) {
      /* we need to insert before this item */
      break;
    }
    p = p->u.l.next;
  }

#if DEBUG_UPVAL
printf("Need to create, p is %p %s\n", p, p == &L->openupval ? "sentinel" : "upval");
#endif
  /* not found: create a new one */
  uv = luaC_newobj(G(L), LUA_TUPVAL);
  uv->v = level;  /* current value lives in the stack */

  lua_assert(p != NULL);

  uv->u.l.next = p;
  uv->u.l.prev = p->u.l.prev;
  p->u.l.prev = uv;
  if (uv->u.l.prev) {
    uv->u.l.prev->u.l.next = uv;
  }

  lua_assert(uv->u.l.prev == NULL || uv->u.l.prev->u.l.next == uv);
  lua_assert(uv->u.l.next == NULL || uv->u.l.next->u.l.prev == uv);
#if DEBUG_UPVAL
  printf("made new upval %p for level %p\n", uv, level);
  dumpopenupvals(L);
#endif
  return uv;
}


static void unlinkupval (UpVal *uv) {
  /* remove from `uvhead' list */
#if DEBUG_UPVAL
  printf("unlinking upval=%p level=%p %s\n",
    uv, uv->v, uv->v == &uv->u.value ? "closed" : "open");
#endif
  if (uv->u.l.next) {
    lua_assert(uv->u.l.next->u.l.prev == uv);
    uv->u.l.next->u.l.prev = uv->u.l.prev;
  }
  if (uv->u.l.prev) {
    lua_assert(uv->u.l.prev->u.l.next == uv);
    uv->u.l.prev->u.l.next = uv->u.l.next;
  }
}


void luaF_freeupval (lua_State *L, UpVal *uv) {
  if (uv->v != &uv->u.value)  /* is it open? */
    unlinkupval(uv);  /* remove from open list */
  luaM_free(L, uv);  /* free upvalue */
}


void luaF_close (lua_State *L, StkId level) {
  UpVal *uv;
  global_State *g = G(L);
#if DEBUG_UPVAL
  printf("close upval >= level %p\n", level);
#endif
  while (L->openupval.u.l.next != &L->openupval &&
      (uv = L->openupval.u.l.next)->v >= level) {
    lua_assert(uv->v != &uv->u.value);
//    L->openupval = uv->u.l.next;  /* remove from `open' list */
    unlinkupval(uv);

    /* copy value into the upval itself */
    luaC_writebarriervv(G(L), &uv->gch, &uv->u.value, uv->v);
    uv->v = &uv->u.value;  /* now current value lives here */
//      setobj(L, &uv->u.value, uv->v);
//      luaC_linkupval(L, uv);  /* link upvalue into `gcroot' list */
  }
#if DEBUG_UPVAL
  dumpopenupvals(L);
#endif
}


Proto *luaF_newproto (lua_State *L) {
  Proto *f = luaC_newobj(G(L), LUA_TPROTO);
//  luaC_link(L, obj2gco(f), LUA_TPROTO);
  f->k = NULL;
  f->sizek = 0;
  f->p = NULL;
  f->sizep = 0;
  f->code = NULL;
  f->sizecode = 0;
  f->sizelineinfo = 0;
  f->sizeupvalues = 0;
  f->nups = 0;
  f->upvalues = NULL;
  f->numparams = 0;
  f->is_vararg = 0;
  f->maxstacksize = 0;
  f->lineinfo = NULL;
  f->sizelocvars = 0;
  f->locvars = NULL;
  f->linedefined = 0;
  f->lastlinedefined = 0;
  f->source = NULL;
  return f;
}


void luaF_freeproto (lua_State *L, Proto *f) {
  luaM_freearray(L, f->code, f->sizecode, Instruction);
  luaM_freearray(L, f->p, f->sizep, Proto *);
  luaM_freearray(L, f->k, f->sizek, TValue);
  luaM_freearray(L, f->lineinfo, f->sizelineinfo, int);
  luaM_freearray(L, f->locvars, f->sizelocvars, struct LocVar);
  luaM_freearray(L, f->upvalues, f->sizeupvalues, TString *);
  luaM_free(L, f);
}


void luaF_freeclosure (lua_State *L, Closure *c) {
  int size = (c->c.isC) ? sizeCclosure(c->c.nupvalues) :
                          sizeLclosure(c->l.nupvalues);
  luaM_freemem(L, c, size);
}


/*
** Look for n-th local variable at line `line' in function `func'.
** Returns NULL if not found.
*/
const char *luaF_getlocalname (const Proto *f, int local_number, int pc) {
  int i;
  for (i = 0; i<f->sizelocvars && f->locvars[i].startpc <= pc; i++) {
    if (pc < f->locvars[i].endpc) {  /* is variable active? */
      local_number--;
      if (local_number == 0)
        return getstr(f->locvars[i].varname);
    }
  }
  return NULL;  /* not found */
}

/* vim:ts=2:sw=2:et:
 */
