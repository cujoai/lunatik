/*
** $Id: lref.c,v 1.2 1999/11/10 15:37:50 roberto Exp roberto $
** REF mechanism
** See Copyright Notice in lua.h
*/


#define LUA_REENTRANT

#include "lmem.h"
#include "lref.h"
#include "lstate.h"
#include "lua.h"


int luaR_ref (lua_State *L, const TObject *o, int lock) {
  int ref;
  if (ttype(o) == LUA_T_NIL)
    ref = LUA_REFNIL;
  else {
    if (L->refFree != NONEXT) {  /* is there a free place? */
      ref = L->refFree;
      L->refFree = L->refArray[ref].st;
    }
    else {  /* no more free places */
      luaM_growvector(L, L->refArray, L->refSize, 1, struct ref, refEM, MAX_INT);
      ref = L->refSize++;
    }
    L->refArray[ref].o = *o;
    L->refArray[ref].st = lock ? LOCK : HOLD;
  }
  return ref;
}


void lua_unref (lua_State *L, int ref) {
  if (ref >= 0) {
    if (ref >= L->refSize || L->refArray[ref].st >= 0)
      lua_error(L, "API error - invalid parameter for function `lua_unref'");
    L->refArray[ref].st = L->refFree;
    L->refFree = ref;
  }
}


const TObject *luaR_getref (lua_State *L, int ref) {
  if (ref == LUA_REFNIL)
    return &luaO_nilobject;
  else if (0 <= ref && ref < L->refSize &&
          (L->refArray[ref].st == LOCK || L->refArray[ref].st == HOLD))
    return &L->refArray[ref].o;
  else
    return NULL;
}


static int ismarked (const TObject *o) {
  /* valid only for locked objects */
  switch (o->ttype) {
    case LUA_T_STRING: case LUA_T_USERDATA:
      return o->value.ts->marked;
    case LUA_T_ARRAY:
      return o->value.a->marked;
    case LUA_T_CLOSURE:
      return o->value.cl->marked;
    case LUA_T_PROTO:
      return o->value.tf->marked;
#ifdef DEBUG
    case LUA_T_LINE: case LUA_T_CLMARK:
    case LUA_T_CMARK: case LUA_T_PMARK:
      LUA_INTERNALERROR(L, "invalid type");
#endif
    default:  /* number or cproto */
      return 1;
  }
}


/* for internal debugging only; check if a link of free refs is valid */
#define VALIDLINK(L, st,n)	(NONEXT <= (st) && (st) < (n))

void luaR_invalidaterefs (lua_State *L) {
  int n = L->refSize;
  int i;
  for (i=0; i<n; i++) {
    struct ref *r = &L->refArray[i];
    if (r->st == HOLD && !ismarked(&r->o))
      r->st = COLLECTED;
    LUA_ASSERT(L, (r->st == LOCK && ismarked(&r->o)) ||
                r->st == COLLECTED ||
                r->st == NONEXT ||
               (r->st < n && VALIDLINK(L, L->refArray[r->st].st, n)),
               "inconsistent ref table");
  }
  LUA_ASSERT(L, VALIDLINK(L, L->refFree, n), "inconsistent ref table");
}

