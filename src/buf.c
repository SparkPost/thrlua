/*
 * Copyright (c) 2011 Message Systems, Inc. All rights reserved
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF MESSAGE SYSTEMS
 * The copyright notice above does not evidence any
 * actual or intended publication of such source code.
 *
 * Redistribution of this material is strictly prohibited.
 *
 */

#include "thrlua.h"

struct luaL_BufferObj {
  char *ptr;
  size_t len, allocd;
  luaL_BuffFreeFunc freefunc;
  void *sliceref;
};

luaL_BufferObj *luaL_bufnew(lua_State *L,
	size_t size, void *ptr, luaL_BuffFreeFunc freefunc, size_t len)
{
  luaL_BufferObj *b;

  if (ptr) {
    b = lua_newuserdata(L, sizeof(*b));
    memset(b, 0, sizeof(*b));
    b->freefunc = freefunc;
    b->len = len;
    b->ptr = ptr;
  } else {
    b = lua_newuserdata(L, sizeof(*b) + size);
    memset(b, 0, sizeof(*b));
    b->ptr = (char*)(b + 1);
  }
  b->allocd = size;

  luaL_getmetatable(L, LUAL_BUFFER_MT);
  lua_setmetatable(L, -2);

  return b;
}

luaL_BufferObj *luaL_tobuffer(lua_State *L, int idx)
{
  luaL_BufferObj *b = lua_touserdata(L, idx);

  if (b && lua_getmetatable(L, idx)) {
    lua_getfield(L, LUA_REGISTRYINDEX, LUAL_BUFFER_MT);
    if (lua_rawequal(L, -1, -2)) {
      lua_pop(L, 2);  /* remove both metatables */
      return b;
    }
  }
  return NULL;
}

size_t luaL_bufwrite(luaL_BufferObj *b, int offset,
	const void *ptr, size_t bytes)
{
  char *dest = b->ptr;
  char *end = dest + b->allocd;

  if (offset == -1) {
    offset = b->len;
  }

  dest += offset;
  if (dest > end) {
    return 0;
  }

  if (dest + bytes > end) {
    bytes = end - dest;
  }

  if (bytes) {
    memcpy(dest, ptr, bytes);
  }

  b->len = offset + bytes;

  return bytes;
}

luaL_BufferObj *luaL_bufslice(lua_State *L,
	luaL_BufferObj *b, size_t offset, size_t len)
{
  luaL_BufferObj *slice;
  Udata *ud = ((Udata*)b) - 1;

  if (offset > b->allocd || offset + len > b->allocd) {
    return NULL;
  }

  slice = lua_newuserdata(L, sizeof(*slice));
  memset(slice, 0, sizeof(*slice));
  slice->ptr = b->ptr + offset;
  slice->len = len;
  slice->allocd = len;
  ck_pr_inc_32(&ud->uv.gch.ref);
  slice->sliceref = &ud->uv.gch;

  luaL_getmetatable(L, LUAL_BUFFER_MT);
  lua_setmetatable(L, -2);

  return slice;
}

void *luaL_bufmem(luaL_BufferObj *b, size_t *len)
{
  if (len) {
    *len = b->len;
  }
  return b->ptr;
}

size_t luaL_bufcopy(luaL_BufferObj *dest, int destoff,
		luaL_BufferObj *srcbuf, size_t srcoff, int srclen)
{
  if (srclen == -1) {
    srclen = srcbuf->len - srcoff;
  }

  if (srcoff > srcbuf->allocd || srcoff + srclen > srcbuf->allocd) {
    return 0;
  }

  return luaL_bufwrite(dest, destoff, srcbuf->ptr + srcoff, srclen);
}

static int buf_gc(lua_State *L)
{
  luaL_BufferObj *b = luaL_checkudata(L, 1, LUAL_BUFFER_MT);

  if (b->sliceref) {
    lua_delrefobj(L, b->sliceref);
    b->sliceref = NULL;
  }

  if (b->freefunc) {
    b->freefunc(b->ptr);
    b->ptr = NULL;
  }
  return 0;
}

static int buf_len(lua_State *L)
{
  luaL_BufferObj *b = luaL_checkudata(L, 1, LUAL_BUFFER_MT);

  lua_pushinteger(L, b->len);
  return 1;
}

static int buf_tostring(lua_State *L)
{
  luaL_BufferObj *b = luaL_checkudata(L, 1, LUAL_BUFFER_MT);
  lua_pushlstring(L, b->ptr, b->len);
  return 1;
}

