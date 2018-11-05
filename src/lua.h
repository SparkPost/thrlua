/*
** $Id: lua.h,v 1.218.1.5 2008/08/06 13:30:12 roberto Exp $
** Lua - An Extensible Extension Language
** Lua.org, PUC-Rio, Brazil (http://www.lua.org)
** See Copyright Notice at the end of this file
*/


#ifndef lua_h
#define lua_h

#include <stdarg.h>
#include <stddef.h>
#include "luaconf.h"

#if __GNUC__ > 2 || __GNUC__ == 2 && __GNUC_MINOR__ >= 5
# define LUA_NORETURN __attribute__((__noreturn__))
#else
# define LUA_NORETURN /* nothing */
#endif

#define LUA_VERSION	"thrLua 5.1"
#define LUA_RELEASE	"thrLua " PACKAGE_VERSION
#define LUA_VERSION_NUM	501
#define LUA_COPYRIGHT	"Copyright (C) 1994-2008 Lua.org, PUC-Rio\nCopyright (C) 2008-2012 Message Systems, Inc"
#define LUA_AUTHORS 	"R. Ierusalimschy, L. H. de Figueiredo & W. Celes"


/* mark for precompiled code (`<esc>Lua') */
#define	LUA_SIGNATURE	"\033Lua"

/* option for multiple returns in `lua_pcall' and `lua_call' */
#define LUA_MULTRET	(-1)


/*
** pseudo-indices
*/
#define LUA_REGISTRYINDEX	(-10000)
#define LUA_ENVIRONINDEX	(-10001)
#define LUA_TLSINDEX      (-10002)
#define LUA_OSTLSINDEX    (-10003)
#define LUA_GLOBALSINDEX	(-10004)
#define lua_upvalueindex(i)	(LUA_GLOBALSINDEX-(i))


/* thread status; 0 is OK */
#define LUA_OK 0
#define LUA_YIELD	1
#define LUA_ERRRUN	2
#define LUA_ERRSYNTAX	3
#define LUA_ERRMEM	4
#define LUA_ERRERR	5
#define LUA_SUSPEND	6


typedef struct lua_State lua_State;

typedef int (*lua_CFunction) (lua_State *L);


/*
** functions that read/write blocks when loading/dumping Lua chunks
*/
typedef const char * (*lua_Reader) (lua_State *L, void *ud, size_t *sz);

typedef int (*lua_Writer) (lua_State *L, const void* p, size_t sz, void* ud);


/*
** prototype for memory-allocation functions
*/
#if 0
typedef void *(*lua_Alloc) (void *ud, void *ptr, size_t osize, size_t nsize);
#endif

enum lua_memtype {
  LUA_MEM_STRING_TABLE_NODE, /* fixed size */
  LUA_MEM_TABLE, /* fixed size */
  LUA_MEM_GLOBAL_STATE, /* fixed size */
  LUA_MEM_THREAD, /* fixed size */
  LUA_MEM_UPVAL, /* fixed size */
  LUA_MEM_PROTO, /* fixed size */
  LUA_MEM__VSIZE, /* symbolic; indicates start of variable sized types */
  LUA_MEM_STRING_TABLE = LUA_MEM__VSIZE,
  LUA_MEM_FUNCTION,
  LUA_MEM_STRING,
  LUA_MEM_USERDATA,
  LUA_MEM_TABLE_NODES,
  LUA_MEM_ZBUF,
  LUA_MEM_STACK,
  LUA_MEM_CALLINFO,
  LUA_MEM_PROTO_DATA,
  LUA_MEM__MAX /* must be last */
};

struct lua_memtype_alloc_info {
  int64_t bytes;
  int64_t allocs;
};

struct lua_mem_usage_data {
  struct lua_memtype_alloc_info global;
  struct lua_memtype_alloc_info bytype[LUA_MEM__MAX];
};

enum lua_mem_info_scope {
  LUA_MEM_SCOPE_LOCAL,
  LUA_MEM_SCOPE_GLOBAL,
};
/* returns memory used by the lua runtime */
void lua_mem_get_usage(lua_State *L, struct lua_mem_usage_data *data,
  enum lua_mem_info_scope scope);

typedef void *(*lua_Alloc2)(void *ud, enum lua_memtype objtype,
  void *ptr, size_t osize, size_t nsize);

/*
** basic types
*/
#define LUA_TNONE		(-1)

#define LUA_TNIL		0
#define LUA_TBOOLEAN		1
#define LUA_TLIGHTUSERDATA	2
#define LUA_TNUMBER		3
#define LUA_TSTRING		4
#define LUA_TTABLE		5
#define LUA_TFUNCTION		6
#define LUA_TUSERDATA		7
#define LUA_TTHREAD		8



