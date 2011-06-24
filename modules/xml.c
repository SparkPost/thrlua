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
static int
xml_save_writer(void *vstr, const char *buffer, int len) {
  struct xml_buffer_ptr *clv = vstr;
  if(!clv->buff) {
    clv->allocd = 8192;
    clv->buff = malloc(clv->allocd);
  }
  while(len + clv->len > clv->allocd) {
    char *newbuff;
    int newsize = clv->allocd;
    newsize <<= 1;
    newbuff = realloc(clv->buff, newsize);
    if(!newbuff) {
      return -1;
    }
    clv->allocd = newsize;
    clv->buff = newbuff;
  }
  memcpy(clv->buff + clv->len, buffer, len);
  clv->len += len;
  return len;
}
static int
xml_save_closer(void *vstr) {
  struct xml_buffer_ptr *clv = vstr;
  if(clv->buff == NULL) return 0;
  clv->buff[clv->len] = '\0';
  return 0;
}

char *
xmlSaveToBuffer(xmlDocPtr doc) {
  xmlOutputBufferPtr out;
  xmlCharEncodingHandlerPtr enc;
  struct xml_buffer_ptr buf = { NULL,0,0,0 };

  enc = xmlGetCharEncodingHandler(XML_CHAR_ENCODING_UTF8);
  out = xmlOutputBufferCreateIO(xml_save_writer,
                                xml_save_closer,
                                &buf, enc);
  assert(out);
  xmlSaveFormatFileTo(out, doc, "utf8", 1);
  return buf.buff;
}

