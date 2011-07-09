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

#include "rcluaconfig.h"
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "json/json.h"

#define MT_JSON "libjson:json_object*"

static int ljson_tostring(lua_State *L)
{
  struct json_object *json = luaL_checkudata(L, 1, MT_JSON);

  lua_pushstring(L, json_object_to_json_string(json));
  return 1;
}

static int ljson_gc(lua_State *L)
{
  struct json_object *json = luaL_checkudata(L, 1, MT_JSON);

  json_object_put(json);
  return 0;
}

static int push_json_value(lua_State *L, struct json_object *json)
{
  if (!json) {
    lua_pushnil(L);
    return 1;
  }

  switch (json_object_get_type(json)) {
    case json_type_boolean:
      lua_pushboolean(L, json_object_get_boolean(json) == JSON_TRUE);
      return 1;
    case json_type_double:
      lua_pushnumber(L, json_object_get_double(json));
      return 1;
    case json_type_int:
      lua_pushinteger(L, json_object_get_int(json));
      return 1;
    case json_type_string:
      lua_pushstring(L, json_object_get_string(json));
      return 1;
    default:
      luaL_pushuserptr(L, MT_JSON, json);
      return 1;
  }
}

static int ljson_index(lua_State *L)
{
  struct json_object *json = luaL_checkudata(L, 1, MT_JSON);
  const char *key;
  struct json_object *prop;

  switch (json_object_get_type(json)) {
    case json_type_object:
      key = luaL_checkstring(L, 2);
      prop = json_object_object_get(json, key);
      break;
    case json_type_array:
      prop = json_object_array_get_idx(json,
          luaL_checkinteger(L, 2) - 1);
      break;
    default:
      return 0;
  }

  return push_json_value(L, prop);
}

/* epsilon; a small value that we can use to fuzzy compare */
static const double eps = 1.5e-8;

static struct json_object *make_json_val(lua_State *L, int idx)
{
  struct json_object *val = NULL;
  struct json_object *item;

  switch (lua_type(L, idx)) {
    case LUA_TSTRING:
    {
      size_t l;
      const char *s = lua_tolstring(L, idx, &l);

      return json_object_new_string_len(s, l);
    }

    case LUA_TBOOLEAN:
      return json_object_new_boolean(lua_toboolean(L, idx) ?
          JSON_TRUE : JSON_FALSE);

    case LUA_TUSERDATA:
      return luaL_checkudata(L, idx, MT_JSON);

    case LUA_TNUMBER:
    {
      lua_Number num = lua_tonumber(L, idx);

      if (fabs(num - floor(num)) < eps && num <= INT_MAX && num >= INT_MIN) {
        /* the value is an integer (as best we can tell), then map it
         * as an integer instead of a double */
        return json_object_new_int((int)num);
      }

      return json_object_new_double(num);
    }

    case LUA_TTABLE:
    {
      /* we have some ambiguity here; lua tables are both array and hash,
       * whereas JSON allows for one or the other.  We need to peek in
       * the table to figure out which is appropriate.  We do this by
       * looking up the 1st element; if it is set, the table is an array. */
      lua_pushinteger(L, 1);
      lua_gettable(L, idx);
      if (lua_isnil(L, -1)) {
        /* "must" be an object; iterate the keys and values and put them in */

        val = json_object_new_object();
        /* top of stack is already nil to start the iterator */
        while (lua_next(L, idx)) {
          const char *key;

          item = make_json_val(L, lua_gettop(L));

          if (lua_type(L, -2) == LUA_TSTRING) {
            key = lua_tostring(L, -2);
            json_object_object_add(val, key, item);
          } else {
            /* non-string key; follow best practice and avoid converting
             * it during iteration; make a copy instead */
            lua_pushvalue(L, -2);
            key = lua_tostring(L, -1);
            json_object_object_add(val, key, item);
            lua_pop(L, 1);
          }

          lua_pop(L, 1);
        }

      } else {
        /* "must" be an array */
        int i = 1;

        val = json_object_new_array();

        do {
          item = make_json_val(L, lua_gettop(L));
          lua_pop(L, 1);

          json_object_array_add(val, item);

          lua_pushinteger(L, ++i);
          lua_gettable(L, idx);
        } while (!lua_isnil(L, -1));
      }
      lua_pop(L, 1);

      return val;
    }

    default:
      luaL_error(L, "cannot assign values of type %s",
          lua_typename(L, lua_type(L, idx)));
  }

  return val;
}

static int ljson_newindex(lua_State *L)
{
  struct json_object *json = luaL_checkudata(L, 1, MT_JSON);
  const char *key = NULL;
  int idx;
  struct json_object *val = NULL;

  switch (json_object_get_type(json)) {
    case json_type_array:
      if (!lua_isnumber(L, 2)) {
        luaL_error(L, "json array index must be numeric");
      }
      idx = lua_tonumber(L, 2);
      if (idx < 1) {
        luaL_error(L,
            "lua array semantics are that index operations are 1-based");
      }
      break;
    case json_type_object:
      key = luaL_checkstring(L, 2);
      break;
    default:
      luaL_error(L, "cannot assign to this type of json object");
  }

  if (lua_isnil(L, 3)) {
    /* delete, rather than set */
    if (key) {
      json_object_object_del(json, key);
    } else {
      luaL_error(L, "cannot delete from json arrays");
    }
    return 0;
  }

  val = make_json_val(L, 3);

  /* these take ownership of val */
  if (key) {
    json_object_object_add(json, key, val);
  } else {
    json_object_array_put_idx(json, idx - 1, val);
  }
  return 0;
}