/* minimum Lua stack available to a C function */
#define LUA_MINSTACK	20


/*
** generic extra include file
*/
#if defined(LUA_USER_H)
#include LUA_USER_H
#endif


/* type of numbers in Lua */
typedef LUA_NUMBER lua_Number;


/* type for integer functions */
typedef LUA_INTEGER lua_Integer;

LUA_API void lua_initialize(void);


#if 0
/*
** state manipulation
*/
LUA_API lua_State *(lua_newstate) (lua_Alloc f, void *ud);
#endif

struct lua_StateParams {
  /** specify the allocator */
  lua_Alloc2 allocfunc;
  /** specify the allocator data */
  void *allocdata;
  /** size of additional space to allocate after each lua_State.
   * An application can use lua_get_extra to obtain a pointer to this
   * extra space */
  unsigned int extraspace;
  /** called when each lua_State is allocated */
  void (*on_state_create)(lua_State *L);
  /** called when each lua_State is finalized */
  void (*on_state_finalize)(lua_State *L);
  /** called when the runtime needs to dlopen and dlsym.
   * Returns:
   * LUA_LOADFUNC_SUCCESS and pushes a C closure on the stack on success.
   * LUA_LOADFUNC_ERR_LIB and pushes errmsg on stack on error opening lib.
   * LUA_LOADFUNC_ERR_FUNC and pushes errmsg on stack on error finding func.
   */
  int (*loadfunc)(lua_State *L, const char *path, const char *sym);
#define LUA_LOADFUNC_SUCCESS 0
#define LUA_LOADFUNC_ERR_LIB 1
#define LUA_LOADFUNC_ERR_FUNC 2
  /** A logging callback */
  void (*logfunc)(int level, const char *fmt, ...);
};

LUA_API lua_State *(lua_newglobalstate)(struct lua_StateParams *p);
LUA_API void *(lua_get_extra)(lua_State *L);

LUA_API void       (lua_close) (lua_State *L);
LUA_API lua_State *(lua_newthread) (lua_State *L);

/** Create a new lua_State sharing globals and so forth from the
 * provided lua_State.  The returned lua_State is pinned with a refcount
 * of 1, and is NOT referenced from the stack of original.
 * The caller MUST use lua_delrefthread to dispose of the thread when it
 * is no longer needed.
 */
LUA_API lua_State *(lua_newthreadref)(lua_State *L);

/** Add a reference to a lua_State.
 * While the reference count is greater than zero, a lua_State will not
 * be collected.
 * Use lua_delrefthread to release a reference.
 **/
LUA_API void      (lua_addrefthread)(lua_State *L);

/** Pin a reference to the object at the specified stack index.
 *
 * If the specified stack index is not a collectable type, returns
 * NULL.
 *
 * Otherwise, returns a local reference to the object, preventing it
 * from being collected until all outstanding pinned local references
 * are released.
 *
 * When you have finished using the reference, you must unpin it using
 * lua_delrefobj().
 */
LUA_API void *lua_addrefobj(lua_State *L, int index);

/** Un-pin a reference from an object returned from lua_addrefobj.
 * After you have un-pinned the reference, you must assume that it
 * is no longer valid to pass to any other functions.
 */
LUA_API void lua_delrefobj(lua_State *L, void *ref);

/** Push a reference on to the stack.
 *
 * The reference must be a valid reference returned from lua_addrefobj.
 */
LUA_API void lua_pushobjref(lua_State *L, void *ref);


/** Delete a reference from a lua_State.
 * Since a lua_State may own objects with outstanding references, you
 * must provide a reference to another lua_State that will take ownership
 * of those objects when the reference count falls to zero.
 * If you pass inheritor == NULL, then the main thread will be used.
 * The inheriting thread will be locked while ownership is transferred;
 * the inheriting thread must therefore be unlocked for this to succeed
 * without deadlock. */
LUA_API void      (lua_delrefthread)(lua_State *L, lua_State *inheritor);

LUA_API lua_CFunction (lua_atpanic) (lua_State *L, lua_CFunction panicf);


/*
** basic stack manipulation
*/
LUA_API int   (lua_gettop) (lua_State *L);
LUA_API void  (lua_settop) (lua_State *L, int idx);
LUA_API void  (lua_pushvalue) (lua_State *L, int idx);
LUA_API void  (lua_remove) (lua_State *L, int idx);
LUA_API void  (lua_insert) (lua_State *L, int idx);
LUA_API void  (lua_replace) (lua_State *L, int idx);
LUA_API int   (lua_checkstack) (lua_State *L, int sz);