struct xpath_iter {
  xmlXPathContextPtr ctxt;
  xmlXPathObjectPtr pobj;
  int cnt;
  int idx;
};
static int
lua_xpath_iter(lua_State *L) {
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
static int
lua_xpath(lua_State *L) {
  int n;
  const char *xpathexpr;
  xmlDocPtr *docptr, doc;
  xmlNodePtr *nodeptr = NULL;
  xmlXPathContextPtr ctxt;
  struct xpath_iter *xpi;

  n = lua_gettop(L);
  /* the first arg is implicitly self (it's a method) */
  docptr = lua_touserdata(L, lua_upvalueindex(1));
  if(docptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  if(n < 2 || n > 3) luaL_error(L, "expects 1 or 2 arguments, got %d", n);
  doc = *docptr;
  xpathexpr = lua_tostring(L, 2);
  if(!xpathexpr) luaL_error(L, "no xpath expression provided");
  ctxt = xmlXPathNewContext(doc);
  if(n == 3) {
    nodeptr = lua_touserdata(L, 3);
    if(nodeptr) ctxt->node = *nodeptr;
  }
  if(!ctxt) luaL_error(L, "invalid xpath");

  xpi = (struct xpath_iter *)lua_newuserdata(L, sizeof(*xpi));
  xpi->ctxt = ctxt;
  xpi->pobj = xmlXPathEval((xmlChar *)xpathexpr, xpi->ctxt);
  if(!xpi->pobj || xpi->pobj->type != XPATH_NODESET)
    xpi->cnt = 0;
  else
    xpi->cnt = xmlXPathNodeSetGetLength(xpi->pobj->nodesetval);
  xpi->idx = 0;
  luaL_getmetatable(L, "xpathiter");
  lua_setmetatable(L, -2);
  lua_pushcclosure(L, lua_xpath_iter, 1);
  return 1;
}
static int
lua_xmlnode_name(lua_State *L) {
  xmlNodePtr *nodeptr;
  /* the first arg is implicitly self (it's a method) */
  nodeptr = lua_touserdata(L, lua_upvalueindex(1));
  if(nodeptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  if(lua_gettop(L) == 1) {
    xmlChar *v;
    v = (xmlChar *)(*nodeptr)->name;
    if(v) {
      lua_pushstring(L, (const char *)v);
    }
    else lua_pushnil(L);
    return 1;
  }
  luaL_error(L,"must be called with no arguments");
  return 0;
}
static int
lua_xmlnode_attr(lua_State *L) {
  xmlNodePtr *nodeptr;
  /* the first arg is implicitly self (it's a method) */
  nodeptr = lua_touserdata(L, lua_upvalueindex(1));
  if(nodeptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  if(lua_gettop(L) == 3 && lua_isstring(L,2)) {
    const char *attr = lua_tostring(L,2);
    if(lua_isnil(L,3))
      xmlSetProp(*nodeptr, (xmlChar *)attr, NULL);
    else
      xmlSetProp(*nodeptr, (xmlChar *)attr, (xmlChar *)lua_tostring(L,3));
    return 0;
  }
  if(lua_gettop(L) == 2 && lua_isstring(L,2)) {
    xmlChar *v;
    const char *attr = lua_tostring(L,2);
    v = xmlGetProp(*nodeptr, (xmlChar *)attr);
    if(v) {
      lua_pushstring(L, (const char *)v);
      xmlFree(v);
    }
    else lua_pushnil(L);
    return 1;
  }
  luaL_error(L,"must be called with one argument");
  return 0;
}
static int
lua_xmlnode_contents(lua_State *L) {
  xmlNodePtr *nodeptr;
  /* the first arg is implicitly self (it's a method) */
  nodeptr = lua_touserdata(L, lua_upvalueindex(1));
  if(nodeptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  if(lua_gettop(L) == 2 && lua_isstring(L,2)) {
    const char *data = lua_tostring(L,2);
    xmlChar *enc = xmlEncodeEntitiesReentrant((*nodeptr)->doc, (xmlChar *)data);
    xmlNodeSetContent(*nodeptr, (xmlChar *)enc);
    return 0;
  }
  if(lua_gettop(L) == 1) {
    xmlChar *v;
    v = xmlNodeGetContent(*nodeptr);
    if(v) {
      lua_pushstring(L, (const char *)v);
      xmlFree(v);
    }
    else lua_pushnil(L);
    return 1;
  }
  luaL_error(L,"must be called with no arguments");
  return 0;
}
static int
lua_xmlnode_next(lua_State *L) {
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
static int
lua_xmlnode_addchild(lua_State *L) {
  xmlNodePtr *nodeptr;
  nodeptr = lua_touserdata(L, lua_upvalueindex(1));
  if(nodeptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  if(lua_gettop(L) == 2 && lua_isstring(L,2)) {
    xmlNodePtr *newnodeptr;
    newnodeptr = (xmlNodePtr *)lua_newuserdata(L, sizeof(*nodeptr));
    *newnodeptr = xmlNewChild(*nodeptr, NULL,
                              (xmlChar *)lua_tostring(L,2), NULL);
    luaL_getmetatable(L, "xmlnode");
    lua_setmetatable(L, -2);
    return 1;
  }
  luaL_error(L,"must be called with one argument");
  return 0;
}
static int
lua_xmlnode_children(lua_State *L) {
  xmlNodePtr *nodeptr, node, cnode;
  /* the first arg is implicitly self (it's a method) */
  nodeptr = lua_touserdata(L, lua_upvalueindex(1));
  if(nodeptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  node = *nodeptr;
  cnode = node->children;
  nodeptr = lua_newuserdata(L, sizeof(cnode));
  *nodeptr = cnode;
  luaL_getmetatable(L, "xmlnode");
  lua_setmetatable(L, -2);
  lua_pushcclosure(L, lua_xmlnode_next, 1);
  return 1;
}
static int
lua_xml_tostring(lua_State *L) {
  int n;
  xmlDocPtr *docptr;
  char *xmlstring;
  n = lua_gettop(L);
  /* the first arg is implicitly self (it's a method) */
  docptr = lua_touserdata(L, lua_upvalueindex(1));
  if(docptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  if(n != 1) luaL_error(L, "expects no arguments, got %d", n - 1);
  xmlstring = xmlSaveToBuffer(*docptr);
  lua_pushstring(L, xmlstring);
  free(xmlstring);
  return 1;
}
static int
lua_xml_docroot(lua_State *L) {
  int n;
  xmlDocPtr *docptr;
  xmlNodePtr *ptr;
  n = lua_gettop(L);
  /* the first arg is implicitly self (it's a method) */
  docptr = lua_touserdata(L, lua_upvalueindex(1));
  if(docptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  if(n != 1) luaL_error(L, "expects no arguments, got %d", n - 1);
  ptr = lua_newuserdata(L, sizeof(*ptr));
  *ptr = xmlDocGetRootElement(*docptr);
  luaL_getmetatable(L, "xmlnode");
  lua_setmetatable(L, -2);
  return 1;
}
static int
lua_xpathiter_gc(lua_State *L) {
  struct xpath_iter *xpi;
  xpi = lua_touserdata(L, 1);
  xmlXPathFreeContext(xpi->ctxt);
  if(xpi->pobj) xmlXPathFreeObject(xpi->pobj);
  return 0;
}
static int
xmlnode_index_func(lua_State *L) {
  int n;
  const char *k;
  xmlNodePtr *udata;
  n = lua_gettop(L); /* number of arguments */
  assert(n == 2);
  if(!luaL_checkudata(L, 1, "xmlnode")) {
    luaL_error(L, "metatable error, arg1 not a xmlnode!");
  }
  udata = lua_touserdata(L, 1);
  if(!lua_isstring(L, 2)) {
    luaL_error(L, "metatable error, arg2 not a string!");
  }
  k = lua_tostring(L, 2);
  switch(*k) {
    case 'a':
      LUA_DISPATCH(attr, lua_xmlnode_attr);
      LUA_DISPATCH(attribute, lua_xmlnode_attr);
      LUA_DISPATCH(addchild, lua_xmlnode_addchild);
      break;
    case 'c':
      LUA_DISPATCH(children, lua_xmlnode_children);
      LUA_DISPATCH(contents, lua_xmlnode_contents);
      break;
    case 'n':
      LUA_DISPATCH(name, lua_xmlnode_name);
      break;
    default:
      break;
  }
  luaL_error(L, "xmlnode no such element: %s", k);
  return 0;
}
static int
nl_parsexml(lua_State *L) {
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
static int
lua_xmldoc_gc(lua_State *L) {
  xmlDocPtr *holder;
  holder = (xmlDocPtr *)lua_touserdata(L,1);
  xmlFreeDoc(*holder);
  return 0;
}
static int
xmldoc_index_func(lua_State *L) {
  int n;
  const char *k;
  xmlDocPtr *udata;
  n = lua_gettop(L); /* number of arguments */
  assert(n == 2);
  if(!luaL_checkudata(L, 1, "xmldoc")) {
    luaL_error(L, "metatable error, arg1 not a xmldoc!");
  }
  udata = lua_touserdata(L, 1);
  if(!lua_isstring(L, 2)) {
    luaL_error(L, "metatable error, arg2 not a string!");
  }
  k = lua_tostring(L, 2);
  switch(*k) {
    case 'r':
     LUA_DISPATCH(root, lua_xml_docroot);
     break;
    case 't':
     LUA_DISPATCH(tostring, lua_xml_tostring);
     break;
    case 'x':
     LUA_DISPATCH(xpath, lua_xpath);
     break;
    default:
     break;
  }
  luaL_error(L, "xmldoc no such element: %s", k);
  return 0;
}

static const struct luaL_reg reg_libxml[] =
{
  { "parsexml", nl_parsexml },
	{ NULL,		NULL }
};

LUALIB_API int luaopen_xml(lua_State *L)
{
  luaL_newmetatable(L, "xmldoc");
  lua_pushcfunction(L, lua_xmldoc_gc);
  lua_setfield(L, -2, "__gc");
  luaL_newmetatable(L, "xmldoc");
  lua_pushcclosure(L, xmldoc_index_func, 0);
  lua_setfield(L, -2, "__index");

  luaL_newmetatable(L, "xmlnode");
  lua_pushcclosure(L, xmlnode_index_func, 0);
  lua_setfield(L, -2, "__index");

  luaL_newmetatable(L, "xpathiter");
  lua_pushcfunction(L, lua_xpathiter_gc);
  lua_setfield(L, -2, "__gc");

  luaL_register(L, LUA_XMLLIBNAME, reg_libxml);

  return 1;
}
