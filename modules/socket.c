/*
 * Copyright (c) 2011 Message Systems, Inc. All rights reserved
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF MESSAGE SYSTEMS
 * The copyright notice above does not evidence any
 * actual or intended publication of such source code.
 *
 * Redistribution of this material is strictly prohibited.
 *
 * PRELIMINARY: This is unsupported code intended to facilitate our
 * internal test harness.  It should not be used in production.
 */

#include "rcluaconfig.h"
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include <sys/socket.h>
#include <arpa/inet.h>

#define SOCKET_MT_TCP "socket.tcp"
#define SOCKET_MT_BUF "socket.buf"

struct lua_socket_buf {
  char *buf;
  size_t len;
  size_t alloc;
  int should_free;
};

struct lua_socket {
  int fd;
};

static int push_err_info(lua_State *L, int err)
{
  lua_pushnil(L);
  lua_pushinteger(L, err);
  lua_pushstring(L, strerror(err));
  return 3;
}

static int buf_gc(lua_State *L)
{
  struct lua_socket_buf *s = luaL_checkudata(L, 1, SOCKET_MT_BUF);

  if (s->should_free && s->buf) {
    free(s->buf);
    s->buf = NULL;
  }

  return 0;
}

static int buf_len(lua_State *L)
{
  struct lua_socket_buf *s = luaL_checkudata(L, 1, SOCKET_MT_BUF);

  lua_pushnumber(L, s->len);
  return 1;
}

static int buf_to_string(lua_State *L)
{
  struct lua_socket_buf *s = luaL_checkudata(L, 1, SOCKET_MT_BUF);

  lua_pushlstring(L, s->buf, s->len);
  return 1;
}

static luaL_reg buf_funcs[] = {
  { "__gc", buf_gc },
  { "__len", buf_len },
  { "__tostring", buf_to_string },
  { NULL, NULL },
};

static int tcp_close(lua_State *L)
{
  struct lua_socket *s = luaL_checkudata(L, 1, SOCKET_MT_TCP);

  if (s->fd >= 0) {
    close(s->fd);
    s->fd = -1;
  }

  return 0;
}

static int tcp_accept(lua_State *L)
{
  struct lua_socket *s = luaL_checkudata(L, 1, SOCKET_MT_TCP);
  struct lua_socket *client;
  int res;
  struct sockaddr_storage sa;
  int salen = sizeof(sa);

  client = lua_newuserdata(L, sizeof(*s));
  client->fd = accept(s->fd, (struct sockaddr*)&sa, &salen);

  if (client->fd == -1) {
    return push_err_info(L, errno);
  }

  luaL_getmetatable(L, SOCKET_MT_TCP);
  lua_setmetatable(L, -2);
  return 1;
}

static int tcp_listen(lua_State *L)
{
  struct lua_socket *s = luaL_checkudata(L, 1, SOCKET_MT_TCP);
  lua_Integer backlog = luaL_optinteger(L, 2, 250);
  int res;

  res = listen(s->fd, backlog);

  if (res == 0) {
    lua_pushboolean(L, 1);
    return 1;
  }

  return push_err_info(L, errno);
}

static int tcp_read(lua_State *L)
{
  struct lua_socket *s = luaL_checkudata(L, 1, SOCKET_MT_TCP);
  lua_Integer size = luaL_optinteger(L, 2, 8192);
  int res;
  struct lua_socket_buf *buf;

  buf = lua_newuserdata(L, sizeof(*buf) + size);
  buf->buf = (char*)(buf + 1);
  buf->alloc = size;
  buf->len = 0;
  buf->should_free = 0;

  luaL_getmetatable(L, SOCKET_MT_BUF);
  lua_setmetatable(L, -2);

  res = recv(s->fd, buf->buf, buf->alloc, 0);

  if (res >= 0) {
    buf->len = res;
    return 1;
  }

  return push_err_info(L, errno);
}

static int tcp_write(lua_State *L)
{
  struct lua_socket *s = luaL_checkudata(L, 1, SOCKET_MT_TCP);
  lua_Integer size = luaL_optinteger(L, 3, 0);
  const void *bp;
  int res;
  struct lua_socket_buf *buf = NULL;

  bp = lua_touserdata(L, 2);
  if (!bp) {
    bp = luaL_checkstring(L, 2);
    if (size == 0) {
      size = strlen((char*)bp);
    }
  } else {
    buf = luaL_checkudata(L, 2, SOCKET_MT_BUF);
    if (size == 0) {
      size = buf->len;
    }
    bp = buf->buf;
  }

  res = send(s->fd, bp, size, 0);

  if (res >= 0) {
    lua_pushinteger(L, res);
    return 1;
  }

  return push_err_info(L, errno);
}

