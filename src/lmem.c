/*
** $Id: lmem.c,v 1.70.1.1 2007/12/27 13:02:25 roberto Exp $
** Interface to Memory Manager
** See Copyright Notice in lua.h
*/


#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define lmem_c
#define LUA_CORE

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"

/*
** About the realloc function:
** void * frealloc (void *ud, void *ptr, size_t osize, size_t nsize);
** (`osize' is the old size, `nsize' is the new size)
**
** Lua ensures that (ptr == NULL) iff (osize == 0).
**
** * frealloc(ud, NULL, 0, x) creates a new block of size `x'
**
** * frealloc(ud, p, x, 0) frees the block `p'
** (in this specific case, frealloc must return NULL).
** particularly, frealloc(ud, NULL, 0, 0) does nothing
** (which is equivalent to free(NULL) in ANSI C)
**
** frealloc returns NULL if it cannot create or reallocate the area
** (any reallocation to an equal or smaller size cannot fail!)
*/

static void *default_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
  (void)ud;
  (void)osize;
  if (nsize == 0) {
    free(ptr);
    return NULL;
  }
  return realloc(ptr, nsize);
}

/** function to reallocate memory */
static lua_Alloc frealloc = default_alloc;
/** auxiliary data to `frealloc' */
static void *fud = NULL;



#define MINSIZEARRAY	4


void *luaM_growaux_ (lua_State *L, void *block, int *size, size_t size_elems,
                     int limit, const char *errormsg) {
  void *newblock;
  int newsize;
  if (*size >= limit/2) {  /* cannot double it? */
    if (*size >= limit)  /* cannot grow even a little? */
      luaG_runerror(L, errormsg);
    newsize = limit;  /* still have at least one free place */
  }
  else {
    newsize = (*size)*2;
    if (newsize < MINSIZEARRAY)
      newsize = MINSIZEARRAY;  /* minimum size */
  }
  newblock = luaM_reallocv(L, block, *size, newsize, size_elems);
  *size = newsize;  /* update only when everything else is OK */
  return newblock;
}


void *luaM_toobig (lua_State *L) {
  luaG_runerror(L, "memory allocation error: block too big");
  return NULL;  /* to avoid warnings */
}



/*
** generic allocation routine.
*/
void *luaM_realloc_ (lua_State *L, void *block, size_t osize, size_t nsize) {
  lua_assert((osize == 0) == (block == NULL));
  block = (*frealloc)(fud, block, osize, nsize);
  if (block == NULL && nsize > 0)
    luaD_throw(L, LUA_ERRMEM);
  lua_assert((nsize == 0) == (block == NULL));
  if (L) {
    global_State *g = G(L);
    g->totalbytes = (g->totalbytes - osize) + nsize;
  }
  return block;
}

#include <stdio.h>
void *luaM_newobj(lua_State *L, lu_byte tt)
{
  GCObject *o;
  switch (tt) {
#define NEWIMPL(a, b) \
    case a: \
      o = luaM_malloc(L, sizeof(b)); \
      memset(o, 0, sizeof(b)); \
      o->gch.tt = a; \
      return o
    NEWIMPL(LUA_TUPVAL, UpVal);
    NEWIMPL(LUA_TPROTO, Proto);
    NEWIMPL(LUA_TTABLE, Table);
    NEWIMPL(LUA_TGLOBAL, global_State);
    NEWIMPL(LUA_TTHREAD, lua_State);
    default:
      printf("unhandled tt=%d\n", tt);
      luaD_throw(L, LUA_ERRMEM);
      return NULL;
  }
}

void *luaM_newobjv(lua_State *L, lu_byte tt, size_t size)
{
  GCObject *o;
  switch (tt) {
#undef NEWIMPL
#define NEWIMPL(a, b) \
    case a: \
      o = luaM_malloc(L, size); \
      memset(o, 0, size); \
      o->gch.tt = a; \
      return o
    NEWIMPL(LUA_TFUNCTION, Closure);
    default:
      printf("unhandled tt=%d\n", tt);
      luaD_throw(L, LUA_ERRMEM);
      return NULL;
  }
}

pthread_key_t luai_tls_key;
static pthread_once_t once_control = PTHREAD_ONCE_INIT;

static void tls_dtor(void *ptr)
{
  os_State *pt = ptr;
  luaZ_freebuffer(NULL, &pt->buff);
}

static void do_init(void)
{
  pthread_key_create(&luai_tls_key, tls_dtor);
}

void lua_initialize(void)
{
  pthread_once(&once_control, do_init);
}

os_State *luaM_init_pt(void)
{
  os_State *pt;

  pt = calloc(1, sizeof(*pt));
  pthread_mutex_init(&pt->handshake, NULL);

  pthread_setspecific(luai_tls_key, pt);
  luaZ_initbuffer(NULL, &pt->buff);

  return pt;
}

LUA_API lua_Alloc lua_getallocf (lua_State *L, void **ud)
{
  return frealloc;
}

LUA_API void lua_setallocf (lua_State *L, lua_Alloc f, void *ud)
{
  if (L) {
    /* no-op */
  } else {
    frealloc = f;
    fud = ud;
  }
}



/* vim:ts=2:sw=2:et:
 */
