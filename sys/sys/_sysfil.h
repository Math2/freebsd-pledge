#ifndef _SYS_SYSFILSET_H_
#define	_SYS_SYSFILSET_H_

#include <sys/types.h>
#include <sys/_bitset.h>

#define	SYSFIL_SHIFT		6	/* enough for 64 */
#define	SYSFIL_SIZE		(1U << SYSFIL_SHIFT)
#define	SYSFIL_MASK		(SYSFIL_SIZE - 1)

#define	SYSFILSET_BITS		(1U << SYSFIL_SHIFT)

BITSET_DEFINE(_sysfilset, SYSFILSET_BITS);
typedef struct _sysfilset sysfilset_t;

#define	SYSFILSET_INITIALIZER	BITSET_T_INITIALIZER(0)

#define	SYSFILSET_MATCH(s, i)	BIT_ISSET(SYSFILSET_BITS, i, s)
#define	SYSFILSET_EQUAL(s, ss)	(BIT_CMP(SYSFILSET_BITS, s, ss) == 0)

#define	SYSFILSET_MASK(t, s)	BIT_AND(SYSFILSET_BITS, t, s)
#define	SYSFILSET_MERGE(t, s)	BIT_OR(SYSFILSET_BITS, t, s)
#define SYSFILSET_FILL(s, i)	BIT_SET(SYSFILSET_BITS, i, s)
#define SYSFILSET_CLEAR(s, i)	BIT_CLR(SYSFILSET_BITS, i, s)
#define	SYSFILSET_FILL_ALL(s)	BIT_FILL(SYSFILSET_BITS, s);

#define	SYSFILSET_IS_RESTRICTED(s) (!BIT_ISFULLSET(SYSFILSET_BITS, s))

#endif
