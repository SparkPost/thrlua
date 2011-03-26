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

#define THRLIB_THREAD "thread.thread"
#define THRLIB_MUTEX  "thread.mutex"

struct thrlib_thread {
  pthread_t osthr;
  int joined;
  lua_State *L;
};

static int traceback(lua_State *L)
{
  // FIXME: write a test for traceback inside a thread
  if (!lua_isstring(L, 1))  {
    /* 'message' not a string? */
    printf("TRACEBACK: error is not a string\n");
    return 1;  /* keep it intact */
  }
  lua_getfield(L, LUA_GLOBALSINDEX, "debug");
  if (!lua_istable(L, -1)) {
    printf("TRACEBACK: can't find debug lib?\n");
    lua_pop(L, 1);
    return 1;
  }
  lua_getfield(L, -1, "traceback");
  if (!lua_isfunction(L, -1)) {
    printf("TRACEBACK: can't find debug.traceback?\n");
    lua_pop(L, 2);
    return 1;
  }
  printf("TRACEBACK: should have seen the traceback here?\n");
  lua_pushvalue(L, 1);  /* pass error message */
  lua_pushinteger(L, 2);  /* skip this function and traceback */
  lua_call(L, 2, 1);  /* call debug.traceback */
  return 1;
}

static void *thrlib_thread_func(void *arg)
{
  struct thrlib_thread *th = arg;
  int st;
  lua_State *L = th->L;

  /* on entry, stack looks like this:
   * [1] traceback
   * [2] closure
   * [3] closure argument
   * Therefore, we call pcall with 1 arg and a base of 1 (traceback) */

  st = lua_pcall(L, 1, 0, 1);
  if (st != 0) {
    printf("thread pcall failed with status %d\n", st);
  }

  lua_pop(L, lua_gettop(L));
  luaC_localgc(L);

  /* we exit leaving a ref to the lua_State; whoever joins will un-pin
   * and inherit that thread */

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
  /* one ref for the OS-level thread */
  ck_pr_inc_32(&th->L->gch.ref);

  /* trace function is on the top of the stack */
  lua_pushcfunction(th->L, traceback);

  /* make a copy of 1st function parameter; put on top of stack */
  lua_pushvalue(L, 1);
  /* move function over to new thread */
  lua_xmove(L, th->L, 1);
  /* pop lua state from this stack */
  lua_pop(L, 1);
  /* copy userdata over to new thread; it will be the first arg
   * to the function */
  lua_pushvalue(L, -1);
  lua_xmove(L, th->L, 1);

  luaL_getmetatable(L, THRLIB_THREAD);
  lua_setmetatable(L, -2);

  err = pthread_create(&th->osthr, NULL, thrlib_thread_func, th);
  if (err) {
    return luaL_error(L, "thread.create failed: %d %s", err, strerror(err));
  }
  return 1;
}

static int thread_join(lua_State *L)
{
  struct thrlib_thread *th = luaL_checkudata(L, 1, THRLIB_THREAD);
  if (!th->joined) {
    void *retval = NULL;
    pthread_join(th->osthr, &retval);
    th->joined = 1;

    /* we inherit this thread */
    luaC_inherit_thread(L, th->L);
    ck_pr_dec_32(&th->L->gch.ref);
    th->L = NULL;
  }
  lua_pushboolean(L, 1);
  return 1;
}