LUA_API void  (lua_xmove) (lua_State *from, lua_State *to, int n);


/*
** access functions (stack -> C)
*/

LUA_API int             (lua_isnumber) (lua_State *L, int idx);
LUA_API int             (lua_isstring) (lua_State *L, int idx);
LUA_API int             (lua_iscfunction) (lua_State *L, int idx);
LUA_API int             (lua_isuserdata) (lua_State *L, int idx);
LUA_API int             (lua_type) (lua_State *L, int idx);
LUA_API const char     *(lua_typename) (lua_State *L, int tp);

LUA_API int            (lua_equal) (lua_State *L, int idx1, int idx2);
LUA_API int            (lua_rawequal) (lua_State *L, int idx1, int idx2);
LUA_API int            (lua_lessthan) (lua_State *L, int idx1, int idx2);

LUA_API lua_Number      (lua_tonumber) (lua_State *L, int idx);
LUA_API lua_Integer     (lua_tointeger) (lua_State *L, int idx);
LUA_API int             (lua_toboolean) (lua_State *L, int idx);
LUA_API const char     *(lua_tolstring) (lua_State *L, int idx, size_t *len);
LUA_API size_t          (lua_objlen) (lua_State *L, int idx);
LUA_API lua_CFunction   (lua_tocfunction) (lua_State *L, int idx);
LUA_API void	       *(lua_touserdata) (lua_State *L, int idx);
LUA_API lua_State      *(lua_tothread) (lua_State *L, int idx);
LUA_API const void     *(lua_topointer) (lua_State *L, int idx);


/*
** push functions (C -> stack)
*/
LUA_API void  (lua_pushnil) (lua_State *L);
LUA_API void  (lua_pushnumber) (lua_State *L, lua_Number n);
LUA_API void  (lua_pushinteger) (lua_State *L, lua_Integer n);
LUA_API void  (lua_pushlstring) (lua_State *L, const char *s, size_t l);
LUA_API void  (lua_pushstring) (lua_State *L, const char *s);
LUA_API const char *(lua_pushvfstring) (lua_State *L, const char *fmt,
                                                      va_list argp);
LUA_API const char *(lua_pushfstring) (lua_State *L, const char *fmt, ...);
LUA_API void  (lua_pushcclosure) (lua_State *L, lua_CFunction fn, int n);
LUA_API void  (lua_pushcclosure2) (lua_State *L, const char *name, lua_CFunction fn, int n);
LUA_API void  (lua_pushboolean) (lua_State *L, int b);
LUA_API void  (lua_pushlightuserdata) (lua_State *L, void *p);
LUA_API int   (lua_pushthread) (lua_State *L);

/*
** get functions (Lua -> stack)
*/
LUA_API void  (lua_gettable) (lua_State *L, int idx);
LUA_API void  (lua_getfield) (lua_State *L, int idx, const char *k);
LUA_API void  (lua_rawget) (lua_State *L, int idx);
LUA_API void  (lua_rawgeti) (lua_State *L, int idx, int n);
LUA_API void  (lua_createtable) (lua_State *L, int narr, int nrec);
LUA_API void *(lua_newuserdata) (lua_State *L, size_t sz);
LUA_API int   (lua_getmetatable) (lua_State *L, int objindex);
LUA_API void  (lua_getfenv) (lua_State *L, int idx);


/*
** set functions (stack -> Lua)
*/
LUA_API void  (lua_settable) (lua_State *L, int idx);
LUA_API void  (lua_setfield) (lua_State *L, int idx, const char *k);
LUA_API void  (lua_rawset) (lua_State *L, int idx);
LUA_API void  (lua_rawseti) (lua_State *L, int idx, int n);
LUA_API int   (lua_setmetatable) (lua_State *L, int objindex);
LUA_API int   (lua_setfenv) (lua_State *L, int idx);


/*
** `load' and `call' functions (load and run Lua code)
*/
LUA_API void  (lua_call) (lua_State *L, int nargs, int nresults);
LUA_API int   (lua_pcall) (lua_State *L, int nargs, int nresults, int errfunc);
LUA_API int   (lua_cpcall) (lua_State *L, lua_CFunction func, void *ud);
LUA_API int   (lua_load) (lua_State *L, lua_Reader reader, void *dt,
                                        const char *chunkname);

LUA_API int (lua_dump) (lua_State *L, lua_Writer writer, void *data);


