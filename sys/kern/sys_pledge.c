#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_capsicum.h"

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/syscallsubr.h>
#include <sys/sysproto.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/ucred.h>
#include <sys/uio.h>
#include <sys/ktrace.h>
#include <sys/systm.h>
#include <sys/syslog.h>
#include <sys/sysfil.h>
#include <sys/jail.h>
#include <sys/pledge.h>
#include <sys/filio.h>

#ifdef PLEDGE

static const struct promise_name {
	char name[12];
	enum pledge_promise promise;
} promise_names[] = {
	{ "capsicum",	PLEDGE_CAPSICUM },
	{ "error",	PLEDGE_ERROR },
	{ "stdio",	PLEDGE_STDIO },
	{ "unveil",	PLEDGE_UNVEIL },
	{ "rpath",	PLEDGE_RPATH },
	{ "wpath",	PLEDGE_WPATH },
	{ "cpath",	PLEDGE_CPATH },
	{ "dpath",	PLEDGE_DPATH },
	{ "tmppath",	PLEDGE_TMPPATH },
	{ "flock",	PLEDGE_FLOCK },
	{ "fattr",	PLEDGE_FATTR },
	{ "chown",	PLEDGE_CHOWN },
	{ "proc",	PLEDGE_PROC },
	{ "thread",	PLEDGE_THREAD },
	{ "exec",	PLEDGE_EXEC },
	{ "id",		PLEDGE_ID },
	{ "tty",	PLEDGE_TTY },
	{ "settime",	PLEDGE_SETTIME },
	{ "inet",	PLEDGE_INET },
	{ "unix",	PLEDGE_UNIX },
	{ "dns",	PLEDGE_DNS },
	{ "",		PLEDGE_NULL },
};

/* Map promises to syscall filter flags.  Syscall filters were added as a more
 * general mechanism that could be used by other things.  Thus they use
 * different bit values than pledge promises.  There may also be many more
 * pledges than there are syscall filters. */

static sysfil_t pledge2fflags[PLEDGE_COUNT] = {
	[PLEDGE_CAPSICUM] = SYF_CAPENABLED,
	[PLEDGE_STDIO] = SYF_PLEDGE_STDIO,
	[PLEDGE_UNVEIL] = SYF_PLEDGE_UNVEIL,
	[PLEDGE_RPATH] = SYF_PLEDGE_RPATH,
	[PLEDGE_WPATH] = SYF_PLEDGE_WPATH,
	[PLEDGE_CPATH] = SYF_PLEDGE_CPATH,
	[PLEDGE_DPATH] = SYF_PLEDGE_DPATH,
	[PLEDGE_TMPPATH] = SYF_PLEDGE_TMPPATH,
	[PLEDGE_FLOCK] = SYF_PLEDGE_FLOCK,
	[PLEDGE_FATTR] = SYF_PLEDGE_FATTR,
	[PLEDGE_CHOWN] = SYF_PLEDGE_CHOWN,
	[PLEDGE_PROC] = SYF_PLEDGE_PROC,
	[PLEDGE_THREAD] = SYF_PLEDGE_THREAD,
	[PLEDGE_EXEC] = SYF_PLEDGE_EXEC,
	[PLEDGE_ID] = SYF_PLEDGE_ID,
	[PLEDGE_TTY] = SYF_PLEDGE_TTY,
	[PLEDGE_SETTIME] = SYF_PLEDGE_SETTIME,
	[PLEDGE_INET] = SYF_PLEDGE_INET,
	[PLEDGE_UNIX] = SYF_PLEDGE_UNIX,
	[PLEDGE_DNS] = SYF_PLEDGE_DNS,
};

static int
parse_promises(char *promises, sysfil_t *fflags, pledge_flags_t *pflags)
{
	/* NOTE: destroys the passed string */
	char *promise;
	while ((promise = strsep(&promises, " ")))
		if (*promise) {
			const struct promise_name *pn;
			for (pn = promise_names; *pn->name; pn++)
				if (0 == strcmp(pn->name, promise))
					break;
			if (pn->promise == PLEDGE_NULL)
				return EINVAL;
			*pflags |= (pledge_flags_t)1 << pn->promise;
			*fflags |= pledge2fflags[pn->promise];
		}
	return (0);
}