static int thread_gc(lua_State *L)
{
  return thread_join(L);
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

static int thrlib_mutex_new(lua_State *L)
{
  pthread_mutexattr_t mattr;
  int nargs = lua_gettop(L);
  pthread_mutex_t *mtx = lua_newuserdata(L, sizeof(*mtx));

  pthread_mutexattr_init(&mattr);
  LUAI_TRY_BLOCK(L) {
    lua_Integer t = PTHREAD_MUTEX_ERRORCHECK;

    if (nargs) {
      t = luaL_checkinteger(L, 1);
      switch (t) {
        case PTHREAD_MUTEX_NORMAL:
        case PTHREAD_MUTEX_RECURSIVE:
        case PTHREAD_MUTEX_ERRORCHECK:
          break;
        default:
          luaL_argcheck(L, 0, 1,
            "specify one of thread.MUTEX_NORMAL, "
            "thread.MUTEX_RECURSIVE or thread.MUTEX_ERRORCHECK");
      }
    }
    pthread_mutexattr_settype(&mattr, t);

    pthread_mutex_init(mtx, &mattr);
  } LUAI_TRY_FINALLY(L) {
    pthread_mutexattr_destroy(&mattr);
  } LUAI_TRY_END(L);

  luaL_getmetatable(L, THRLIB_MUTEX);
  lua_setmetatable(L, -2);

  return 1;
}

static int handle_mutex_return(lua_State *L, const char *what, int ret)
{
  switch (ret) {
    case 0:
      lua_pushboolean(L, 1);
      return 1;

    case EINVAL:
    case EDEADLK:
    case EPERM:
      return luaL_error(L, "failed to %s mutex: %d %s",
        what, ret, strerror(ret));

    default:
      lua_pushinteger(L, ret);
      return 1;
  }
}

static int thrlib_mutex_lock(lua_State *L)
{
  pthread_mutex_t *mtx = luaL_checkudata(L, 1, THRLIB_MUTEX);
  int ret;

  ret = pthread_mutex_lock(mtx);

  return handle_mutex_return(L, "lock", ret);
}

static int thrlib_mutex_trylock(lua_State *L)
{
  pthread_mutex_t *mtx = luaL_checkudata(L, 1, THRLIB_MUTEX);
  int ret;

  ret = pthread_mutex_trylock(mtx);

  return handle_mutex_return(L, "trylock", ret);
}

static int thrlib_mutex_unlock(lua_State *L)
{
  pthread_mutex_t *mtx = luaL_checkudata(L, 1, THRLIB_MUTEX);
  int ret;

  ret = pthread_mutex_unlock(mtx);

  return handle_mutex_return(L, "unlock", ret);
}

static int thrlib_mutex_gc(lua_State *L)
{
  pthread_mutex_t *mtx = luaL_checkudata(L, 1, THRLIB_MUTEX);

  pthread_mutex_destroy(mtx);

  return 0;
}

static const luaL_Reg mutex_funcs[] = {
  {"lock", thrlib_mutex_lock },
  {"unlock", thrlib_mutex_unlock },
  {"trylock", thrlib_mutex_trylock },
  {"__gc", thrlib_mutex_gc },
  {NULL, NULL}
};

static const luaL_Reg thrlib[] = {
  {"create", thrlib_create },
  {"sleep", thrlib_sleep },
  {"mutex", thrlib_mutex_new },
  {NULL, NULL}
};

LUALIB_API int luaopen_thread(lua_State *L)
{
  /* OS thread metatable */
  luaL_newmetatable(L, THRLIB_THREAD);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  luaL_register(L, NULL, thread_funcs);

  /* mutex metatable */
  luaL_newmetatable(L, THRLIB_MUTEX);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  luaL_register(L, NULL, mutex_funcs);

  luaL_register(L, LUA_THREADLIBNAME, thrlib);

  /* thread.MUTEX_NORMAL through thread.MUTEX_ERRORCHECK */
  lua_pushinteger(L, PTHREAD_MUTEX_NORMAL);
  lua_setfield(L, -2, "MUTEX_NORMAL");
  lua_pushinteger(L, PTHREAD_MUTEX_RECURSIVE);
  lua_setfield(L, -2, "MUTEX_RECURSIVE");
  lua_pushinteger(L, PTHREAD_MUTEX_ERRORCHECK);
  lua_setfield(L, -2, "MUTEX_ERRORCHECK");

  return 1;
}

/* vim:ts=2:sw=2:et:
 */
