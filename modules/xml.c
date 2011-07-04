/*
 * Copyright (c) 2011 Message Systems, Inc. All rights reserved
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF MESSAGE SYSTEMS
 * The copyright notice above does not evidence any
 * actual or intended publication of such source code.
 *
 * Redistribution of this material is strictly prohibited.
 *
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name OmniTI Computer Consulting, Inc. nor the names
 *       of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written
 *       permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "thrlua.h"
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/tree.h>


#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#define LUA_DISPATCH(n, f) \
     if(!strcmp(k, #n)) { \
       lua_pushlightuserdata(L, udata); \
       lua_pushcclosure(L, f, 1); \
       return 1; \
     }

struct xml_buffer_ptr {
  char *buff;
  int raw_len;
  int len;
  int allocd;
};

struct xpath_iter {
  xmlXPathContextPtr ctxt;
  xmlXPathObjectPtr pobj;
  int cnt;
  int idx;
};

static int xml_save_writer(void *vstr, const char *buffer, int len)
{
  struct xml_buffer_ptr *clv = vstr;

  if (len + clv->len > clv->allocd) {
    int newsize = clv->allocd ? clv->allocd * 2 : 8192;
    char *newbuff;

    while (newsize < len + clv->len) {
      newsize *= 2;
    }

    newbuff = realloc(clv->buff, newsize);
    if (!newbuff) {
      return -1;
    }

    clv->allocd = newsize;
    clv->buff = newbuff;
  }

  memcpy(clv->buff + clv->len, buffer, len);
  clv->len += len;
  return len;
}

static int xml_save_closer(void *vstr)
{
  struct xml_buffer_ptr *clv = vstr;
  if(clv->buff == NULL) return 0;
  clv->buff[clv->len] = '\0';
  return 0;
}

static void xmlSaveToBuffer(lua_State *L, xmlDocPtr doc)
{
  xmlOutputBufferPtr out;
  xmlCharEncodingHandlerPtr enc;
  struct xml_buffer_ptr buf = { NULL,0,0,0 };

  enc = xmlGetCharEncodingHandler(XML_CHAR_ENCODING_UTF8);
  out = xmlOutputBufferCreateIO(xml_save_writer,
                                xml_save_closer,
                                &buf, enc);

  if (!out) {
    luaL_error(L, "unable to create xml output buffer");
  }
  xmlSaveFormatFileTo(out, doc, "utf8", 1);

  lua_pushstring(L, buf.buff);
  free(buf.buff);
}

static int lua_xml_tostring(lua_State *L)
{
  int n;
  xmlDocPtr *docptr = luaL_checkudata(L, 1, "xmldoc");

  n = lua_gettop(L);
  if (n != 1) luaL_error(L, "expects no arguments, got %d", n - 1);
  xmlSaveToBuffer(L, *docptr);
  return 1;
}

static int lua_xmlnode_tostring(lua_State *L)
{
  int n;
  xmlNodePtr node = *(xmlNodePtr*)luaL_checkudata(L, 1, "xmlnode");
  xmlOutputBufferPtr out;
  xmlCharEncodingHandlerPtr enc;
  struct xml_buffer_ptr buf = { NULL, 0, 0, 0 };

  n = lua_gettop(L);
  if (n != 1) luaL_error(L, "expects no arguments, got %d", n - 1);

  enc = xmlGetCharEncodingHandler(XML_CHAR_ENCODING_UTF8);
  out = xmlOutputBufferCreateIO(xml_save_writer,
                                xml_save_closer,
                                &buf, enc);
  if (!out) {
    luaL_error(L, "unable to create xml output buffer");
  }
  xmlNodeDumpOutput(out, node->doc, node, 0, 1, "utf8");
  xmlOutputBufferClose(out);

  lua_pushstring(L, buf.buff);
  free(buf.buff);

  return 1;
}


static int lua_xpath_iter(lua_State *L)
{
  struct xpath_iter *xpi;
  xpi = lua_touserdata(L, lua_upvalueindex(1));
  if(xpi->pobj) {
    if(xpi->idx < xpi->cnt) {
      xmlNodePtr node, *nodeptr;
      node = xmlXPathNodeSetItem(xpi->pobj->nodesetval, xpi->idx);
      xpi->idx++;
      nodeptr = (xmlNodePtr *)lua_newuserdata(L, sizeof(node));
      *nodeptr = node;
      luaL_getmetatable(L, "xmlnode");
      lua_setmetatable(L, -2);
      return 1;
    }
  }
  return 0;
}

static int lua_xpath(lua_State *L)
{
  int n;
  const char *xpathexpr;
  xmlDocPtr doc;
  xmlNodePtr *nodeptr = NULL;
  xmlXPathContextPtr ctxt;
  struct xpath_iter *xpi;

  n = lua_gettop(L);
  doc = *(xmlDocPtr*)luaL_checkudata(L, 1, "xmldoc");
  if (n < 2 || n > 3) luaL_error(L, "expects 1 or 2 arguments, got %d", n);
  if (n == 3) {
    nodeptr = luaL_checkudata(L, 3, "xmlnode");
  }

  xpathexpr = lua_tostring(L, 2);
  if (!xpathexpr) luaL_error(L, "no xpath expression provided");

  ctxt = xmlXPathNewContext(doc);
  if (nodeptr) ctxt->node = *nodeptr;
  if (!ctxt) luaL_error(L, "invalid xpath");

  xpi = (struct xpath_iter *)lua_newuserdata(L, sizeof(*xpi));
  memset(xpi, 0, sizeof(*xpi));
  xpi->ctxt = ctxt;
  xpi->pobj = xmlXPathEval((xmlChar *)xpathexpr, xpi->ctxt);
  if (xpi->pobj && xpi->pobj->type == XPATH_NODESET) {
    xpi->cnt = xmlXPathNodeSetGetLength(xpi->pobj->nodesetval);
  }
  luaL_getmetatable(L, "xpathiter");
  lua_setmetatable(L, -2);
  lua_pushcclosure(L, lua_xpath_iter, 1);
  return 1;
}

static int lua_xmlnode_name(lua_State *L)
{
  xmlNodePtr *nodeptr = luaL_checkudata(L, 1, "xmlnode");
  xmlChar *v;

  if (lua_gettop(L) != 1) {
    luaL_error(L,"must be called with no arguments");
  }

  if ((*nodeptr)->name) {
    lua_pushstring(L, (const char *)((*nodeptr)->name));
  } else {
    lua_pushnil(L);
  }
  return 1;
}

static int lua_xmlnode_attr(lua_State *L)
{
  xmlNodePtr *nodeptr = luaL_checkudata(L, 1, "xmlnode");
  const char *attr;
  xmlChar *v = NULL;

  attr = luaL_checkstring(L, 2);

  switch (lua_gettop(L)) {
    case 3:
      if (!lua_isnil(L, 3)) {
        v = (xmlChar*)lua_tostring(L, 3);
      }
      xmlSetProp(*nodeptr, (xmlChar*)attr, v);
      return 0;

    case 2:
      v = xmlGetProp(*nodeptr, (xmlChar*)attr);
      if (v) {
        lua_pushstring(L, (const char *)v);
        xmlFree(v);
      } else {
        lua_pushnil(L);
      }
      return 1;
  }
  luaL_error(L,"must be called with one argument");
}

static int lua_xmlnode_contents(lua_State *L)
{
  xmlNodePtr node = *(xmlNodePtr*)luaL_checkudata(L, 1, "xmlnode");
  xmlChar *v;
  const char *data;

  switch (lua_gettop(L)) {
    case 2:
      data = luaL_checkstring(L, 2);
      v = xmlEncodeEntitiesReentrant(node->doc, (xmlChar *)data);
      xmlNodeSetContent(node, v);
      return 0;
    case 1:
      v = xmlNodeGetContent(node);
      if (v) {
        lua_pushstring(L, (const char *)v);
        xmlFree(v);
      } else {
        lua_pushnil(L);
      }
      return 1;
  }
  luaL_error(L,"must be called with no arguments");
  return 0;
}

/* This is an iterator returned by node:children() */
static int lua_xmlnode_next(lua_State *L)
{
  xmlNodePtr *nodeptr;
  nodeptr = lua_touserdata(L, lua_upvalueindex(1));
  if(*nodeptr) {
    xmlNodePtr *newnodeptr;
    newnodeptr = (xmlNodePtr *)lua_newuserdata(L, sizeof(*nodeptr));
    *newnodeptr = *nodeptr;
    luaL_getmetatable(L, "xmlnode");
    lua_setmetatable(L, -2);
    *nodeptr = (*nodeptr)->next;
    return 1;
  }
  return 0;
}

