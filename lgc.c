/*
** $Id: lgc.c,v 1.6 1997/10/24 17:17:24 roberto Exp roberto $
** Garbage Collector
** See Copyright Notice in lua.h
*/


#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lua.h"



static int markobject (TObject *o);



/*
** =======================================================
** REF mechanism
** =======================================================
*/

static struct ref {
  TObject o;
  enum {LOCK, HOLD, FREE, COLLECTED} status;
} *refArray = NULL;
static int refSize = 0;


int luaC_ref (TObject *o, int lock)
{
  int ref;
  if (ttype(o) == LUA_T_NIL)
    ref = -1;   /* special ref for nil */
  else {
    for (ref=0; ref<refSize; ref++)
      if (refArray[ref].status == FREE)
        goto found;
    /* no more empty spaces */ {
      int oldSize = refSize;
      refSize = luaM_growvector(&refArray, refSize, struct ref, refEM, MAX_WORD);
      for (ref=oldSize; ref<refSize; ref++)
        refArray[ref].status = FREE;
      ref = oldSize;
    } found:
    refArray[ref].o = *o;
    refArray[ref].status = lock ? LOCK : HOLD;
  }
  return ref;
}


void lua_unref (int ref)
{
  if (ref >= 0 && ref < refSize)
    refArray[ref].status = FREE;
}


TObject* luaC_getref (int ref)
{
  if (ref == -1)
    return &luaO_nilobject;
  if (ref >= 0 && ref < refSize &&
      (refArray[ref].status == LOCK || refArray[ref].status == HOLD))
    return &refArray[ref].o;
  else
    return NULL;
}


static void travlock (void)
{
  int i;
  for (i=0; i<refSize; i++)
    if (refArray[i].status == LOCK)
      markobject(&refArray[i].o);
}


static int ismarked (TObject *o)
{
  switch (o->ttype) {
    case LUA_T_STRING: case LUA_T_USERDATA:
      return o->value.ts->head.marked;
    case LUA_T_FUNCTION:
      return o->value.cl->head.marked;
    case LUA_T_PROTO:
      return o->value.tf->head.marked;
    case LUA_T_ARRAY:
      return o->value.a->head.marked;
    default:  /* nil, number or cproto */
      return 1;
  }
}


static void invalidaterefs (void)
{
  int i;
  for (i=0; i<refSize; i++)
    if (refArray[i].status == HOLD && !ismarked(&refArray[i].o))
      refArray[i].status = COLLECTED;
}



static void hashcallIM (Hash *l)
{
  TObject t;
  ttype(&t) = LUA_T_ARRAY;
  for (; l; l=(Hash *)l->head.next) {
    avalue(&t) = l;
    luaD_gcIM(&t);
  }
}


static void strcallIM (TaggedString *l)
{
  TObject o;
  ttype(&o) = LUA_T_USERDATA;
  for (; l; l=(TaggedString *)l->head.next)
    if (l->constindex == -1) {  /* is userdata? */
      tsvalue(&o) = l;
      luaD_gcIM(&o);
    }
}



static GCnode *listcollect (GCnode *l)
{
  GCnode *frees = NULL;
  while (l) {
    GCnode *next = l->next;
    l->marked = 0;
    while (next && !next->marked) {
      l->next = next->next;
      next->next = frees;
      frees = next;
      next = l->next;
    }
    l = next;
  }
  return frees;
}


static void strmark (TaggedString *s)
{
  if (!s->head.marked)
    s->head.marked = 1;
}


static void protomark (TProtoFunc *f)
{
  if (!f->head.marked) {
    LocVar *v = f->locvars;
    int i;
    f->head.marked = 1;
    if (f->fileName)
      strmark(f->fileName);
    for (i=0; i<f->nconsts; i++)
      markobject(&f->consts[i]);
    if (v) {
      for (; v->line != -1; v++)
        if (v->varname)
          strmark(v->varname);
    }
  }
}


static void funcmark (Closure *f)
{
  if (!f->head.marked) {
    int i;
    f->head.marked = 1;
    for (i=f->nelems; i>=0; i--)
      markobject(&f->consts[i]);
  }
}


static void hashmark (Hash *h)
{
  if (!h->head.marked) {
    int i;
    h->head.marked = 1;
    for (i=0; i<nhash(h); i++) {
      Node *n = node(h,i);
      if (ttype(ref(n)) != LUA_T_NIL) {
        markobject(&n->ref);
        markobject(&n->val);
      }
    }
  }
}


static void globalmark (void)
{
  TaggedString *g;
  for (g=(TaggedString *)luaS_root.next; g; g=(TaggedString *)g->head.next)
    if (g->u.globalval.ttype != LUA_T_NIL) {
      markobject(&g->u.globalval);
      strmark(g);  /* cannot collect non nil global variables */
    }
}


static int markobject (TObject *o)
{
  switch (ttype(o)) {
    case LUA_T_USERDATA:  case LUA_T_STRING:
      strmark(tsvalue(o));
      break;
    case LUA_T_ARRAY:
      hashmark(avalue(o));
      break;
    case LUA_T_FUNCTION:  case LUA_T_MARK:
      funcmark(o->value.cl);
      break;
    case LUA_T_PROTO:
      protomark(o->value.tf);
      break;
    default: break;  /* numbers, cprotos, etc */
  }
  return 0;
}



#define GARBAGE_BLOCK 150

unsigned long luaC_threshold = GARBAGE_BLOCK;


static void markall (void)
{
  luaD_travstack(markobject); /* mark stack objects */
  globalmark();  /* mark global variable values and names */
  travlock(); /* mark locked objects */
  luaT_travtagmethods(markobject);  /* mark fallbacks */
}


long lua_collectgarbage (long limit)
{
  unsigned long recovered = luaO_nblocks;  /* to subtract nblocks after gc */
  Hash *freetable;
  TaggedString *freestr;
  TProtoFunc *freefunc;
  Closure *freeclos;
  markall();
  invalidaterefs();
  freestr = luaS_collector();
  freetable = (Hash *)listcollect(&luaH_root);
  freefunc = (TProtoFunc *)listcollect(&luaF_root);
  freeclos = (Closure *)listcollect(&luaF_rootcl);
  luaC_threshold *= 4;  /* to avoid GC during GC */
  hashcallIM(freetable);  /* GC tag methods for tables */
  strcallIM(freestr);  /* GC tag methods for userdata */
  luaD_gcIM(&luaO_nilobject);  /* GC tag method for nil (signal end of GC) */
  luaH_free(freetable);
  luaS_free(freestr);
  luaF_freeproto(freefunc);
  luaF_freeclosure(freeclos);
  luaM_clearbuffer();
  recovered = recovered-luaO_nblocks;
/*printf("==total %ld  coletados %ld\n", luaO_nblocks+recovered, recovered);*/
  luaC_threshold = (limit == 0) ? 2*luaO_nblocks : luaO_nblocks+limit;
  return recovered;
}


void luaC_checkGC (void)
{
  if (luaO_nblocks >= luaC_threshold)
    lua_collectgarbage(0);
}