/*
** coroutine functions
*/
LUA_API int  (lua_yield) (lua_State *L, int nresults);
LUA_API int  (lua_resume) (lua_State *L, int narg);
LUA_API int  (lua_status) (lua_State *L);

/*
** garbage-collection function and options
*/

#define LUA_GCSTOP		0
#define LUA_GCRESTART		1
#define LUA_GCCOLLECT		2
#define LUA_GCCOUNT		3
#define LUA_GCCOUNTB		4
#define LUA_GCSTEP		5
#define LUA_GCSETPAUSE		6
#define LUA_GCSETSTEPMUL	7
/** trigger a global trace and a local collection */
#define LUA_GCGLOBALTRACE 8
/** set a different global trace threshold */
#define LUA_GCSETGLOBALTRACE 9
/** set a different global trace xref threshold */
#define LUA_GCSETGLOBALTRACEXREF 10
#define LUA_GCSETINTERNCLEANUPMAX 11

LUA_API int (lua_gc) (lua_State *L, int what, int data);


/*
** miscellaneous functions
*/

LUA_API int   (lua_error) (lua_State *L);

LUA_API int   (lua_next) (lua_State *L, int idx);

LUA_API void  (lua_concat) (lua_State *L, int n);

/* timing stats for block_mutators() */
LUA_API struct timeval (lua_get_mutator_wait_start) (lua_State *L);
LUA_API struct timeval (lua_get_mutator_wait_end) (lua_State *L);

#if 0
LUA_API lua_Alloc (lua_getallocf) (lua_State *L, void **ud);
LUA_API void lua_setallocf (lua_State *L, lua_Alloc f, void *ud);
#endif



/* 
** ===============================================================
** some useful macros
** ===============================================================
*/

#define lua_pop(L,n)		lua_settop(L, -(n)-1)

#define lua_newtable(L)		lua_createtable(L, 0, 0)

#define lua_register(L,n,f) (lua_pushcfunction(L, (f)), lua_setglobal(L, (n)))

#define lua_pushcfunction(L,f)	lua_pushcclosure(L, (f), 0)

#define lua_strlen(L,i)		lua_objlen(L, (i))

#define lua_isfunction(L,n)	(lua_type(L, (n)) == LUA_TFUNCTION)
#define lua_istable(L,n)	(lua_type(L, (n)) == LUA_TTABLE)
#define lua_islightuserdata(L,n)	(lua_type(L, (n)) == LUA_TLIGHTUSERDATA)
#define lua_isnil(L,n)		(lua_type(L, (n)) == LUA_TNIL)
#define lua_isboolean(L,n)	(lua_type(L, (n)) == LUA_TBOOLEAN)
#define lua_isthread(L,n)	(lua_type(L, (n)) == LUA_TTHREAD)
#define lua_isnone(L,n)		(lua_type(L, (n)) == LUA_TNONE)
#define lua_isnoneornil(L, n)	(lua_type(L, (n)) <= 0)

#define lua_pushliteral(L, s)	\
	lua_pushlstring(L, "" s, (sizeof(s)/sizeof(char))-1)

#define lua_setglobal(L,s)	lua_setfield(L, LUA_GLOBALSINDEX, (s))
#define lua_getglobal(L,s)	lua_getfield(L, LUA_GLOBALSINDEX, (s))

#define lua_tostring(L,i)	lua_tolstring(L, (i), NULL)



/*
** compatibility macros and functions
*/

#define lua_open()	luaL_newstate()

#define lua_getregistry(L)	lua_pushvalue(L, LUA_REGISTRYINDEX)

#define lua_getgccount(L)	lua_gc(L, LUA_GCCOUNT, 0)

#define lua_Chunkreader		lua_Reader
#define lua_Chunkwriter		lua_Writer


/* hack */
LUA_API void lua_setlevel	(lua_State *from, lua_State *to);


/*
** {======================================================================
** Debug API
** =======================================================================
*/


/*
** Event codes
*/
#define LUA_HOOKCALL	0
#define LUA_HOOKRET	1
#define LUA_HOOKLINE	2
#define LUA_HOOKCOUNT	3
#define LUA_HOOKTAILRET 4


/*
** Event masks
*/
#define LUA_MASKCALL	(1 << LUA_HOOKCALL)
#define LUA_MASKRET	(1 << LUA_HOOKRET)
#define LUA_MASKLINE	(1 << LUA_HOOKLINE)
#define LUA_MASKCOUNT	(1 << LUA_HOOKCOUNT)

typedef struct lua_Debug lua_Debug;  /* activation record */


/* Functions to be called by the debuger in specific events */
typedef void (*lua_Hook) (lua_State *L, lua_Debug *ar);


