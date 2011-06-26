/*
** $Id: ltm.c,v 2.8.1.1 2007/12/27 13:02:25 roberto Exp $
** Tag methods
** See Copyright Notice in lua.h
*/

#define ltm_c
#define LUA_CORE

#include "thrlua.h"


const char *const luaT_typenames[] = {
  "nil", "boolean", "userdata", "number",
  "string", "table", "function", "userdata", "thread",
  "proto", "upval", "deadkey", "global"
};


void luaT_init (lua_State *L) {
  static const char *const luaT_eventname[] = {  /* ORDER TM */
    "__index", "__newindex",
    "__gc", "__mode", "__eq",
    "__add", "__sub", "__mul", "__div", "__mod",
    "__pow", "__unm", "__len", "__lt", "__le",
    "__concat", "__call"
#if defined(LUA_BITWISE_OPERATORS)
    ,"__or", "__and", "__xor", "__shl", "__shr", "__not", "__intdiv"
#endif
  };
  int i;
  for (i=0; i<TM_N; i++) {
    G(L)->tmname[i] = luaS_new(L, luaT_eventname[i]);
  }
}


int luaT_load_tm(lua_State *L, Table *events, TMS event,
    TValue *tm, GCheader *barrier, int nilok)
{
  if (luaH_load_str(L, events, G(L)->tmname[event], tm, barrier, nilok)) {
    return 1;
  }
  if (event <= TM_EQ) {
    /* remember the miss so that fasttm won't need to work so hard */
    events->flags |= cast_byte(1u << event);
  }
  return 0;
}

/* assumption: that the table is appropriately locked! */
int luaT_gettmbyobj(lua_State *L, const TValue *o, TMS event, TValue *tm)
{
  Table *mt;
  switch (ttype(o)) {
    case LUA_TTABLE:
      mt = gch2h(hvalue(o)->metatable);
      break;
    case LUA_TUSERDATA:
      mt = gch2h(uvalue(o)->metatable);
      break;
    default:
      mt = G(L)->mt[ttype(o)];
  }
  if (!mt) {
    return 0;
  }
  return luaT_load_tm(L, mt, event, tm, NULL, 0);
}

/* vim:ts=2:sw=2:et:
 */