static int lua_xmlnode_addchild(lua_State *L)
{
  xmlNodePtr *newnodeptr;
  xmlNodePtr node = *(xmlNodePtr*)luaL_checkudata(L, 1, "xmlnode");
  const char *v;

  v = luaL_checkstring(L, 2);

  newnodeptr = (xmlNodePtr *)lua_newuserdata(L, sizeof(*newnodeptr));
  *newnodeptr = xmlNewChild(node, NULL, (xmlChar *)v, NULL);
  luaL_getmetatable(L, "xmlnode");
  lua_setmetatable(L, -2);
  return 1;
}

static int lua_xmlnode_children(lua_State *L)
{
  xmlNodePtr node, cnode;
  xmlNodePtr *nodeptr = luaL_checkudata(L, 1, "xmlnode");

  node = *nodeptr;
  cnode = node->children;
  nodeptr = lua_newuserdata(L, sizeof(cnode));
  *nodeptr = cnode;
  luaL_getmetatable(L, "xmlnode");
  lua_setmetatable(L, -2);
  lua_pushcclosure(L, lua_xmlnode_next, 1);
  return 1;
}

static int lua_xml_docroot(lua_State *L)
{
  int n;
  xmlDocPtr *docptr = luaL_checkudata(L, 1, "xmldoc");
  xmlNodePtr *ptr;

  n = lua_gettop(L);
  if(n != 1) luaL_error(L, "expects no arguments, got %d", n - 1);

  ptr = lua_newuserdata(L, sizeof(*ptr));
  *ptr = xmlDocGetRootElement(*docptr);
  luaL_getmetatable(L, "xmlnode");
  lua_setmetatable(L, -2);
  return 1;
}

