#ifndef _SYS_SYSFIL_H_
#define	_SYS_SYSFIL_H_

#include <sys/types.h>
#ifdef _KERNEL
#include <sys/systm.h>
#include <sys/bitset.h>
#include <sys/_sysfil.h>
#include <sys/ucred.h>
#include <sys/proc.h>
#include <sys/mutex.h>
#include <sys/lock.h>
#include <sys/mman.h>
#include <sys/capsicum.h>
#endif

/*
 * SYSFIL_DEFAULT is a generic category for miscellaneous operations that must
 * be restricted.  It will be lost after the first pledge().  It must be zero
 * for certain structures to correctly initialize with its value (such as
 * struct fileops/cdevsw).
 */
#define	SYSFIL_DEFAULT		0
/*
 * SYSFIL_UNCAPSICUM represents the state of not being in Capsicum capability
 * mode.  It will be lost after entering capability mode (and like any other
 * sysfil, it cannot be regained).  Its index is also defined in
 * <sys/_sysfil.h> (to avoid having to include all of the sysfil stuff to test
 * for capability mode).
 */
#define	SYSFIL_UNCAPSICUM	1
/*
 * SYSFIL_ALWAYS is for various operations that should always be allowed.  It
 * should never be lost.
 */
#define	SYSFIL_ALWAYS		2

#define	SYSFIL_STDIO		3
#define	SYSFIL_PATH		4
#define	SYSFIL_RPATH		5
#define	SYSFIL_WPATH		6
#define	SYSFIL_CPATH		7
#define	SYSFIL_DPATH		8
#define	SYSFIL_FATTR		9
#define	SYSFIL_FLOCK		10
#define	SYSFIL_TTY		11
#define	SYSFIL_NET		12
#define	SYSFIL_PROC		13
#define	SYSFIL_THREAD		14
#define	SYSFIL_EXEC		15
#define	SYSFIL_UNVEIL		16
#define	SYSFIL_RLIMIT		17
#define	SYSFIL_SETTIME		18
#define	SYSFIL_ID		19
#define	SYSFIL_CHOWN		20
#define	SYSFIL_MLOCK		21
#define	SYSFIL_AIO		22
#define	SYSFIL_EXTATTR		23
#define	SYSFIL_ACL		24
#define	SYSFIL_CPUSET		25
#define	SYSFIL_SYSVIPC		26
#define	SYSFIL_POSIXIPC		27
#define	SYSFIL_POSIXRT		28
#define	SYSFIL_MAC		29
#define	SYSFIL_CHROOT		30
#define	SYSFIL_JAIL		31
#define	SYSFIL_SCHED		32
#define	SYSFIL_ERROR		33
#define	SYSFIL_PS		34
#define	SYSFIL_INET		35
#define	SYSFIL_INET_RAW		36
#define	SYSFIL_UNIX		37
#define	SYSFIL_MCAST		38
#define	SYSFIL_SIGTRAP		39
#define	SYSFIL_CHMOD_SPECIAL	40
#define	SYSFIL_SYSFLAGS		41
#define	SYSFIL_ANY_AF		42
#define	SYSFIL_ANY_PRIV		43
#define	SYSFIL_SENDFILE		44
#define	SYSFIL_MOUNT		45
#define	SYSFIL_QUOTA		46
#define	SYSFIL_FH		47
#define	SYSFIL_RECVFD		48
#define	SYSFIL_SENDFD		49
#define	SYSFIL_PROT_EXEC	50
#define	SYSFIL_ANY_PROCESS	51
#define	SYSFIL_ANY_IOCTL	52
#define	SYSFIL_ANY_SOCKOPT	53
#define	SYSFIL_CRYPTODEV	54
#define	SYSFIL_ROUTE		55
#define	SYSFIL_WROUTE		56
#define	SYSFIL_FFCLOCK		57
#define	SYSFIL_SETFIB		58
#define	SYSFIL_SAME_SESSION	59
#define	SYSFIL_SAME_PGRP	60
#define	SYSFIL_CHILD_PROCESS	61
#define	SYSFIL_PROT_EXEC_LOOSE	62
#define	SYSFIL_SOCKET_IOCTL	63
#define	SYSFIL_BPF		64
#define	SYSFIL_PFIL		65
#define	SYSFIL_LAST		65 /* UPDATE ME!!! */

#ifdef _KERNEL
CTASSERT(SYSFIL_UNCAPSICUM == SYSFILSET_NOT_IN_CAPABILITY_MODE_BIT);
CTASSERT(SYSFIL_LAST < SYSFIL_SIZE);
#endif

/*
 * Some syscalls are assigned to sysfils that may seem to be less restrictive
 * than they should be.  Usually these syscalls will be doing their own
 * checking and only allow safe operations.  These aliases are used to keep
 * track of them and make it more explicit.
 */
#ifdef _KERNEL
/*
 * SYSFIL_CAPCOMPAT is for certain syscalls that are allowed under Capsicum but
 * not under OpenBSD's "stdio" pledge.  This is to get at least some basic
 * level of compatibility when attempting to run Capsicum applications with
 * inherited pledges.
 */
