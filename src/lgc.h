/*
** $Id: lgc.h,v 2.15.1.1 2007/12/27 13:02:25 roberto Exp $
** Garbage Collector
** See Copyright Notice in lua.h
*/

#ifndef lgc_h
#define lgc_h

/*
** Possible states of the Garbage Collector
*/
#define GCSpause	0
#define GCSpropagate	1
#define GCSsweepstring	2
#define GCSsweep	3
#define GCSfinalize	4

/** the write barrier is used for assignments made to properties of
 * heap objects, not stack items */
LUAI_FUNC void luaC_writebarrier(lua_State *L, GCheader *object,
  GCheader **lvalue, GCheader *rvalue);
LUAI_FUNC void luaC_writebarriervv(lua_State *L, GCheader *object,
  TValue *lvalue, const TValue *rvalue);
LUAI_FUNC void luaC_writebarrierov(lua_State *L, GCheader *object,
  GCheader **lvalue, const TValue *rvalue);
LUAI_FUNC void luaC_writebarriervo(lua_State *L, GCheader *object,
  TValue *lvalue, GCheader *rvalue);
LUAI_FUNC void luaC_writebarrierstr(lua_State *L, 
                                    unsigned int h,
                                    struct stringtable_node *n);

LUAI_FUNC void *luaC_newobj(lua_State *L, enum lua_obj_type tt);
LUAI_FUNC void *luaC_newobjv(lua_State *L, enum lua_obj_type tt, size_t size);
LUAI_FUNC global_State *luaC_newglobal(struct lua_StateParams *p);
LUAI_FUNC void luaC_checkGC(lua_State *L);
LUAI_FUNC int64_t luaC_count(lua_State *L);
LUAI_FUNC int luaC_fullgc (lua_State *L);
LUAI_FUNC int luaC_localgc (lua_State *L, int greedy);

#endif
/* vim:ts=2:sw=2:et:
 */