static int
apply_promises(struct ucred *cred, char *promises, char *execpromises)
{
	sysfil_t wanted_fflags, wanted_execfflags;
	pledge_flags_t wanted_pflags, wanted_execpflags;
	int error;
	wanted_fflags = wanted_execfflags = SYF_PLEDGE_ALWAYS;
	wanted_pflags = wanted_execpflags = 0;
	if (promises) {
		error = parse_promises(promises,
		    &wanted_fflags, &wanted_pflags);
		if (error)
			return error;
	}
	if (execpromises) {
		error = parse_promises(execpromises,
		    &wanted_execfflags, &wanted_execpflags);
		if (error)
			return error;
	}
	if (cred->cr_pledge.pflags & ((pledge_flags_t)1 << PLEDGE_ERROR)) {
		/* Silently ignore attempts to add promises.  Only if the
		 * PLEDGE_ERROR promise is already in effect, not if it's just
		 * being asked for. */
		wanted_pflags &= cred->cr_pledge.pflags;
		wanted_fflags &= cred->cr_fflags;
		wanted_execpflags &= cred->cr_execpledge.pflags;
		wanted_execfflags &= cred->cr_execfflags;
	}
	if (promises) {
		if ((wanted_pflags & ~cred->cr_pledge.pflags) != 0 ||
		    (wanted_fflags & ~cred->cr_fflags) != 0)
			return EPERM; /* asked to elevate permissions */
		cred->cr_fflags &= wanted_fflags;
		cred->cr_pledge.pflags &= wanted_pflags;
		cred->cr_flags |= CRED_FLAG_SANDBOX;
		KASSERT(CRED_IN_SANDBOX_MODE(cred),
		    ("CRED_IN_SANDBOX_MODE() inconsistent"));
	}
	if (execpromises) {
		if ((wanted_execpflags & ~cred->cr_execpledge.pflags) != 0 ||
		    (wanted_execfflags & ~cred->cr_execfflags) != 0)
			return EPERM; /* also cannot be elevated */
		cred->cr_execfflags &= wanted_execfflags;
		cred->cr_execpledge.pflags &= wanted_execpflags;
	}
	return (0);
}

static int
do_pledge(struct thread *td, char *promises, char *execpromises)
{
	/* NOTE: destroys the passed promise strings */
	struct ucred *newcred, *oldcred;
	struct proc *p;
	int error;

	newcred = crget();
	p = td->td_proc;
	PROC_LOCK(p);

	oldcred = crcopysafe(p, newcred);
	error = apply_promises(newcred, promises, execpromises);
	proc_set_cred(p, newcred);

	if (error == 0) /* XXX */
		log(LOG_INFO,
		    "pid %d (%s), jid %d, uid %d: pledged from %#jx[%#x] to %#jx[%#x]\n",
		    td->td_proc->p_pid, td->td_proc->p_comm,
		    td->td_ucred->cr_prison->pr_id, td->td_ucred->cr_uid,
		    (uintmax_t)oldcred->cr_pledge.pflags, oldcred->cr_fflags,
		    (uintmax_t)newcred->cr_pledge.pflags, newcred->cr_fflags);

	PROC_UNLOCK(p);
	crfree(oldcred);

	return error;
}

int
sys_pledge(struct thread *td, struct pledge_args *uap)
{
	size_t size = MAXPATHLEN;
	char *promises = NULL, *execpromises = NULL;
	int error;
	if (uap->promises) {
		promises = malloc(size, M_TEMP, M_WAITOK);
		error = copyinstr(uap->promises, promises, size, NULL);
		if (error)
			goto error;
	}
	if (uap->execpromises) {
		execpromises = malloc(size, M_TEMP, M_WAITOK);
		error = copyinstr(uap->execpromises, execpromises, size, NULL);
		if (error)
			goto error;
	}
	error = do_pledge(td, promises, execpromises);
	/* NOTE: The new pledges and syscall filters are not immediately
	 * effective for the process' threads because the thread credential
	 * pointers have a copy-on-write optimization.  They are updated on the
	 * next syscall or trap. */
error:
	if (promises)
		free(promises, M_TEMP);
	if (execpromises)
		free(execpromises, M_TEMP);
	return (error);
}