#define	SYSFIL_CAPCOMPAT	SYSFIL_STDIO
/* Can do certain operations on self. */
#define	SYSFIL_PROC_CHECKED	SYSFIL_STDIO
#define	SYSFIL_THREAD_CHECKED	SYSFIL_ALWAYS
#define	SYSFIL_CPUSET_CHECKED	SYSFIL_SCHED
/* Creation of anonymous memory objects are allowed. */
#define	SYSFIL_POSIXIPC_CHECKED	SYSFIL_STDIO
/*
 * SYSFIL_CHOWN is not required for all chown(2) syscalls.  It represents the
 * ability to set the file's owner UID to something different or set its group
 * GID to something different that the process is not a member of.
 */
#define	SYSFIL_CHOWN_CHECKED	SYSFIL_FATTR
/* Retrieving correction delta with adjtime(2) is allowed. */
#define	SYSFIL_SETTIME_CHECKED	SYSFIL_STDIO
#define	SYSFIL_SCTP		SYSFIL_NET
#endif

#define	SYSFIL_VALID(i)		((i) >= 0 && (i) <= SYSFIL_LAST)
#define	SYSFIL_USER_VALID(i)	(SYSFIL_VALID(i) && (i) >= SYSFIL_STDIO)

/* sysfilctl() flags and other constants */

#define	SYSFILCTL_RESTRICT	(1 << 0)
#define	SYSFILCTL_OPTIONAL	(1 << 1)
#define	SYSFILCTL_REQUIRE	(1 << 2)
#define	SYSFILCTL_ON_SELF	(1 << 16)
#define	SYSFILCTL_ON_EXEC	(1 << 17)
#define	SYSFILCTL_ON_BOTH	(SYSFILCTL_ON_SELF | SYSFILCTL_ON_EXEC)

#define	SYSFILCTL_MAX_SELS	1024

#define	SYSFILSEL_ON_SELF	(1 << 16)
#define	SYSFILSEL_ON_EXEC	(1 << 17)
#define	SYSFILSEL_ON_BOTH	(SYSFILSEL_ON_SELF | SYSFILSEL_ON_EXEC)
#define	SYSFILSEL_SYSFIL(s)	(s & ((1 << 16) - 1))

int sysfilctl(int flags, size_t selc, const int *selv);


#ifdef _KERNEL

#define	SYSFIL_FAILED_ERRNO	ECAPMODE

static inline int
sysfil_match_cred(const struct ucred *cr, int sf) {
#ifdef SYSFIL
	return (SYSFILSET_MATCH(&cr->cr_sysfilset, sf));
#else
	return (1);
#endif
}

static inline int
sysfil_check_cred(const struct ucred *cr, int sf)
{
	if (__predict_false(!SYSFIL_VALID(sf)))
		return (EINVAL);
	if (__predict_false(!sysfil_match_cred(cr, sf)))
		return (SYSFIL_FAILED_ERRNO);
	return (0);
}


static inline int
sysfil_check(const struct thread *td, int sf)
{
	return (sysfil_check_cred(td->td_ucred, sf));
}

void sysfil_violation(struct thread *, int sf, int error);

/*
 * Note: sysfil_require() may acquire the PROC_LOCK to send a violation signal.
 * Thus it must not be called with the PROC_LOCK (or any other incompatible
 * lock) currently being held.
 */
static inline int
sysfil_require(struct thread *td, int sf)
{
	int error;
	PROC_LOCK_ASSERT(td->td_proc, MA_NOTOWNED);
	error = sysfil_check(td, sf);
	if (__predict_false(error))
		sysfil_violation(td, sf, error);
	return (error);
}

static inline int
sysfil_failed(struct thread *td, int sf)
{
	sysfil_violation(td, sf, SYSFIL_FAILED_ERRNO);
	return (SYSFIL_FAILED_ERRNO);
}

int sysfil_require_vm_prot(struct thread *, vm_prot_t prot, bool loose);
int sysfil_require_ioctl(struct thread *, int sf, u_long com);
int sysfil_require_af(struct thread *, int af);
int sysfil_require_sockopt(struct thread *, int level, int name);

int sysfil_priv_check(struct ucred *, int priv);

static inline void
sysfil_cred_init(struct ucred *cr)
{
#ifdef SYSFIL
	SYSFILSET_FILL_ALL(&cr->cr_sysfilset);
	SYSFILSET_FILL_ALL(&cr->cr_sysfilset_exec);
#endif
}

static inline bool
sysfil_cred_need_exec_switch(const struct ucred *cr)
{
#ifdef SYSFIL
	return (!SYSFILSET_EQUAL(&cr->cr_sysfilset, &cr->cr_sysfilset_exec));
#endif
	return (false);
}

static inline void
sysfil_cred_exec_switch(struct ucred *cr)
{
#ifdef SYSFIL
	cr->cr_sysfilset = cr->cr_sysfilset_exec;
#endif
}

void sysfil_cred_rights(struct ucred *, cap_rights_t *);

#endif

#endif