static int lua_xpathiter_gc(lua_State *L)
{
  struct xpath_iter *xpi;
  xpi = lua_touserdata(L, 1);
  xmlXPathFreeContext(xpi->ctxt);
  if(xpi->pobj) xmlXPathFreeObject(xpi->pobj);
  return 0;
}

static int nl_parsexml(lua_State *L)
{
  xmlDocPtr *docptr, doc;
  const char *in;
  size_t inlen;

  if(lua_gettop(L) != 1) luaL_error(L, "parsexml requires one argument");

  in = lua_tolstring(L, 1, &inlen);
  doc = xmlParseMemory(in, inlen);
  if(!doc) {
    lua_pushnil(L);
    return 1;
  }

  docptr = (xmlDocPtr *)lua_newuserdata(L, sizeof(doc));
  *docptr = doc;
  luaL_getmetatable(L, "xmldoc");
  lua_setmetatable(L, -2);
  return 1;
}

static int lua_xmldoc_gc(lua_State *L)
{
  xmlDocPtr *holder = luaL_checkudata(L,1, "xmldoc");

  xmlFreeDoc(*holder);
  return 0;
}

static const struct luaL_reg xmlnode_funcs[] = {
  { "attr", lua_xmlnode_attr },
  { "attribute", lua_xmlnode_attr },
  { "addchild", lua_xmlnode_addchild },
  { "children", lua_xmlnode_children },
  { "contents", lua_xmlnode_contents },
  { "name", lua_xmlnode_name },
  { "tostring", lua_xmlnode_tostring },
  { "__tostring", lua_xmlnode_tostring },
  { NULL, NULL }
};

static const struct luaL_reg xmldoc_funcs[] = {
  { "__gc", lua_xmldoc_gc },
  { "root", lua_xml_docroot },
  { "tostring", lua_xml_tostring },
  { "__tostring", lua_xml_tostring },
  { "xpath", lua_xpath },
  { NULL, NULL }
};

static const struct luaL_reg reg_libxml[] = {
  { "parsexml", nl_parsexml },
  { NULL, NULL }
};

LUALIB_API int luaopen_xml(lua_State *L)
{
  luaL_newmetatable(L, "xmldoc");
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  luaL_register(L, NULL, xmldoc_funcs);

  luaL_newmetatable(L, "xmlnode");
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  luaL_register(L, NULL, xmlnode_funcs);

  luaL_newmetatable(L, "xpathiter");
  lua_pushcfunction(L, lua_xpathiter_gc);
  lua_setfield(L, -2, "__gc");

  luaL_register(L, "xml", reg_libxml);

  return 1;
}

/* vim:ts=2:sw=2:et:
 */
