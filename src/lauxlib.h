/*
** $Id: lauxlib.h,v 1.88.1.1 2007/12/27 13:02:25 roberto Exp $
** Auxiliary functions for building Lua libraries
** See Copyright Notice in lua.h
*/


#ifndef lauxlib_h
#define lauxlib_h


#include <stddef.h>
#include <stdio.h>

#include "lua.h"


#if defined(LUA_COMPAT_GETN)
LUALIB_API int (luaL_getn) (lua_State *L, int t);
LUALIB_API void (luaL_setn) (lua_State *L, int t, int n);
#else
#define luaL_getn(L,i)          ((int)lua_objlen(L, i))
#define luaL_setn(L,i,j)        ((void)0)  /* no op! */
#endif

#if defined(LUA_COMPAT_OPENLIB)
#define luaI_openlib	luaL_openlib
#endif


/* extra error code for `luaL_load' */
#define LUA_ERRFILE     (LUA_ERRERR+1)


typedef struct luaL_Reg {
  const char *name;
  lua_CFunction func;
} luaL_Reg;



LUALIB_API void (luaI_openlib) (lua_State *L, const char *libname,
                                const luaL_Reg *l, int nup);
LUALIB_API void (luaL_register) (lua_State *L, const char *libname,
                                const luaL_Reg *l);
LUALIB_API int (luaL_getmetafield) (lua_State *L, int obj, const char *e);
LUALIB_API int (luaL_callmeta) (lua_State *L, int obj, const char *e);
LUALIB_API int (luaL_typerror) (lua_State *L, int narg, const char *tname);
LUALIB_API int (luaL_argerror) (lua_State *L, int numarg, const char *extramsg);
LUALIB_API const char *(luaL_checklstring) (lua_State *L, int numArg,
                                                          size_t *l);
LUALIB_API const char *(luaL_optlstring) (lua_State *L, int numArg,
                                          const char *def, size_t *l);
LUALIB_API lua_Number (luaL_checknumber) (lua_State *L, int numArg);
LUALIB_API lua_Number (luaL_optnumber) (lua_State *L, int nArg, lua_Number def);

LUALIB_API lua_Integer (luaL_checkinteger) (lua_State *L, int numArg);
LUALIB_API lua_Integer (luaL_optinteger) (lua_State *L, int nArg,
                                          lua_Integer def);

LUALIB_API void (luaL_checkstack) (lua_State *L, int sz, const char *msg);
LUALIB_API void (luaL_checktype) (lua_State *L, int narg, int t);
LUALIB_API void (luaL_checkany) (lua_State *L, int narg);

LUALIB_API int   (luaL_newmetatable) (lua_State *L, const char *tname);
LUALIB_API void *(luaL_checkudata) (lua_State *L, int ud, const char *tname);

/* Like luaL_checkudata, but does not throw a type error
 * if the userdata type does not match. Returns NULL if the value
 * is not a userdata, or if it is a userdata but does not match the type.
 * Note: userdata cannot wrap a NULL pointer.
 */
LUALIB_API void *(luaL_checkudata_noerror) (lua_State *L, int ud,
                                            const char *tname);

LUALIB_API void (luaL_where) (lua_State *L, int lvl);
LUALIB_API int (luaL_error) (lua_State *L, const char *fmt, ...);

LUALIB_API int (luaL_checkoption) (lua_State *L, int narg, const char *def,
                                   const char *const lst[]);

LUALIB_API int (luaL_ref) (lua_State *L, int t);
LUALIB_API void (luaL_unref) (lua_State *L, int t, int ref);

LUALIB_API int (luaL_loadfile) (lua_State *L, const char *filename);
LUALIB_API int (luaL_loadbuffer) (lua_State *L, const char *buff, size_t sz,
                                  const char *name);
LUALIB_API int (luaL_loadstring) (lua_State *L, const char *s);

LUALIB_API lua_State *(luaL_newstate) (void);


LUALIB_API const char *(luaL_gsub) (lua_State *L, const char *s, const char *p,
                                                  const char *r);

LUALIB_API const char *(luaL_findtable) (lua_State *L, int idx,
                                         const char *fname, int szhint);

