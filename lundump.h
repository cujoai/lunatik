/*
** $Id: lundump.h,v 1.10 1999/08/16 20:52:00 roberto Exp roberto $
** load pre-compiled Lua chunks
** See Copyright Notice in lua.h
*/

#ifndef lundump_h
#define lundump_h

#include "lobject.h"
#include "lzio.h"

TProtoFunc* luaU_undump1 (lua_State *L, ZIO* Z);	/* load one chunk */
void luaU_badconstant (lua_State *L, const char* s, int i, const TObject* o, TProtoFunc* tf);
					/* handle cases that cannot happen */
double luaU_str2d (lua_State *L, const char* b, const char* where);
					/* convert number from text */

/* definitions for headers of binary files */
#define	VERSION		0x32		/* last format change was in 3.2 */
#define	VERSION0	0x32		/* last major  change was in 3.2 */
#define ID_CHUNK	27		/* binary files start with ESC... */
#define	SIGNATURE	"Lua"		/* ...followed by this signature */

/* formats for error messages */
#define SOURCE		"<%s:%d>"
#define IN		" in %p " SOURCE
#define INLOC		tf,tf->source->str,tf->lineDefined

/* format for numbers in listings and error messages */
#ifndef NUMBER_FMT
#define NUMBER_FMT	"%.16g"		/* LUA_NUMBER */
#endif

/* a multiple of PI for testing native format */
/* multiplying by 1E8 gives non-trivial integer values */
#define	TEST_NUMBER	3.14159265358979323846E8

#endif
