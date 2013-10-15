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

static void *default_alloc(void *ud, enum lua_memtype objtype, void *ptr,
  size_t osize, size_t nsize)
{
  (void)ud;
  (void)osize;
  (void)objtype;
  if (nsize == 0) {
    free(ptr);
    return NULL;
  }
  return realloc(ptr, nsize);
}

#define MINSIZEARRAY	4

void *luaM_growaux_(lua_State *L, enum lua_memtype objtype, void *block,
    int *size, size_t size_elems, int limit, const char *errormsg)
{
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
  newblock = luaM_reallocv(L, objtype, block, *size, newsize, size_elems);
  *size = newsize;  /* update only when everything else is OK */
  return newblock;
}


void *luaM_toobig (lua_State *L) {
  luaG_runerror(L, "memory allocation error: block too big");
  return NULL;  /* to avoid warnings */
}

static inline void *call_allocator(lua_State *L, enum lua_memtype objtype,
  void *block, size_t oldsize, size_t size)
{
  void *res;
  int64_t delta;
  uint32_t vers;
  int over_limit;

  delta = (int64_t)size - (int64_t)oldsize;

  /* first see if this allocation will even be allowed */
  if (L->mem.limit > 0 && delta > 0) {
    do {
      vers = ck_sequence_read_begin(&L->memlock);
      over_limit = (L->mem.bytes + delta > L->mem.limit);
    } while (ck_sequence_read_retry(&L->memlock, vers));

    if (over_limit) {
      /* throw an error, but let the error handler allocate
         memory otherwise we'll get into an infinite loop */
      L->mem.limit = 0;
      lua_pushstring(L, "lua thread memory limit");
      lua_error(L);
      return NULL;
    }
  }

  res = G(L)->alloc(G(L)->allocdata, objtype, block, oldsize, size);

  /* metrics for local collection */
  L->gcestimate += delta;

  ck_sequence_write_begin(&L->memlock);

  if (objtype < LUA_MEM__VSIZE) {
    /* fixed size allocs */
    L->mem.allocs += size ? 1 : -1;
    L->memtype[objtype].allocs += size ? 1 : -1;
  }

  L->mem.bytes += delta;
  L->memtype[objtype].bytes += delta;
  ck_sequence_write_end(&L->memlock);

  return res;
}


/*
** generic allocation routine.
*/
void *luaM_realloc_ (lua_State *L, enum lua_memtype objtype, void *block,
  size_t osize, size_t nsize)
{
  lua_assert((osize == 0) == (block == NULL));
  block = call_allocator(L, objtype, block, osize, nsize);
  if (block == NULL && nsize > 0)
    luaD_throw(L, LUA_ERRMEM);
  lua_assert((nsize == 0) == (block == NULL));
  return block;
}

void *luaM_realloc(lua_State *L, enum lua_memtype objtype,
  void *block, size_t oldsize, size_t size)
{
  return call_allocator(L, objtype, block, oldsize, size);
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
  struct lua_StateParams p;
  lua_State *L;

  memset(&p, 0, sizeof(p));
  p.allocfunc = default_alloc;

  L = lua_newglobalstate(&p);

  if (L) lua_atpanic(L, &panic);
  return L;
}

/* vim:ts=2:sw=2:et:
 */
