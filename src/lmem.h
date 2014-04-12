/*
** $Id: lmem.h,v 1.31.1.1 2007/12/27 13:02:25 roberto Exp $
** Interface to Memory Manager
** See Copyright Notice in lua.h
*/

#ifndef lmem_h
#define lmem_h

#include <sys/param.h>

#define MEMERRMSG	"not enough memory"


#define luaM_reallocv(L,objtype, b,on,n,e) \
	((cast(size_t, (n)+1) <= MAX_SIZET/(e)) ?  /* +1 to avoid warnings */ \
		luaM_realloc_(L, (objtype), (b), (on)*(e), (n)*(e)) : \
		luaM_toobig(L))

#define luaM_freemem(L, objtype, b, s)	luaM_realloc_(L, objtype, (b), (s), 0)
#define luaM_free(L, objtype, b)		luaM_realloc_(L, objtype, (b), sizeof(*(b)), 0)
#define luaM_freearray(L, objtype, b, n, t)   luaM_reallocv(L, objtype, (b), n, 0, sizeof(t))

#define luaM_malloc(L,objtype,t)	luaM_realloc_(L, objtype, NULL, 0, (t))
#define luaM_new(L,t)		cast(t *, luaM_malloc(L, sizeof(t)))
#define luaM_newvector(L,objtype,n,t) \
		cast(t *, luaM_reallocv(L, objtype, NULL, 0, n, sizeof(t)))

#define MINSIZEARRAY  4
#define luaM_growvector(L, objtype, v,nelems,size,t,limit,e) do {\
  if ((nelems)+1 > (size)) { \
    ((v)=cast(t *, luaM_growaux_(L,objtype, v,&(size),sizeof(t),limit,e))); \
  } \
} while(0)
		  

#define luaM_growvector_safe(L, objtype, v, nelems, size, t, limit, e, fixup) do { \
  if ((nelems)+1 > (size)) { \
    t *__newmem = NULL; \
    t *__oldmem = v; \
    size_t __newsize; \
    size_t __oldsize = (size); \
    if ((size) >= limit/2) {  /* cannot double it? */ \
      if ((size) >= (limit)) { /* cannot grow even a little? */ \
        luaG_runerror(L, e); \
      } \
      __newsize = (limit);  /* still have at least one free place */ \
    } \
    else { \
      __newsize = (size)*2; \
      if (__newsize < MINSIZEARRAY) { \
        __newsize = MINSIZEARRAY;  /* minimum size */ \
      } \
    } \
    __newmem = luaM_newvector(L, objtype, __newsize, t); \
    /* Cannot change the existing data structures without the collector \
     * blocked */ \
    luaC_blockcollector(L); \
    memcpy(__newmem, v, (size) * sizeof(t)); \
    v = __newmem; \
    size = __newsize; \
    fixup; \
    luaC_unblockcollector(L); \
    luaM_freearray(L, objtype, __oldmem, __oldsize, t); \
  } \
} while(0)

#define luaM_reallocvector(L, objtype, v,oldn,n,t) \
   ((v)=cast(t *, luaM_reallocv(L, objtype, v, oldn, n, sizeof(t))))

#define do_nothing do { } while(0)

#define luaM_reallocvector2(L, memtype, obj, size, newsize, objtype, fixup) do { \
  objtype* __newobj = NULL; \
  objtype* __oldobj = obj; \
  int __oldsize = size; \
  /* Allocate the new memory */ \
  __newobj = luaM_newvector(L, memtype, newsize, objtype); \
  /* Block the collector */ \
  luaC_blockcollector(L); \
  /* Copy the old memory to the new memory */ \
  memcpy(__newobj, __oldobj, MIN(__oldsize,newsize) * sizeof(objtype)); \
  obj = __newobj; \
  /* do whatever assignment needed for the new memory */ \
  fixup; \
  /* Unblock the collector */ \
  luaC_unblockcollector(L); \
  /* Free the old memory */ \
  if (__oldobj && __oldsize) { \
    luaM_freemem(L, memtype, __oldobj, __oldsize * sizeof(objtype)); \
  } \
} while(0)

LUAI_FUNC void *luaM_realloc_ (lua_State *L, enum lua_memtype objtype,
	void *block, size_t oldsize, size_t size);
LUAI_FUNC void *luaM_toobig (lua_State *L);
LUAI_FUNC void *luaM_realloc(lua_State *L, enum lua_memtype objtype,
	void *block, size_t oldsize, size_t size);
void *luaM_growaux_(lua_State *L, enum lua_memtype objtype, void *block,
    int *size, size_t size_elems, int limit, const char *errormsg);

#endif

