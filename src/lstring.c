/*
** $Id: lstring.c,v 2.8.1.1 2007/12/27 13:02:25 roberto Exp $
** String table (keeps all strings handled by Lua)
** See Copyright Notice in lua.h
*/

#define lstring_c
#define LUA_CORE

#include "thrlua.h"

/* the string table is thread local, but can be walked by a
 * collecting thread, so we need a lock */

static void strt_lock(lua_State *L, stringtable *strt)
{
  int r;
  do {
    r = pthread_mutex_lock(&strt->lock);
  } while (r == EINTR || r == EAGAIN);
  if (r) {
    luaL_error(L, "strt lock failed with errno %d: %s\n",
      errno, strerror(errno));
  }
}

static void strt_unlock(lua_State *L, stringtable *strt)
{
  int r;
  do {
    r = pthread_mutex_unlock(&strt->lock);
  } while (r == EINTR || r == EAGAIN);
  if (r) {
    luaL_error(L, "strt unlock failed with errno %d: %s\n",
      errno, strerror(errno));
  }
}

/* MUST be called with the string table locked by the caller */
void luaS_resize (global_State *g, stringtable *tb, int newsize)
{
  struct stringtable_node **newhash;
  int i;
  thr_State *pt = get_per_thread(g);

  newhash = luaM_reallocG(g, NULL, 0,
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
  luaM_reallocG(g, tb->hash, 
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
  thr_State *pt = get_per_thread(G(L));

  if (l+1 > (MAX_SIZET - sizeof(TString))/sizeof(char))
    luaM_toobig(L);
  ts = luaC_newobjv(G(L), LUA_TSTRING, (l+1)*sizeof(char)+sizeof(TString));
  ts->tsv.len = l;
  ts->tsv.hash = h;
  memcpy(ts+1, str, l*sizeof(char));
  ((char *)(ts+1))[l] = '\0';  /* ending 0 */

  tb = &pt->strt;

  n = luaM_malloc(L, sizeof(*n)); /* FIXME: slab candidate */
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
  thr_State *pt = get_per_thread(G(L));
  TString *ts = NULL;

  for (l1=l; l1>=step; l1-=step)  /* compute hash */
    h = h ^ ((h<<5)+(h>>2)+cast(unsigned char, str[l1-1]));
  strt_lock(L, &pt->strt);
  LUAI_TRY_BLOCK(L) {
    for (o = pt->strt.hash[lmod(h, pt->strt.size)];
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
    strt_unlock(L, &pt->strt);
  } LUAI_TRY_END(L);
  return ts;
}


Udata *luaS_newudata (lua_State *L, size_t s, Table *e) {
  Udata *u;
  if (s > MAX_SIZET - sizeof(Udata))
    luaM_toobig(L);
  u = luaC_newobjv(G(L), LUA_TUSERDATA, s + sizeof(Udata));
//  u = cast(Udata *, luaM_malloc(L, s + sizeof(Udata)));
//  u->uv.marked = luaC_white(G(L));  /* is not finalized */
//  u->uv.gch.tt = LUA_TUSERDATA;
  u->uv.len = s;
  u->uv.metatable = NULL;
  u->uv.env = &e->gch;
  /* chain it on udata list (after main thread) */
//  u->uv.next = G(L)->mainthread->next;
//  G(L)->mainthread->next = obj2gco(u);
  return u;
}

/* vim:ts=2:sw=2:et:
 */