/** this function is called whenever lua_pushuserptr is called.
 * It allows an opportunity to for adding a reference that the metatable
 * __gc handler will delete */
typedef void (*luaL_UserPtrPushFunc)(lua_State *L, void *ptr);

/* Creates a new named metatable and registers the provided set
 * of functions in the table, such that it contains both metatable
 * event handlers and runtime methods.
 * If pushfunc is provided, it is stored as an opaque lightuserdata
 * in the metatable.
 * Does not modify the stack.
 */
LUALIB_API void luaL_registerptrtype(lua_State *L,
	const char *metatable, const luaL_Reg *funcs,
    luaL_UserPtrPushFunc pushfunc);

/* Creates a "userptr" variant of a userdata.
 * The userptr type can hold only a pointer and is implicitly created
 * with the specified metatable set.
 * If the metatable was created by luaL_registerptrtype and has a
 * pushfunc registered, the pushfunc will be called.
 * Pushes a userdata representation of the provided pointer on to the stack.
 * luaL_checkudata() and lua_touserdata() will return the "ptr" argument,
 * so the calling code will not need to dereference it as it would if was
 * using lua_newuserdata().
 * */
LUALIB_API void luaL_pushuserptr(lua_State *L, const char *metatable,
	void *ptr, int nocallpush);

/* Creates a "userptr" per luaL_pushuserptr.
 * This userptr instance will keep a reference to "otherref", such that
 * it will release "otherref" when this userptr instance is finalized.
 */
LUALIB_API void luaL_pushuserptrandref(lua_State *L,
	const char *metatable, void *ptr, int nocallpush, void *otherref);

/* Breaks a userptr reference.  Useful in situations where some other
 * library function has explicitly finalized the underlying resource and
 * we don't want to trigger it again on gc */
LUALIB_API void luaL_breakuserptr(lua_State *L, int idx);

LUALIB_API int luaL_isuserptr(lua_State *L, int idx);

/*
** ===============================================================
** some useful macros
** ===============================================================
*/

#define luaL_argcheck(L, cond,numarg,extramsg)	\
		((void)((cond) || luaL_argerror(L, (numarg), (extramsg))))
#define luaL_checkstring(L,n)	(luaL_checklstring(L, (n), NULL))
#define luaL_optstring(L,n,d)	(luaL_optlstring(L, (n), (d), NULL))
#define luaL_checkint(L,n)	((int)luaL_checkinteger(L, (n)))
#define luaL_optint(L,n,d)	((int)luaL_optinteger(L, (n), (d)))
#define luaL_checklong(L,n)	((long)luaL_checkinteger(L, (n)))
#define luaL_optlong(L,n,d)	((long)luaL_optinteger(L, (n), (d)))

#define luaL_typename(L,i)	lua_typename(L, lua_type(L,(i)))

#define luaL_dofile(L, fn) \
	(luaL_loadfile(L, fn) || lua_pcall(L, 0, LUA_MULTRET, 0))

#define luaL_dostring(L, s) \
	(luaL_loadstring(L, s) || lua_pcall(L, 0, LUA_MULTRET, 0))

#define luaL_getmetatable(L,n)	(lua_getfield(L, LUA_REGISTRYINDEX, (n)))

#define luaL_opt(L,f,n,d)	(lua_isnoneornil(L,(n)) ? (d) : f(L,(n)))

/*
** {======================================================
** Generic Buffer manipulation
** =======================================================
*/



typedef struct luaL_Buffer {
  char *p;			/* current position in buffer */
  int lvl;  /* number of strings in the stack (level) */
  lua_State *L;
  char buffer[LUAL_BUFFERSIZE];
} luaL_Buffer;

#define luaL_addchar(B,c) \
  ((void)((B)->p < ((B)->buffer+LUAL_BUFFERSIZE) || luaL_prepbuffer(B)), \
   (*(B)->p++ = (char)(c)))

/* compatibility only */
#define luaL_putchar(B,c)	luaL_addchar(B,c)

#define luaL_addsize(B,n)	((B)->p += (n))