static int push_sockaddr_string(lua_State *L, struct sockaddr *sa, int len)
{
  struct sockaddr_in *sin = (struct sockaddr_in*)sa;
  char buf[256];
  int port;
  char *cp;

  switch (sa->sa_family) {
    case AF_INET:
      port = ntohs(sin->sin_port);
      if (sin->sin_addr.s_addr == INADDR_ANY) {
        lua_pushfstring(L, "*:%d", port);
        return 1;
      }
      inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf));
      cp = strchr(buf, ':');
      if (cp) *cp = '\0';
      lua_pushfstring(L, "%s:%d", buf, port);
      return 1;
  }

  return 0;
}

static int tcp_sockname(lua_State *L)
{
  struct lua_socket *s = luaL_checkudata(L, 1, SOCKET_MT_TCP);
  struct sockaddr_storage sa;
  int salen = sizeof(&sa);
  int res;

  res = getsockname(s->fd, (struct sockaddr*)&sa, &salen);

  if (res == 0) {
    return push_sockaddr_string(L, (struct sockaddr*)&sa, salen);
  }

  return push_err_info(L, errno);
}

static int tcp_peername(lua_State *L)
{
  struct lua_socket *s = luaL_checkudata(L, 1, SOCKET_MT_TCP);
  struct sockaddr_storage sa;
  int salen = sizeof(&sa);
  int res;

  res = getpeername(s->fd, (struct sockaddr*)&sa, &salen);

  if (res == 0) {
    return push_sockaddr_string(L, (struct sockaddr*)&sa, salen);
  }

  return push_err_info(L, errno);
}

static int tcp_bind(lua_State *L)
{
  struct lua_socket *s = luaL_checkudata(L, 1, SOCKET_MT_TCP);
  const char *name = luaL_checkstring(L, 2);
  int porti = luaL_optinteger(L, 3, 0);
  uint16_t port;
  struct sockaddr_in sin;
  int res;

  memset(&sin, 0, sizeof(sin));

  if (porti < 0 || porti > 65535) {
    luaL_error(L, "port %d is out of range", porti);
  }
  port = (uint16_t)porti;
  res = inet_pton(AF_INET, name, &sin.sin_addr);
  if (res != 1) {
    return push_err_info(L, errno);
  }
  sin.sin_family = AF_INET;
  sin.sin_port = htons(port);

  res = bind(s->fd, (struct sockaddr*)&sin, sizeof(sin));

  if (res == 0) {
    lua_pushboolean(L, 1);
    return 1;
  }
  return push_err_info(L, errno);
}

static luaL_reg tcp_funcs[] = {
  { "close", tcp_close },
  { "__gc", tcp_close },
  { "listen", tcp_listen },
  { "accept", tcp_accept },
  { "read", tcp_read },
  { "write", tcp_write },
  { "sockname", tcp_sockname },
  { "peername", tcp_peername },
  { "bind", tcp_bind },
  { NULL, NULL },
};

static int tcp_open(lua_State *L)
{
  struct lua_socket *s;

  s = lua_newuserdata(L, sizeof(*s));
  s->fd = socket(PF_INET, SOCK_STREAM, 0);
  if (s->fd == -1) {
    return push_err_info(L, errno);
  }

  luaL_getmetatable(L, SOCKET_MT_TCP);
  lua_setmetatable(L, -2);
  return 1;
}

static luaL_reg funcs[] = {
  { "tcp", tcp_open },
  { NULL, NULL }
};

LUALIB_API int luaopen_socket(lua_State *L)
{
  luaL_newmetatable(L, SOCKET_MT_TCP);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  luaL_register(L, NULL, tcp_funcs);

  luaL_newmetatable(L, SOCKET_MT_BUF);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  luaL_register(L, NULL, buf_funcs);

  luaL_register(L, "socket", funcs);
  return 1;
}

/* vim:ts=2:sw=2:et:
 */

