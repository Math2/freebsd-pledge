#ifndef	_SYS_PLEDGE_H_
#define	_SYS_PLEDGE_H_

/*
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 *
 * XXX pledge(2) support is Work In Progress.  Currently incomplete and
 * possibly insecure. XXX
 *
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 *
 * Miscellaneous Development Notes:
 *
 * The following syscalls are enabled under pledge("stdio") (PLEDGE_ALWAYS +
 * PLEDGE_STDIO) but not Capsicum.  They should have some extra verification
 * (on top of the general VFS access checks).
 *
 *   open(2), wait4(2), fchdir(2), access(2), readlink(2), adjtime(2),
 *   eaccess(2), posix_fadvise(2)
 *
 * Both Capsicum and pledge("stdio") allow ioctl(2), but Capsicum applications
 * probably tend to be more careful about not carrying too many potentially
 * insecure FDs after entering capability mode (and hopefully they'll restrict
 * the rights(4) on those that they keep).  ioctl(2) will need good filtering
 * to be safe for pledged applications.  This could also be useful as an extra
 * filtering layer for Capsicum applications.
 *
 * pledge("stdio") allows a few filesystem access syscalls to support a
 * whitelist of paths that can be accessed (even without pledge("rpath")).
 * This works differently than unveil(2) and it only works if absolute paths
 * are passed to syscalls (it does do some canonicalization though).  This will
 * have to be done very carefully.
 *
 * As it is, it is also the case that Capsicum allows some syscalls that
 * pledge("stdio") does not.  It should be safe to enable most (if not all) of
 * them under pledge("stdio"), but this defeats one goal of pledge() which is
 * to limit the kernel's exposed attack surface.  For now, let's at least allow
 * a minimal set of less risky syscalls that most Capsicum applications
 * absolutely need.
 *
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/pledge_set.h>
#include <sys/ucred.h>
#include <sys/proc.h>

/*
 * If pledge support isn't compiled in, pledge promises aren't actually stored
 * in the credentials structures.  But pledge_check() is still available and
 * behaves as if they had been all enabled.
 */

static inline void
pledge_cred_init(struct ucred *cr) {
	cr->cr_fflags = cr->cr_execfflags = -1; /* allow all syscalls */
#ifdef PLEDGE
	pledge_set_init(&cr->cr_pledge);
	pledge_set_init(&cr->cr_execpledge);
#endif
}

static inline int
pledge_cred_needs_exec_tweak(struct ucred *cr) {
	if (cr->cr_fflags != cr->cr_execfflags)
		return (1);
#ifdef PLEDGE
	if (cr->cr_pledge.pflags != cr->cr_execpledge.pflags)
		return (1);
#endif
	return (0);
}

static inline void
pledge_cred_exec_tweak(struct ucred *cr) {
	cr->cr_fflags = cr->cr_execfflags;
#ifdef PLEDGE
	cr->cr_pledge.pflags = cr->cr_execpledge.pflags;
#endif
}

static inline int
pledge_check(struct thread *td, enum pledge_promise pr) {
	/* XXX: OpenBSD generally returns EPERM for this, and ECAPMODE's error
	 * string is "Not permitted in capability mode", which is confusing
	 * because "capability mode" is a Capsicum term.  syscallret()
	 * currently relies on these error codes being used to detect pledge
	 * violations and send a signal if needed. */
#ifdef PLEDGE
	return (pledge_set_test(&td->td_ucred->cr_pledge, pr) ? 0 : ECAPMODE);
#else
	return (0); /* no restrictions */
#endif
}

#define	CRED_PLEDGED(cr, pr)		(pledge_set_test(&(cr)->cr_pledge, (pr)))

#endif
