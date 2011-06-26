/*
** $Id: ltable.h,v 2.10.1.1 2007/12/27 13:02:25 roberto Exp $
** Lua tables (hash)
** See Copyright Notice in lua.h
*/

#ifndef ltable_h
#define ltable_h

#include "ck_rwlock.h"

#define gnode(t,i)	(&(t)->node[i])
#define gkey(n)		(&(n)->i_key.nk)
#define gval(n)		(&(n)->i_val)
#define gnext(n)	((n)->i_key.nk.next)

#define key2tval(n)	(&(n)->i_key.tvk)

/* These load routines will look up the provided key and store the result
 * into the provided val.  If barrier is non-NULL, the store will be made
 * via a writebarrier, with the "barrier" parameter being passed as the
 * continaing object.
 * If the load returns a nil object (indicating no value was found), then
 * "val" will only have the nil value loaded into it if the "nilok" parameter
 * evaluates to true.
 * Returns 1 if the load found a non-nil value, 0 otherwise.
 */

LUAI_FUNC int luaH_load_num(lua_State *L, Table *t, int key,
		TValue *val, GCheader *barrier, int nilok);

LUAI_FUNC int luaH_load_str(lua_State *L, Table *t, const TString *key,
		TValue *val, GCheader *barrier, int nilok);

LUAI_FUNC int luaH_load(lua_State *L, Table *t, const TValue *key,
		TValue *val, GCheader *barrier, int nilok);

LUAI_FUNC int luaH_store(lua_State *L, Table *t, const TValue *key,
		const TValue *rval, int create);

LUAI_FUNC int luaH_store_str(lua_State *L, Table *t, const TString *key,
		const TValue *rval, int create);

LUAI_FUNC int luaH_store_num(lua_State *L, Table *t, int key,
		const TValue *rval, int create);

LUAI_FUNC Table *luaH_new (lua_State *L, int narray, int lnhash);
LUAI_FUNC void luaH_resizearray (lua_State *L, Table *t, int nasize);
LUAI_FUNC void luaH_free (lua_State *L, Table *t);
LUAI_FUNC int luaH_next (lua_State *L, Table *t, StkId key);
LUAI_FUNC int luaH_getn (Table *t);

LUAI_FUNC void luaH_wrlock(lua_State *L, Table *t);
LUAI_FUNC void luaH_wrunlock(lua_State *L, Table *t);
LUAI_FUNC void luaH_rdlock(lua_State *L, Table *t);
LUAI_FUNC void luaH_rdunlock(lua_State *L, Table *t);

#if defined(LUA_DEBUG)
LUAI_FUNC Node *luaH_mainposition (const Table *t, const TValue *key);
LUAI_FUNC int luaH_isdummy (Node *n);
#endif


#endif
