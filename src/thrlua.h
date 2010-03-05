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

#include "luaconf.h"
#include <setjmp.h>
#include <locale.h>
#include <time.h>
#include <signal.h>

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