#else /* !PLEDGE */

int
sys_pledge(struct thread *td, struct pledge_args *uap)
{
	return (ENOSYS);
}

#endif /* PLEDGE */

#ifdef PLEDGE

__read_mostly cap_rights_t cap_rpath;
__read_mostly cap_rights_t cap_wpath;
__read_mostly cap_rights_t cap_cpath;
__read_mostly cap_rights_t cap_dpath;
__read_mostly cap_rights_t cap_exec;

struct pwl_entry {
	const char *path;
	enum pledge_promise promise;
};

static const struct pwl_entry pwl_rpath[] = {
	{ "/etc/malloc.conf", PLEDGE_STDIO },
	{ "/etc/localtime", PLEDGE_STDIO },
	{ "/usr/share/zoneinfo/", PLEDGE_STDIO }, /* subhierarchy match */
	{ "/usr/share/nls/", PLEDGE_STDIO },
	{ "/usr/local/share/nls/", PLEDGE_STDIO },
	/* XXX: Some programs try to open /dev/null with O_CREAT.  This
	 * triggers a PLEDGE_CPATH check and fails.  /dev/null shouldn't be
	 * added to the cpath whitelist because pledge() can be used to
	 * restrict root processes and that would allow them to replace
	 * /dev/null with something else. */
	{ "/dev/null", PLEDGE_STDIO },
	{ "/etc/nsswitch.conf", PLEDGE_DNS },
	{ "/etc/resolv.conf", PLEDGE_DNS },
	{ "/etc/hosts", PLEDGE_DNS },
	{ "/etc/services", PLEDGE_DNS },
	{ "/var/db/services.db", PLEDGE_DNS },
	{ "/etc/protocols", PLEDGE_DNS },
	{ "/dev/tty", PLEDGE_TTY },
	{ "/etc/nsswitch.conf", PLEDGE_GETPW }, /* repeating path is OK */
	{ "/etc/pwd.db", PLEDGE_GETPW },
	{ "/etc/group", PLEDGE_GETPW },
	/* TODO: Ideally we wouldn't allow to read the directory itself (so
	 * that a pledged process can't find the names of the temporary files
	 * of other processes). */
	{ "/tmp/", PLEDGE_TMPPATH },
	{ NULL, 0 }
};

static const struct pwl_entry pwl_wpath[] = {
	{ "/dev/null", PLEDGE_STDIO },
	{ "/dev/tty", PLEDGE_TTY },
	/* XXX: Review for safety. */
	{ "/dev/crypto", PLEDGE_STDIO },
	{ "/tmp/", PLEDGE_TMPPATH },
	{ NULL, 0 }
};

static const struct pwl_entry pwl_cpath[] = {
	{ "/tmp/", PLEDGE_TMPPATH },
	{ NULL, 0 }
};

static const struct pwl_entry pwl_error[] = {
	/* paths that shouldn't send a violation signal when access fails */
	{ "/etc/spwd.db", PLEDGE_GETPW },
	{ NULL, 0 }
};

static bool
match_path(const char *p, const char *q)
{
	bool s = false;
	/* check if beginning of path matches the entry exactly */
	for (; *p && *p == *q; s = *p++ == '/', q++);
	/* only allow prefix matches if the entry's path ends with '/' */
	if (*p || (!s && *q))
		return (false);
	/* check rest of queried path for ".." components */
	for (; *q; s = *q++ == '/')
		if (s && q[0] == '.' && q[1] == '.' &&
		    (q[2] == '/' || q[2] == '\0'))
			return (false);
	return (true);
}

static bool
search_pwl(struct thread *td, const struct pwl_entry *list, const char *path)
{
	const struct pwl_entry *ent;
	for (ent = list; ent->path; ent++)
		if (match_path(ent->path, path) &&
		    pledge_probe(td, ent->promise) == 0)
			return (true);
	return (false);
}

static int
pledge_check_pwl(struct thread *td, enum pledge_promise pr,
    const struct pwl_entry *list, const char *path)
{
	int error;
	error = pledge_probe(td, pr); /* don't send signals yet */
	if (error == 0)
		return (0);
	if (search_pwl(td, list, path))
		return (0);
	if (search_pwl(td, pwl_error, path))
		return (error); /* fail without signal */
	return (pledge_check(td, pr)); /* possibly send signal */
}