LUA_API int lua_getstack (lua_State *L, int level, lua_Debug *ar);
LUA_API int lua_getinfo (lua_State *L, const char *what, lua_Debug *ar);
LUA_API const char *lua_getlocal (lua_State *L, const lua_Debug *ar, int n);
LUA_API const char *lua_setlocal (lua_State *L, const lua_Debug *ar, int n);
LUA_API const char *lua_getupvalue (lua_State *L, int funcindex, int n);
LUA_API const char *lua_setupvalue (lua_State *L, int funcindex, int n);

LUA_API int lua_sethook (lua_State *L, lua_Hook func, int mask, int count);
LUA_API lua_Hook lua_gethook (lua_State *L);
LUA_API int lua_gethookmask (lua_State *L);
LUA_API int lua_gethookcount (lua_State *L);


struct lua_Debug {
  int event;
  const char *name;	/* (n) */
  const char *namewhat;	/* (n) `global', `local', `field', `method' */
  const char *what;	/* (S) `Lua', `C', `main', `tail' */
  const char *source;	/* (S) */
  int currentline;	/* (l) */
  int nups;		/* (u) number of upvalues */
  int linedefined;	/* (S) */
  int lastlinedefined;	/* (S) */
  char short_src[LUA_IDSIZE]; /* (S) */
  /* private part */
  int i_ci;  /* active function */
};

/* }====================================================================== */

/** A function to be called on resumption of a suspende script.
 *
 * ptr is the pointer passed along with the resume function to the
 * lua_suspend() call.
 *
 * The resume func is responsible for freeing up the ptr, if required.
 *
 * You should return one of LUA_SUSPEND, LUA_OK or LUA_ERRERR.
 */
typedef int (*lua_ResumeFunc)(lua_State *L, void *ptr);

/** Suspends script execution.
 *
 * yields the VM and arranges for an optional function to be invoked
 * on resume when lua_resume() is subsequently invoked.
 *
 * If func is NULL, then the script simply yields.
 *
 * The idiom is to return lua_suspend() from your lua_CFunction, without
 * modifying the state of the lua_State between the time of the call and
 * return from that function.
 *
 * Raises an error if it is not safe to yield; you can check for the safety
 * of this by calling lua_can_suspend().
 */
LUA_API int lua_suspend(lua_State *L, lua_ResumeFunc func, void *ptr);

LUA_API int lua_can_suspend(lua_State *L);

struct lua_Suspender {
  lua_ResumeFunc suspender;
  lua_ResumeFunc resumer;
  void *ptr;
};

/** Sets suspend and resume handlers.
 *
 * In order to run in a variety of environments with async or non-blocking
 * operation modes, thrlua provides the caller with a means for specifying
 * exactly how to perform a suspend and how to re-schedule the running of
 * the script when the external procedure completes.
 *
 * The suspender function is called internally as part of the lua_suspend()
 * implementation.
 *
 * The resumer function is called as part of the lua_arrange_resume()
 * implementation.
 *
 * This function copies the current suspender details into the old_suspender
 * structure (if it is not NULL) and then replaces the suspender details with
 * those in suspender.
 *
 * If suspender is NULL, the suspender details in L are cleared.
 *
 * If a suspender routine is registered in a lua_State, lua_resume() changes
 * its behavior such that a LUA_YIELD status discards the yielded values, and
 * a successful execution status returns a positive integer, 0 or higher, that
 * indicates the number of values returned by the completed call.
 */
LUA_API void lua_set_suspender(lua_State *L,
  const struct lua_Suspender *suspender,
  struct lua_Suspender *old_suspender);

/** Arranges for a script to resume running.
 *
 * A VM may have been previously suspended via the lua_suspend() call.  When
 * it is appropriate for that processing to continue, lua_arrange_resume()
 * should be invoked to arrange for the VM to run again.
 *
 * It is important to realize that the VM may not actually resume execution
 * as part of this call; it may simply be scheduled to resume at the next
 * convenient point.
 */
LUA_API int lua_arrange_resume(lua_State *L);

/** prevent access to this lua_State from another thread.
 * blocks until access is obtained */
LUA_API void lua_lock(lua_State *L);

/** release one level of lock from this lua_State */
LUA_API void lua_unlock(lua_State *L);

/******************************************************************************
* Copyright (C) 1994-2008 Lua.org, PUC-Rio.  All rights reserved.
* Copyright (C) 2008-2011 Message Systems, Inc.
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
******************************************************************************/


#endif

/* vim:ts=2:sw=2:et:
 */
