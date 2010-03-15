/*
** $Id: lmem.c,v 1.70.1.1 2007/12/27 13:02:25 roberto Exp $
** Interface to Memory Manager
** See Copyright Notice in lua.h
*/

#define lmem_c
#define LUA_CORE

#include "thrlua.h"

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
  return block;
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

void *luaM_reallocG(global_State *g, void *block,
  size_t oldsize, size_t size)
{
  if (!g->alloc) {
    g->alloc = default_alloc;
  }
  return g->alloc(g->allocdata, block, oldsize, size);
}

static int panic (lua_State *L)
{
  const char *err;

  (void)L;  /* to avoid warnings */
  err = lua_tostring(L, -1);
#if HAVE_VALGRIND
  VALGRIND_PRINTF_BACKTRACE("unprotected error in call to Lua API (%s)\n",
    err);
#endif
  fprintf(stderr, "PANIC: unprotected error in call to Lua API (%s)\n", err);
  return 0;
}


LUALIB_API lua_State *luaL_newstate (void) {
  lua_State *L = lua_newstate(default_alloc, NULL);
  if (L) lua_atpanic(L, &panic);
  return L;
}

/* vim:ts=2:sw=2:et:
 */
