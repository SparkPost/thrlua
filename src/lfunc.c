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


UpVal *luaF_findupval (lua_State *L, StkId level) {
  global_State *g = G(L);
  UpVal *p;
  UpVal *uv;

  for (p = L->openupval.u.l.next;
      p != &L->openupval && p->v >= level;
      p = p->u.l.next) {
    lua_assert(p->v != &p->u.value);
    if (p->v == level) {  /* found a corresponding upvalue? */
      return p;
    }
  }

  /* not found: create a new one */
  uv = luaC_newobj(G(L), LUA_TUPVAL);
  uv->v = level;  /* current value lives in the stack */
  uv->u.l.next = p;
  uv->u.l.prev = uv->u.l.next->u.l.prev;
  uv->u.l.next->u.l.prev = uv;

  lua_assert(uv->u.l.next->u.l.prev == uv && uv->u.l.prev->u.l.next == uv);
  return uv;
}


static void unlinkupval (UpVal *uv) {
  lua_assert(uv->u.l.next->u.l.prev == uv && uv->u.l.prev->u.l.next == uv);
  uv->u.l.next->u.l.prev = uv->u.l.prev;  /* remove from `uvhead' list */
  uv->u.l.prev->u.l.next = uv->u.l.next;
}


void luaF_freeupval (lua_State *L, UpVal *uv) {
  if (uv->v != &uv->u.value)  /* is it open? */
    unlinkupval(uv);  /* remove from open list */
  luaM_free(L, uv);  /* free upvalue */
}


void luaF_close (lua_State *L, StkId level) {
  UpVal *uv;
  global_State *g = G(L);
  while (L->openupval.u.l.next != &L->openupval &&
      (uv = L->openupval.u.l.next)->v >= level) {
    lua_assert(uv->v != &uv->u.value);
//    L->openupval = uv->u.l.next;  /* remove from `open' list */
    unlinkupval(uv);

    /* copy value into the upval itself */
    luaC_writebarriervv(G(L), &uv->gch, &uv->u.value, uv->v);
//      setobj(L, &uv->u.value, uv->v);
//      uv->v = &uv->u.value;  /* now current value lives here */
//      luaC_linkupval(L, uv);  /* link upvalue into `gcroot' list */
  }
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
