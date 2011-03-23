/*
** $Id: lstring.c,v 2.8.1.1 2007/12/27 13:02:25 roberto Exp $
** String table (keeps all strings handled by Lua)
** See Copyright Notice in lua.h
*/

#define lstring_c
#define LUA_CORE

#include "thrlua.h"

/* MUST be called with the string table locked by the caller */
void luaS_resize (global_State *g, stringtable *tb, int newsize)
{
  struct stringtable_node **newhash;
  int i;

  newhash = luaM_reallocG(g, LUA_MEM_STRING_TABLE, NULL, 0,
              newsize * sizeof(struct stringtable_node *));
  memset(newhash, 0, newsize * sizeof(struct stringtable_node*));

  /* rehash */
  for (i=0; i<tb->size; i++) {
    struct stringtable_node *p = tb->hash[i];
    while (p) {  /* for each node in the list */
      struct stringtable_node *next = p->next;  /* save next */
      unsigned int h = p->str->tsv.hash;
      int h1 = lmod(h, newsize);  /* new position */
      lua_assert(cast_int(h%newsize) == lmod(h, newsize));
      p->next = newhash[h1];  /* chain it */
      newhash[h1] = p;
      p = next;
    }
  }
  luaM_reallocG(g, LUA_MEM_STRING_TABLE, tb->hash, 
    tb->size * sizeof(struct stringtable_node *), 0);
  tb->size = newsize;
  tb->hash = newhash;
}


/* MUST be called with the string table locked */
static TString *newlstr (lua_State *L, const char *str, size_t l,
                                       unsigned int h) {
  TString *ts;
  stringtable *tb;
  struct stringtable_node *n;

  if (l+1 > (MAX_SIZET - sizeof(TString))/sizeof(char))
    luaM_toobig(L);
  ts = luaC_newobjv(L, LUA_TSTRING, (l+1)*sizeof(char)+sizeof(TString));
  ts->tsv.len = l;
  ts->tsv.hash = h;
  memcpy(ts+1, str, l*sizeof(char));
  ((char *)(ts+1))[l] = '\0';  /* ending 0 */

  tb = &L->strt;

  n = luaM_malloc(L, LUA_MEM_STRING_TABLE_NODE, sizeof(*n));
  n->str = ts;

  h = lmod(h, tb->size);
  n->next = tb->hash[h];  /* chain new entry */
  tb->hash[h] = n;
  tb->nuse++;
  if (tb->nuse > cast(uint32_t, tb->size) && tb->size <= MAX_INT/2)
    luaS_resize(G(L), tb, tb->size*2);  /* too crowded */
  return ts;
}


TString *luaS_newlstr (lua_State *L, const char *str, size_t l) {
  struct stringtable_node *o;
  unsigned int h = cast(unsigned int, l);  /* seed */
  size_t step = (l>>5)+1;  /* if string is too long, don't hash all its chars */
  size_t l1;
  TString *ts = NULL;

  for (l1=l; l1>=step; l1-=step)  /* compute hash */
    h = h ^ ((h<<5)+(h>>2)+cast(unsigned char, str[l1-1]));
  lua_lock(L);
  LUAI_TRY_BLOCK(L) {
    for (o = L->strt.hash[lmod(h, L->strt.size)];
        o != NULL;
        o = o->next) {
      ts = o->str;
      if (ts->tsv.len == l && (memcmp(str, getstr(ts), l) == 0)) {
        break;
      }
      ts = NULL;
    }
    if (ts == NULL) {
      ts = newlstr(L, str, l, h);  /* not found */
    }
  } LUAI_TRY_FINALLY(L) {
    lua_unlock(L);
  } LUAI_TRY_END(L);
  return ts;
}


Udata *luaS_newudata (lua_State *L, size_t s, Table *e) {
  Udata *u;
  if (s > MAX_SIZET - sizeof(Udata))
    luaM_toobig(L);
  u = luaC_newobjv(L, LUA_TUSERDATA, s + sizeof(Udata));
  u->uv.len = s;
  u->uv.metatable = NULL;
  u->uv.env = &e->gch;
  return u;
}

/* vim:ts=2:sw=2:et:
 */