LUALIB_API void (luaL_buffinit) (lua_State *L, luaL_Buffer *B);
LUALIB_API char *(luaL_prepbuffer) (luaL_Buffer *B);
LUALIB_API void (luaL_addlstring) (luaL_Buffer *B, const char *s, size_t l);
LUALIB_API void (luaL_addstring) (luaL_Buffer *B, const char *s);
LUALIB_API void (luaL_addvalue) (luaL_Buffer *B);
LUALIB_API void (luaL_pushresult) (luaL_Buffer *B);


/* }====================================================== */

/* Enhanced Buffer API.
 * This allows modules and scripts to efficiently pass data around without
 * forcing them to allocate and map the data as string objects.
 */
#define LUAL_BUFFER_MT "lua:luaL_BufferObj"
struct luaL_BufferObj;
typedef struct luaL_BufferObj luaL_BufferObj;

/** When working with buffers whose memory is managed externally,
 * the free function will be called when the buffer is finalized
 * so that the memory can be released. */
typedef void (*luaL_BuffFreeFunc)(void *ptr);

/** Allocate a new buffer object and push it onto the stack.
 * The buffer is implemented as a userdata with a metatable of
 * type LUAL_BUFFER_MT.
 *
 * There are two modes of operation:
 *
 *    buf = luaL_bufnew(8192, NULL, NULL);
 *
 * This mode creates a new buffer of size 8k.
 * The other mode allows you to reference a pre-existing buffer managed
 * by some other code and/or via another allocator.  When used in this
 * second mode, if freefunc is not NULL, it will called on ptr when
 * the buffer is finalized by the garbage collector:
 *
 *    buf = luaL_bufnew(8192, ptr, free, 64);
 *
 * The len parameter specifies where the end of the used portion of the
 * buffer can be found, in bytes.  This is used to map an externally
 * writable buffer that is partially filled.
 *
 * Buffers are fixed size; they do not grow.
 */
LUALIB_API luaL_BufferObj *luaL_bufnew(lua_State *L,
	size_t size, void *ptr, luaL_BuffFreeFunc freefunc, size_t len);

/** Returns the buffer at the specified acceptable index, or NULL
 * if that value is not a buffer */
LUALIB_API luaL_BufferObj *luaL_tobuffer(lua_State *L, int idx);

/** Writes data into buffer at the specified offset, returning the number
 * of bytes written.
 * If the data would overflow the buffer, a partial write is performed
 * and the number of bytes actually written to the buffer is returned.
 *
 * if offset is -1, then the data will be appended to the buffer starting
 * at the last written offset.
 */
LUALIB_API size_t luaL_bufwrite(luaL_BufferObj *b, int offset,
	const void *ptr, size_t bytes);

/** Create a slice buffer, which is a read/write view on another buffer.
 * Changes made to the slice are reflected in the original buffer.
 * The original buffer is not collectable until any and all slices of
 * it have been collected.
 */
LUALIB_API luaL_BufferObj *luaL_bufslice(lua_State *L,
	luaL_BufferObj *b, size_t offset, size_t len);

/** Returns the buffer memory and used length.
 * This function is intended to be used for read operations when
 * passing the buffer contents into other library routines.
 */
LUALIB_API void *luaL_bufmem(luaL_BufferObj *b, size_t *len);

/** Copies data from srcbuf to destbuf.
 * if destoff is -1, the data will be appended to destbuf, otherwise
 * destoff specifies the offset at which the data will be placed.
 * srcoff specifies the starting offset in srcbuf from which to copy
 * the data.
 * srclen specifies how much data to copy; if srclen is -1 then all
 * data from srcoff through to the end of the src buffer will be copied.
 */
LUALIB_API size_t luaL_bufcopy(luaL_BufferObj *dest, int destoff,
		luaL_BufferObj *srcbuf, size_t srcoff, int srclen);


/* compatibility with ref system */

/* pre-defined references */
#define LUA_NOREF       (-2)
#define LUA_REFNIL      (-1)

#define lua_ref(L,lock) ((lock) ? luaL_ref(L, LUA_REGISTRYINDEX) : \
      (lua_pushstring(L, "unlocked references are obsolete"), lua_error(L), 0))

#define lua_unref(L,ref)        luaL_unref(L, LUA_REGISTRYINDEX, (ref))

#define lua_getref(L,ref)       lua_rawgeti(L, LUA_REGISTRYINDEX, (ref))


#define luaL_reg	luaL_Reg

#endif