static int buf_write(lua_State *L)
{
  luaL_BufferObj *b;
  const char *str;
  size_t len;
  int offset;

  b = luaL_checkudata(L, 1, LUAL_BUFFER_MT);
  str = luaL_checklstring(L, 2, &len);
  offset = luaL_optinteger(L, 3, -1);
  lua_pushinteger(L, luaL_bufwrite(b, offset, str, len));
  return 1;
}

static int buf_copy(lua_State *L)
{
  luaL_BufferObj *src = luaL_checkudata(L, 1, LUAL_BUFFER_MT);
  luaL_BufferObj *dest = luaL_checkudata(L, 2, LUAL_BUFFER_MT);
  int destoff = luaL_optinteger(L, 3, 0);
  int srcoff = luaL_optinteger(L, 4, 0);
  int srclen = luaL_optinteger(L, 5, -1);

  lua_pushinteger(L, luaL_bufcopy(dest, destoff, src, srcoff, srclen));
  return 1;
}

static int buf_slice(lua_State *L)
{
  luaL_BufferObj *b = luaL_checkudata(L, 1, LUAL_BUFFER_MT);
  int start = luaL_optinteger(L, 2, 0);
  int end = luaL_optinteger(L, 3, b->len);

  if (!luaL_bufslice(L, b, start, end)) {
    lua_pushnil(L);
  }
  return 1;
}

/* b:string([start [, end]]) */
static int buf_string(lua_State *L)
{
  luaL_BufferObj *b = luaL_checkudata(L, 1, LUAL_BUFFER_MT);
  int start = luaL_optinteger(L, 2, 0);
  int end = luaL_optinteger(L, 3, b->len);

  if (end < 0 || start > b->len || end > b->len) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushlstring(L, b->ptr + start, end - start);
  return 1;
}

#if 0
static int buf_index(lua_State *L)
{
  luaL_BufferObj *b = luaL_checkudata(L, 1, LUAL_BUFFER_MT);
  int idx = luaL_checkinteger(L, 2);

  if (idx < 0 || idx >= b->allocd) {
    luaL_error(L, "index %d is out of range [0, %d]", idx, b->allocd);
  }

  lua_pushinteger(L, (int)b->ptr[idx]);
  return 1;
}

static int buf_newindex(lua_State *L)
{
  luaL_BufferObj *b = luaL_checkudata(L, 1, LUAL_BUFFER_MT);
  int idx = luaL_checkinteger(L, 2);
  int octet = luaL_checkinteger(L, 3);

  if (idx < 0 || idx >= b->allocd) {
    luaL_error(L, "index %d is out of range [0, %d]", idx, b->allocd);
  }

  if (octet < 0 || octet > 255) {
    luaL_error(L, "octet %d is out of range [0, 255]", octet);
  }

  b->ptr[idx] = (uint8_t)octet;

  return 0;
}
#endif

static int buffer_new(lua_State *L)
{
  if (lua_gettop(L) == 1) {
    if (!lua_isnumber(L, 1) && lua_isstring(L, 1)) {
      size_t len;
      const char *str = lua_tolstring(L, 1, &len);
      luaL_BufferObj *b;

      b = luaL_bufnew(L, len, NULL, NULL, 0);
      luaL_bufwrite(b, 0, str, len);
      return 1;
    }
    luaL_bufnew(L, luaL_checkinteger(L, 1), NULL, NULL, 0);
    return 1;
  }
  return 0;
}

static const luaL_Reg buf_funcs[] = {
  {"__gc", buf_gc},
  {"__tostring", buf_tostring},
  {"__len", buf_len},
#if 0
  {"__index", buf_index},
  {"__newindex", buf_newindex},
#endif
  {"write", buf_write},
  {"string", buf_string},
  {"slice", buf_slice},
  {"copy", buf_copy},
  {NULL, NULL}
};

static const luaL_Reg funcs[] = {
  {"new", buffer_new},
  {NULL, NULL}
};

LUALIB_API int luaopen_buffer(lua_State *L)
{
  luaL_register(L, LUA_BUFFERLIBNAME, funcs);

  luaL_newmetatable(L, LUAL_BUFFER_MT);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  luaL_register(L, NULL, buf_funcs);

  return 1;
}


/* vim:ts=2:sw=2:et:
 */

