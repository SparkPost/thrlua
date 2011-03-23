/*
** $Id: lmem.h,v 1.31.1.1 2007/12/27 13:02:25 roberto Exp $
** Interface to Memory Manager
** See Copyright Notice in lua.h
*/

#ifndef lmem_h
#define lmem_h

#define MEMERRMSG	"not enough memory"


#define luaM_reallocv(L,objtype, b,on,n,e) \
	((cast(size_t, (n)+1) <= MAX_SIZET/(e)) ?  /* +1 to avoid warnings */ \
		luaM_realloc_(L, (objtype), (b), (on)*(e), (n)*(e)) : \
		luaM_toobig(L))

#define luaM_freemem(L, objtype, b, s)	luaM_realloc_(L, objtype, (b), (s), 0)
#define luaM_freememG(g, objtype, b, s)	luaM_reallocG(g, objtype, (b), (s), 0)
#define luaM_free(L, objtype, b)		luaM_realloc_(L, objtype, (b), sizeof(*(b)), 0)
#define luaM_freeG(g, objtype, b)		luaM_reallocG(g, objtype, (b), sizeof(*(b)), 0)
#define luaM_freearray(L, objtype, b, n, t)   luaM_reallocv(L, objtype, (b), n, 0, sizeof(t))
#define luaM_freearrayG(g, objtype, b, n, t)   luaM_reallocG(g, objtype, (b), n * sizeof(t), 0)

#define luaM_malloc(L,objtype,t)	luaM_realloc_(L, objtype, NULL, 0, (t))
#define luaM_mallocG(g,objtype,t)	luaM_reallocG(g, objtype, NULL, 0, (t))
#define luaM_new(L,t)		cast(t *, luaM_malloc(L, sizeof(t)))
#define luaM_newG(g,t)		cast(t *, luaM_mallocG(g, sizeof(t)))
#define luaM_newvector(L,objtype,n,t) \
		cast(t *, luaM_reallocv(L, objtype, NULL, 0, n, sizeof(t)))
#define luaM_newvectorG(g,objtype,n,t) \
		cast(t *, luaM_reallocG(g, objtype, NULL, 0, n, sizeof(t)))

#define luaM_growvector(L, objtype, v,nelems,size,t,limit,e) \
          if ((nelems)+1 > (size)) \
            ((v)=cast(t *, luaM_growaux_(L,objtype, v,&(size),sizeof(t),limit,e)))

#define luaM_reallocvector(L, objtype, v,oldn,n,t) \
   ((v)=cast(t *, luaM_reallocv(L, objtype, v, oldn, n, sizeof(t))))
#define luaM_reallocvectorG(g, objtype, v,oldn,n,t) \
   ((v)=cast(t *, luaM_reallocG(g, objtype, v, oldn * sizeof(t), n * sizeof(t))))



LUAI_FUNC void *luaM_realloc_ (lua_State *L, enum lua_memtype objtype,
	void *block, size_t oldsize, size_t size);
LUAI_FUNC void *luaM_toobig (lua_State *L);
LUAI_FUNC void *luaM_growaux_ (lua_State *L, enum lua_memtype objtype,
	void *block, int *size, size_t size_elem, int limit, const char *errormsg);
LUAI_FUNC void *luaM_reallocG(global_State *g, enum lua_memtype objtype, void *block,
  size_t oldsize, size_t size);

#endif

