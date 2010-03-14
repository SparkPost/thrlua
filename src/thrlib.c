/*
 * Copyright (c) 2010 Message Systems, Inc. All rights reserved
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF MESSAGE SYSTEMS
 * The copyright notice above does not evidence any
 * actual or intended publication of such source code.
 *
 * Redistribution of this material is strictly prohibited.
 */

#include "thrlua.h"

struct thrlib_thread {
  pthread_t osthr;
  int joined;
  lua_State *L;
};

static void *thrlib_thread_func(void *arg)
{
  struct thrlib_thread *th = arg;

  lua_call(th->L, 1, 0);

  scpt_atomic_dec(&th->L->gch.ref);
  return 0;
}

static int thrlib_create(lua_State *L)
{
  struct thrlib_thread *th;
  int err;

  luaL_argcheck(L, lua_isfunction(L, 1), 1,
    "function expected");

  th = lua_newuserdata(L, sizeof(*th));
  memset(th, 0, sizeof(*th));
  th->L = lua_newthread(L);
  scpt_atomic_inc(&th->L->gch.ref);

  /* get function parameter on top of stack */
  lua_pushvalue(L, 1);
  /* move function over to new thread */
  lua_xmove(L, th->L, 1);
  /* pop lua state from this stack */
  lua_pop(L, 1);
  /* copy userdata over to new thread; it will be the first arg
   * to the function */
  lua_pushvalue(L, -1);
  lua_xmove(L, th->L, 1);

  /* FIXME: gc/metatable */
  luaL_getmetatable(L, "thread");
  lua_setmetatable(L, -2);

  err = pthread_create(&th->osthr, NULL, thrlib_thread_func, th);
  if (err) {
    luaL_error(L, "thread.create failed: %d %s", err, strerror(err));
  }
  return 1;
}

static int thread_gc(lua_State *L)
{
  struct thrlib_thread *th = luaL_checkudata(L, 1, "thread");
  if (!th->joined) {
    void *retval = NULL;
    pthread_join(th->osthr, &retval);
    th->joined = 1;
  }
  return 0;
}

static int thread_join(lua_State *L)
{
  struct thrlib_thread *th = luaL_checkudata(L, 1, "thread");
  if (!th->joined) {
    void *retval = NULL;
    pthread_join(th->osthr, &retval);
    th->joined = 1;
  }
  return 0;
}

static const luaL_Reg thread_funcs[] = {
  {"__gc", thread_gc },
  {"join", thread_join },
  {NULL, NULL}
};


static int thrlib_sleep(lua_State *L)
{
  lua_Integer n = luaL_checkinteger(L, 1);
  n = sleep(n);
  lua_pushinteger(L, n);
  return 1;
}

static const luaL_Reg thrlib[] = {
  {"create", thrlib_create },
  {"sleep", thrlib_sleep },
  {NULL, NULL}
};

LUALIB_API int luaopen_thread(lua_State *L)
{
  luaL_newmetatable(L, "thread");
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  luaL_register(L, NULL, thread_funcs);

  luaL_register(L, LUA_THREADLIBNAME, thrlib);
  return 1;
}

/* vim:ts=2:sw=2:et:
 */