static int next_arr(lua_State *L)
{
  /* our upvalue holds the index of the next item to return */
  int idx = lua_tointeger(L, lua_upvalueindex(1));
  struct json_object *json = luaL_checkudata(L, 1, MT_JSON);
  struct json_object *item;

  item = json_object_array_get_idx(json, idx);

  /* increment upvalue */
  lua_pushinteger(L, idx + 1);
  lua_replace(L, lua_upvalueindex(1));

  if (item) {
    lua_pushinteger(L, idx + 1); /* push the 1-based index */
    return 1 + push_json_value(L, item);
  }
  lua_pushnil(L);
  return 1;
}

static int next_obj(lua_State *L)
{
  /* our upvalue holds the lh_entry of the next item to iterate */
  struct json_lh_entry **ep = lua_touserdata(L, lua_upvalueindex(1));
  struct json_object *json = luaL_checkudata(L, 1, MT_JSON);
  struct json_object *item;
  struct json_lh_entry *entry = *ep;
  const char *key;

  if (!entry) {
    lua_pushnil(L);
    return 1;
  }

  key = (const char*)entry->k;
  item = (struct json_object*)entry->v;

  /* step entry */
  *ep = entry->next;

  lua_pushstring(L, key);
  return 1 + push_json_value(L, item);
}


static int make_iter(lua_State *L)
{
  struct json_object *json = luaL_checkudata(L, 1, MT_JSON);
  struct json_lh_entry **ep;

  switch (json_object_get_type(json)) {
    case json_type_array:
      lua_pushinteger(L, 0);
      lua_pushcclosure2(L, "json:next_arr", next_arr, 1);
      lua_pushvalue(L, 1);
      lua_pushnil(L);
      return 3;
    case json_type_object:
      ep = lua_newuserdata(L, sizeof(*ep));
      *ep = json_object_get_object(json)->head;
      lua_pushcclosure2(L, "json:next_obj", next_obj, 1);
      lua_pushvalue(L, 1);
      lua_pushnil(L);
      return 3;
    default:
      return 0;
  }
}

static int ljson_len(lua_State *L)
{
  struct json_object *json = luaL_checkudata(L, 1, MT_JSON);

  switch (json_object_get_type(json)) {
    case json_type_array:
      lua_pushinteger(L, json_object_array_length(json));
      return 1;
    default:
      lua_pushinteger(L, 0);
      return 1;
  }
}

static const struct luaL_reg json_funcs[] = {
  { "__tostring", ljson_tostring },
  { "__gc", ljson_gc },
  { "__index", ljson_index },
  { "__newindex", ljson_newindex },
  { "__iter", make_iter },
  { "__len", ljson_len },
  { NULL, NULL }
};

static int parse_json(lua_State *L)
{
  struct json_tokener *tok;
  const char *str;
  size_t len;
  struct json_object *json;
  int res;

  str = luaL_checklstring(L, 1, &len);

  tok = json_tokener_new();
  json = json_tokener_parse_ex(tok, str, (int)len);
  if (json == NULL) {
    lua_pushnil(L);
    lua_pushinteger(L, tok->err);
    lua_pushfstring(L, "%s at offset %d",
        json_tokener_errors[tok->err],
        tok->char_offset);
    json_tokener_free(tok);
    return 3;
  }
  json_tokener_free(tok);

  res = push_json_value(L, json);
  json_object_put(json);

  return res;
}

static int ljson_strerror(lua_State *L)
{
  int err = luaL_checkinteger(L, 1);

  if (err < 0 || err > json_tokener_error_parse_comment) {
    lua_pushliteral(L, "unknown error");
  } else {
    lua_pushstring(L, json_tokener_errors[err]);
  }
  return 1;
}

static int encode_json(lua_State *L)
{
  struct json_object *json;

  if (lua_gettop(L)) {
    json = make_json_val(L, 1);
  } else {
    json = json_object_new_object();
  }
  return push_json_value(L, json);
}

static const struct luaL_reg funcs[] = {
  { "decode", parse_json },
  { "encode", encode_json },
  { "new", encode_json },
  { "strerror", ljson_strerror },
  { NULL, NULL }
};

static struct {
  int code;
  const char *name;
} codes[] = {
  { json_tokener_error_depth, "ERROR_DEPTH" },
  { json_tokener_error_parse_eof, "ERROR_PARSE_EOF" },
  { json_tokener_error_parse_unexpected, "ERROR_PARSE_UNEXPECTED" },
  { json_tokener_error_parse_null, "ERROR_PARSE_NULL" },
  { json_tokener_error_parse_boolean, "ERROR_PARSE_BOOLEAN" },
  { json_tokener_error_parse_number, "ERROR_PARSE_NUMBER" },
  { json_tokener_error_parse_array, "ERROR_PARSE_ARRAY" },
  { json_tokener_error_parse_object_key_name, "ERROR_PARSE_OBJECT_KEY_NAME" },
  { json_tokener_error_parse_object_key_sep, "ERROR_PARSE_OBJECT_KEY_SEP" },
  { json_tokener_error_parse_object_value_sep, "ERROR_PARSE_OBJECT_VALUE_SEP" },
  { json_tokener_error_parse_string, "ERROR_PARSE_STRING" },
  { json_tokener_error_parse_comment, "ERROR_PARSE_COMMENT" },
};

static void addref(lua_State *L, void *ptr)
{
  struct json_object *json = ptr;

  json_object_get(json);
}

LUALIB_API int luaopen_json(lua_State *L)
{
  int i;

  luaL_registerptrtype(L, MT_JSON, json_funcs, addref);

  luaL_register(L, "json", funcs);

  for (i = 0; i < sizeof(codes)/sizeof(codes[0]); i++) {
    lua_pushstring(L, codes[i].name);
    lua_pushinteger(L, codes[i].code);
    lua_settable(L, -3);
  }

  return 1;
}

/* vim:ts=2:sw=2:et:
 */