#endif

int
pledge_check_path_rights(struct thread *td, const cap_rights_t *rights,
    int modifying, const char *path)
{
#ifdef PLEDGE
	/* TODO: Bypass everything if not pledged.
	 * Better done in the caller? */
	/* XXX: Core dumps get rejected. */
	int error, match;
	match = 0;
	if (cap_rights_overlaps(rights, &cap_dpath)) {
		match++;
		error = pledge_check(td, PLEDGE_DPATH);
		if (error)
			return (error);
	}
	/* The modifying parameter means that the caller has other indications
	 * that the operation will try to modify the filesystem.  In namei()'s
	 * case, it means an operation other than LOOKUP. */
	if (cap_rights_overlaps(rights, &cap_cpath) || modifying) {
		match++;
		error = pledge_check_pwl(td, PLEDGE_CPATH, pwl_cpath, path);
		if (error)
			return (error);
	}
	if (cap_rights_overlaps(rights, &cap_wpath)) {
		match++;
		error = pledge_check_pwl(td, PLEDGE_WPATH, pwl_wpath, path);
		if (error)
			return (error);
	}
	if (cap_rights_overlaps(rights, &cap_rpath)) {
		match++;
		error = pledge_check_pwl(td, PLEDGE_RPATH, pwl_rpath, path);
		if (error)
			return (error);
	}
	if (cap_rights_overlaps(rights, &cap_exec)) {
		match++;
		error = pledge_check(td, PLEDGE_EXEC);
		if (error)
			return (error);
	}
	if (!match) {
		/*
		 * Got an operation on a path not specifying any rights that we
		 * recognize.  If there are no current pledge promises for path
		 * operations, reject it.  Not considering PLEDGE_EXEC because
		 * CAP_EXECAT is always being set in do_execve(), and thus if
		 * the request was for an execve(2) it would have matched the
		 * above checks.
		 */
		if (pledge_probe(td, PLEDGE_RPATH) != 0 &&
		    pledge_probe(td, PLEDGE_WPATH) != 0 &&
		    pledge_probe(td, PLEDGE_CPATH) != 0 &&
		    pledge_probe(td, PLEDGE_DPATH) != 0)
			return pledge_check(td, PLEDGE_RPATH);
	}
#endif
	return (0);
}

int
pledge_check_ioctl(struct thread *td, enum pledge_promise pr, u_long cmd)
{
	switch (cmd) {
#ifdef PLEDGE
	case FIOCLEX:
	case FIONCLEX:
	case FIONREAD:
	case FIONBIO:
	case FIOASYNC:
	case FIOGETOWN:
	case FIODTYPE:
#if 0
	case FIOGETLBA:
#endif
		return (0);
	case FIOSETOWN:
		/* also checked in setown() */
		return (pledge_check(td, PLEDGE_PROC));
#endif
	default:
		return (pledge_check(td, pr));
	}
}

static void
pledge_sysinit(void *dummy)
{
#ifdef PLEDGE
	/* XXX need to test more rights */
	cap_rights_init(&cap_rpath,
	    CAP_READ, CAP_PREAD, CAP_FSTAT);
	cap_rights_init(&cap_wpath,
	    CAP_WRITE, CAP_PWRITE);
	cap_rights_init(&cap_cpath,
	    CAP_CREATE, CAP_UNLINKAT);
	cap_rights_init(&cap_dpath,
	    CAP_MKFIFOAT, CAP_MKNODAT);
	cap_rights_init(&cap_exec,
	    CAP_EXECAT);
	cap_rights_clear(&cap_rpath, CAP_LOOKUP, CAP_SEEK);
	cap_rights_clear(&cap_wpath, CAP_LOOKUP, CAP_SEEK);
	cap_rights_clear(&cap_cpath, CAP_LOOKUP, CAP_SEEK);
	cap_rights_clear(&cap_dpath, CAP_LOOKUP, CAP_SEEK);
	cap_rights_clear(&cap_exec, CAP_LOOKUP, CAP_SEEK);
#endif
}

SYSINIT(pledge, SI_SUB_COPYRIGHT, SI_ORDER_ANY, pledge_sysinit, NULL);
