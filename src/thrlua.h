/*
 * Copyright (c) 2010 Message Systems, Inc. All rights reserved
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF MESSAGE SYSTEMS
 * The copyright notice above does not evidence any
 * actual or intended publication of such source code.
 *
 * Redistribution of this material is strictly prohibited.
 *
 */

#ifndef THRLUA_H
#define THRLUA_H

#define _GNU_SOURCE
#include "luaconf.h"

#define LUA_ASSERTIONS 1

#if HAVE__OPT_MSYS_3RDPARTY_INCLUDE_VALGRIND_VALGRIND_H
#include </opt/msys/3rdParty/include/valgrind/memcheck.h>
#include </opt/msys/3rdParty/include/valgrind/valgrind.h>
#define HAVE_VALGRIND 1
#endif

#if LUA_ASSERTIONS && HAVE_VALGRIND
static inline void lua_assert_fail(const char *expr, const char *file, int line)
{
  VALGRIND_PRINTF_BACKTRACE("Assertion %s failed\n", expr);
  fprintf(stderr, "Assertion %s failed at %s:%d\n", expr, file, line);
  abort();
}

# define lua_assert(expr)	\
  ((expr)							   	\
   ? (void) (0)						\
   : (lua_assert_fail (#expr, __FILE__, __LINE__), \
      (void) (0)))


#elif LUA_ASSERTIONS
# define lua_assert assert
#else
# define lua_assert(expr)
#endif



#include <setjmp.h>
#include <locale.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#if defined(LUA_DL_DLOPEN)
#include <dlfcn.h>
#endif

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

struct global_State;
typedef struct global_State global_State;

#include "llimits.h"
#include "lobject.h"
#include "ltable.h"
#include "lzio.h"
#include "llex.h"
#include "lopcodes.h"
#include "lparser.h"
#include "lcode.h"
#include "ltm.h"
#include "lstate.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lstring.h"
#include "lundump.h"
#include "lvm.h"

/* Macros to set values */
static inline void setnilvalue(TValue *obj)
{
  obj->tt = LUA_TNIL;
}

static inline void setnvalue(TValue *obj, lua_Number n)
{
  obj->value.n = n;
  obj->tt = LUA_TNUMBER;
}

static inline void setpvalue(TValue *obj, void *ud)
{
  obj->value.p = ud;
  obj->tt = LUA_TLIGHTUSERDATA;
}

static inline void setbvalue(TValue *obj, int b)
{
  obj->value.b = b;
  obj->tt = LUA_TBOOLEAN;
}

static inline void setsvalue(lua_State *L, TValue *obj, TString *str)
{
  obj->value.gc = &str->tsv.gch;
  obj->tt = LUA_TSTRING;
  checkliveness(G(L), obj);
}

static inline void setuvalue(lua_State *L, TValue *obj, Udata *ud)
{
  obj->value.gc = &ud->uv.gch;
  obj->tt = LUA_TUSERDATA;
  checkliveness(G(L), obj);
}

static inline void setthvalue(lua_State *L, TValue *obj, lua_State *thr)
{
  obj->value.gc = &thr->gch;
  obj->tt = LUA_TTHREAD;
  checkliveness(G(L), obj);
}

static inline void setclvalue(lua_State *L, TValue *obj, Closure *cl)
{
  obj->value.gc = &cl->gch;
  obj->tt = LUA_TFUNCTION;
  checkliveness(G(L), obj);
}

static inline void sethvalue(lua_State *L, TValue *obj, Table *t)
{
  obj->value.gc = &t->gch;
  obj->tt = LUA_TTABLE;
  checkliveness(G(L), obj);
}

static inline void setptvalue(lua_State *L, TValue *obj, Proto *p)
{
  obj->value.gc = &p->gch;
  obj->tt = LUA_TPROTO;
  checkliveness(G(L), obj);
}

static inline void setobj(lua_State *L, TValue *obj1, const TValue *obj2)
{
  obj1->value = obj2->value;
  obj1->tt = obj2->tt;
  checkliveness(G(L), obj1);
}

/*
** different types of sets, according to destination
*/

/* from stack to (same) stack */
#define setobjs2s	setobj
/* to stack (not from same stack) */
#define setobj2s	setobj
#define setsvalue2s	setsvalue
#define sethvalue2s	sethvalue
#define setptvalue2s	setptvalue
/* from table to same table */
#define setobjt2t	setobj
/* to table */
#define setobj2t	setobj
/* to new object */
#define setobj2n	setobj
#define setsvalue2n	setsvalue

static inline void setttype(TValue *obj, lu_byte tt)
{
#if HAVE_VALGRIND
  VALGRIND_PRINTF_BACKTRACE("changing type on value %p from %s to %s\n",
    obj, lua_typename(NULL, ttype(obj)), lua_typename(NULL, tt));
#endif
  ttype(obj) = tt;
}


#define getstr(ts)	cast(const char *, ((TString*)ts) + 1)
#define svalue(o)       getstr(rawtsvalue(o))

/* chain list of long jump buffers */
struct lua_longjmp {
  struct lua_longjmp *previous;
  luai_jmpbuf b;
  volatile int status;  /* error code */
  const char *file;
  unsigned int line;
};

/* NOTE: undefined (certainly bad!) behavior will result if the function
 * returns from within the TRY/FINALLY block handling */
#define LUAI_TRY_BLOCK(L) do { \
  struct lua_longjmp lj; \
  lj.status = 0; \
  lj.previous = (L)->errorJmp; \
  (L)->errorJmp = &lj; \
  lj.file = __FILE__; \
  lj.line = __LINE__; \
  if (setjmp(lj.b) == 0) {

#define LUAI_TRY_FINALLY(L) \
  }

#define LUAI_TRY_END(L) \
  (L)->errorJmp = lj.previous; \
  if (lj.status) { \
    luaD_throw((L), lj.status); \
  } \
} while(0)

#define LUAI_TRY_CATCH(L) \
  } \
  if (lj.status -= lj.status)

#ifdef __cplusplus
extern "C" {
#endif

LUAI_FUNC void luaA_pushobject (lua_State *L, const TValue *o);


#ifdef __cplusplus
}
#endif

#endif

/* vim:ts=2:sw=2:et:
 */

