#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <paths.h>
#include <pwd.h>
#include <grp.h>
#include <nsswitch.h>
#include <netdb.h>
#include <netinet/in.h>
#include <resolv.h>

#include <curtain.h>
#include <pledge.h>

enum promise_type {
	PROMISE_ERROR,
	PROMISE_BASIC, /* same as PROMISE_STDIO but without the unveils */
	PROMISE_STDIO,
	PROMISE_UNVEIL,
	PROMISE_RPATH,
	PROMISE_WPATH,
	PROMISE_CPATH,
	PROMISE_DPATH,
	PROMISE_TMPPATH,
	PROMISE_FLOCK,
	PROMISE_FATTR,
	PROMISE_CHOWN,
	PROMISE_ID,
	PROMISE_PROC,
	PROMISE_PROC_CHILD,
	PROMISE_PROC_PGRP,
	PROMISE_PROC_SESSION,
	PROMISE_THREAD,
	PROMISE_EXEC,
	PROMISE_PROT_EXEC,
	PROMISE_TTY,
	PROMISE_TTY_PTS,
	PROMISE_RLIMIT,
	PROMISE_SCHED,
	PROMISE_SETTIME,
	PROMISE_FFCLOCK,
	PROMISE_MLOCK,
	PROMISE_AIO,
	PROMISE_EXTATTR,
	PROMISE_ACL,
	PROMISE_MAC,
	PROMISE_CPUSET,
	PROMISE_SYSVIPC,
	PROMISE_POSIXIPC,
	PROMISE_POSIXRT,
	PROMISE_CHROOT,
	PROMISE_JAIL,
	PROMISE_PS,
	PROMISE_PS_CHILD,
	PROMISE_PS_PGRP,
	PROMISE_PS_SESSION,
	PROMISE_CHMOD_SPECIAL,
	PROMISE_SYSFLAGS,
	PROMISE_SENDFILE,
	PROMISE_INET,
	PROMISE_INET_RAW,
	PROMISE_UNIX,
	PROMISE_SETFIB,
	PROMISE_ROUTE,
	PROMISE_RECVFD,
	PROMISE_SENDFD,
	PROMISE_DNS,
	PROMISE_GETPW,
	PROMISE_SSL,
	PROMISE_CRYPTODEV,
	PROMISE_MOUNT,
	PROMISE_QUOTA,
	PROMISE_FH,
	PROMISE_SOCKET_IOCTL,
	PROMISE_BPF,
	PROMISE_PFIL,
	PROMISE_ANY_AF,
	PROMISE_ANY_PRIV,
	PROMISE_ANY_IOCTL,
	PROMISE_ANY_SOCKOPT,
	PROMISE_AUDIO,
	PROMISE_COUNT /* must be last */
};

#define	PROMISE_NAME_SIZE 16
static const struct promise_name {
	const char name[PROMISE_NAME_SIZE];
} names_table[PROMISE_COUNT] = {
	[PROMISE_ERROR] =		{ "error" },
	[PROMISE_BASIC] =		{ "basic" },
	[PROMISE_STDIO] =		{ "stdio" },
	[PROMISE_UNVEIL] =		{ "unveil" },
	[PROMISE_RPATH] =		{ "rpath" },
	[PROMISE_WPATH] =		{ "wpath" },
	[PROMISE_CPATH] =		{ "cpath" },
	[PROMISE_DPATH] =		{ "dpath" },
	[PROMISE_TMPPATH] =		{ "tmppath" },
	[PROMISE_FLOCK] =		{ "flock" },
	[PROMISE_FATTR] =		{ "fattr" },
	[PROMISE_CHOWN] =		{ "chown" },
	[PROMISE_ID] =			{ "id" },
	[PROMISE_PROC] =		{ "proc" },
	[PROMISE_PROC_CHILD] =		{ "proc_child" },
	[PROMISE_PROC_PGRP] =		{ "proc_pgrp" },
	[PROMISE_PROC_SESSION] =	{ "proc_session" },
	[PROMISE_THREAD] =		{ "thread" },
	[PROMISE_EXEC] =		{ "exec" },
	[PROMISE_PROT_EXEC] =		{ "prot_exec" },
	[PROMISE_TTY] =			{ "tty" },
	[PROMISE_TTY_PTS] =		{ "tty_pts" },
	[PROMISE_RLIMIT] =		{ "rlimit" },
	[PROMISE_SCHED] =		{ "sched" },
	[PROMISE_SETTIME] =		{ "settime" },
	[PROMISE_FFCLOCK] =		{ "ffclock" },
	[PROMISE_MLOCK] =		{ "mlock" },
	[PROMISE_AIO] =			{ "aio" },
	[PROMISE_EXTATTR] =		{ "extattr" },
	[PROMISE_ACL] =			{ "acl" },
	[PROMISE_MAC] =			{ "mac" },
	[PROMISE_CPUSET] =		{ "cpuset" },
	[PROMISE_SYSVIPC] =		{ "sysvipc" },
	[PROMISE_POSIXIPC] =		{ "posixipc" },
	[PROMISE_POSIXRT] =		{ "posixrt" },
	[PROMISE_CHROOT] =		{ "chroot" },
	[PROMISE_JAIL] =		{ "jail" },
	[PROMISE_PS] =			{ "ps" },
	[PROMISE_PS_CHILD] =		{ "ps_child" },
	[PROMISE_PS_PGRP] =		{ "ps_pgrp" },
	[PROMISE_PS_SESSION] =		{ "ps_session" },
	[PROMISE_CHMOD_SPECIAL] =	{ "chmod_special" },
	[PROMISE_SYSFLAGS] =		{ "sysflags" },
	[PROMISE_SENDFILE] =		{ "sendfile" },
	[PROMISE_INET] =		{ "inet" },
	[PROMISE_INET_RAW] =		{ "inet_raw" },
	[PROMISE_UNIX] =		{ "unix" },
	[PROMISE_SETFIB] =		{ "setfib" },
	[PROMISE_ROUTE] =		{ "route" },
	[PROMISE_RECVFD] =		{ "recvfd" },
	[PROMISE_SENDFD] =		{ "sendfd" },
	[PROMISE_DNS] =			{ "dns" },
	[PROMISE_GETPW] =		{ "getpw" },
	[PROMISE_SSL] =			{ "ssl" },
	[PROMISE_CRYPTODEV] =		{ "cryptodev" },
	[PROMISE_MOUNT] =		{ "mount" },
	[PROMISE_QUOTA] =		{ "quota" },
	[PROMISE_FH] =			{ "fh" },
	[PROMISE_SOCKET_IOCTL] =	{ "socket_ioctl" },
	[PROMISE_BPF] =			{ "bpf" },
	[PROMISE_PFIL] =		{ "pfil" },
	[PROMISE_ANY_AF] =		{ "any_af" },
	[PROMISE_ANY_PRIV] =		{ "any_priv" },
	[PROMISE_ANY_IOCTL] =		{ "any_ioctl" },
	[PROMISE_ANY_SOCKOPT] =		{ "any_sockopt" },
	[PROMISE_AUDIO] =		{ "audio" },
};

static const struct promise_sysfil {
	enum promise_type type : 8;
	int sysfil : 8;
} sysfils_table[] = {
	{ PROMISE_ERROR,		SYSFIL_ERROR },
	{ PROMISE_BASIC,		SYSFIL_STDIO },
	{ PROMISE_STDIO,		SYSFIL_STDIO },
	{ PROMISE_UNVEIL,		SYSFIL_UNVEIL },
	{ PROMISE_RPATH,		SYSFIL_RPATH },
	{ PROMISE_WPATH,		SYSFIL_WPATH },
	{ PROMISE_CPATH,		SYSFIL_CPATH },
	{ PROMISE_DPATH,		SYSFIL_DPATH },
	{ PROMISE_FLOCK,		SYSFIL_FLOCK },
	{ PROMISE_FATTR,		SYSFIL_FATTR },
	{ PROMISE_CHOWN,		SYSFIL_CHOWN },
	{ PROMISE_ID,			SYSFIL_ID },
	{ PROMISE_PROC,			SYSFIL_PROC },
	{ PROMISE_PROC,			SYSFIL_SCHED },
	{ PROMISE_PROC,			SYSFIL_ANY_PROCESS },
	{ PROMISE_PROC_SESSION,		SYSFIL_PROC },
	{ PROMISE_PROC_SESSION,		SYSFIL_SCHED },
	{ PROMISE_PROC_SESSION,		SYSFIL_SAME_SESSION },
	{ PROMISE_PROC_PGRP,		SYSFIL_PROC },
	{ PROMISE_PROC_PGRP,		SYSFIL_SCHED },
	{ PROMISE_PROC_PGRP,		SYSFIL_SAME_PGRP },
	{ PROMISE_PROC_CHILD,		SYSFIL_PROC },
	{ PROMISE_PROC_CHILD,		SYSFIL_SCHED },
	{ PROMISE_PROC_CHILD,		SYSFIL_CHILD_PROCESS },
	{ PROMISE_THREAD,		SYSFIL_THREAD },
	{ PROMISE_THREAD,		SYSFIL_SCHED },
	{ PROMISE_EXEC,			SYSFIL_EXEC },
	{ PROMISE_PROT_EXEC,		SYSFIL_PROT_EXEC },
	{ PROMISE_TTY,			SYSFIL_TTY },
	{ PROMISE_TTY_PTS,		SYSFIL_TTY },
	{ PROMISE_RLIMIT,		SYSFIL_RLIMIT },
	{ PROMISE_SCHED,		SYSFIL_SCHED },
	{ PROMISE_SETTIME,		SYSFIL_SETTIME },
	{ PROMISE_FFCLOCK,		SYSFIL_FFCLOCK },
	{ PROMISE_MLOCK,		SYSFIL_MLOCK },
	{ PROMISE_AIO,			SYSFIL_AIO },
	{ PROMISE_EXTATTR,		SYSFIL_EXTATTR },
	{ PROMISE_ACL,			SYSFIL_ACL },
	{ PROMISE_MAC,			SYSFIL_MAC },
	{ PROMISE_CPUSET,		SYSFIL_CPUSET },
	{ PROMISE_SYSVIPC,		SYSFIL_SYSVIPC },
	{ PROMISE_POSIXIPC,		SYSFIL_POSIXIPC },
	{ PROMISE_POSIXRT,		SYSFIL_POSIXRT },
	{ PROMISE_CHROOT,		SYSFIL_CHROOT },
	{ PROMISE_JAIL,			SYSFIL_JAIL },
	{ PROMISE_PS,			SYSFIL_PS },
	{ PROMISE_PS,			SYSFIL_ANY_PROCESS },
	{ PROMISE_PS_SESSION,		SYSFIL_PS },
	{ PROMISE_PS_SESSION,		SYSFIL_SAME_SESSION },
	{ PROMISE_PS_PGRP,		SYSFIL_PS },
	{ PROMISE_PS_PGRP,		SYSFIL_SAME_PGRP },
	{ PROMISE_PS_CHILD,		SYSFIL_PS },
	{ PROMISE_PS_CHILD,		SYSFIL_CHILD_PROCESS },
	{ PROMISE_CHMOD_SPECIAL,	SYSFIL_CHMOD_SPECIAL },
	{ PROMISE_SYSFLAGS,		SYSFIL_SYSFLAGS },
	{ PROMISE_SENDFILE,		SYSFIL_SENDFILE },
	{ PROMISE_INET,			SYSFIL_INET },
	{ PROMISE_INET_RAW,		SYSFIL_INET_RAW },
	{ PROMISE_UNIX,			SYSFIL_UNIX },
	{ PROMISE_SETFIB,		SYSFIL_SETFIB },
	{ PROMISE_ROUTE,		SYSFIL_ROUTE },
	{ PROMISE_RECVFD,		SYSFIL_RECVFD },
	{ PROMISE_SENDFD,		SYSFIL_SENDFD },
	{ PROMISE_DNS,			SYSFIL_INET },
	{ PROMISE_DNS,			SYSFIL_ROUTE }, /* XXX */
	{ PROMISE_CRYPTODEV,		SYSFIL_CRYPTODEV },
	{ PROMISE_MOUNT,		SYSFIL_MOUNT },
	{ PROMISE_QUOTA,		SYSFIL_QUOTA },
	{ PROMISE_FH,			SYSFIL_FH },
	{ PROMISE_SOCKET_IOCTL,		SYSFIL_SOCKET_IOCTL },
	{ PROMISE_BPF,			SYSFIL_BPF },
	{ PROMISE_BPF,			SYSFIL_SOCKET_IOCTL }, /* XXX */
	{ PROMISE_PFIL,			SYSFIL_PFIL },
	{ PROMISE_PFIL,			SYSFIL_INET_RAW }, /* XXX for ipfw */
	{ PROMISE_ANY_AF,		SYSFIL_ANY_AF },
	{ PROMISE_ANY_PRIV,		SYSFIL_ANY_PRIV },
	{ PROMISE_ANY_IOCTL,		SYSFIL_ANY_IOCTL },
	{ PROMISE_ANY_SOCKOPT,		SYSFIL_ANY_SOCKOPT },
	{ PROMISE_AUDIO,		SYSFIL_AUDIO },
};

static const char *const root_path = "/";
static const char *const tmp_path = _PATH_TMP;

static const struct promise_unveil {
	const char *path;
	unveil_perms uperms : 8;
	enum promise_type type : 8;
} unveils_table[] = {
#define	N UPERM_NONE
#define	I UPERM_INSPECT
#define	R UPERM_RPATH
#define	W UPERM_WPATH /* NOTE: UPERM_APATH not implied here */
#define	C UPERM_CPATH
#define	X UPERM_XPATH
#define	A UPERM_APATH
#define	T UPERM_TMPPATH
	/*
	 * NOTE: On this implementation, open(2) with O_CREAT does not need
	 * the UPERM_CPATH unveil permission if the file already exists.
	 */
	{ _PATH_ETC "/malloc.conf", R,			PROMISE_STDIO },
	{ _PATH_LIBMAP_CONF, R,				PROMISE_STDIO },
	{ _PATH_VARRUN "/ld-elf.so.hints", R,		PROMISE_STDIO },
	{ _PATH_ETC "/localtime", R,			PROMISE_STDIO },
	{ "/usr/share/zoneinfo/", R,			PROMISE_STDIO },
	{ "/usr/share/nls/", R,				PROMISE_STDIO },
	{ _PATH_LOCALBASE "/share/nls/", R,		PROMISE_STDIO },
	{ _PATH_DEVNULL, R|W,				PROMISE_STDIO },
	{ _PATH_DEV "/random", R,			PROMISE_STDIO },
	{ _PATH_DEV "/urandom", R,			PROMISE_STDIO },
	{ "/libexec/ld-elf.so.1", X,			PROMISE_EXEC },
	{ _PATH_NS_CONF, R,				PROMISE_DNS },
	{ _PATH_RESCONF, R,				PROMISE_DNS },
	{ _PATH_HOSTS, R,				PROMISE_DNS },
	{ _PATH_SERVICES, R,				PROMISE_DNS },
	{ _PATH_SERVICES_DB, R,				PROMISE_DNS },
	{ _PATH_PROTOCOLS, R,				PROMISE_DNS },
	{ _PATH_TTY, R|W|A,				PROMISE_TTY },
	{ _PATH_DEV "/pts/", R|W|A,			PROMISE_TTY_PTS }, /* !!! */
	{ _PATH_NS_CONF, R,				PROMISE_GETPW },
	{ _PATH_MP_DB, R,				PROMISE_GETPW },
	{ _PATH_SMP_DB, R,				PROMISE_GETPW },
	{ _PATH_GROUP, R,				PROMISE_GETPW },
	{ _PATH_DEV "/crypto", R|W,			PROMISE_CRYPTODEV },
	{ _PATH_ETC "/ssl/", R,				PROMISE_SSL },
	{ _PATH_ETC "/ssl/private/", N,			PROMISE_SSL },
	{ _PATH_LOCALBASE "/etc/ssl/", R,		PROMISE_SSL },
	{ _PATH_LOCALBASE "/etc/ssl/private/", N,	PROMISE_SSL },
	{ _PATH_DEV "/bpf", R|W,			PROMISE_BPF },
	{ _PATH_DEV "/pfil", R|W,			PROMISE_PFIL },
	{ _PATH_DEV "/pf", R|W,				PROMISE_PFIL },
	{ _PATH_DEV "/sndstat", R|W,			PROMISE_AUDIO },
	{ _PATH_DEV "/mixer", R|W,			PROMISE_AUDIO },
	{ _PATH_DEV "/dsp", R|W,			PROMISE_AUDIO },
	{ tmp_path, T,					PROMISE_TMPPATH },
#undef	T
#undef	A
#undef	X
#undef	C
#undef	W
#undef	R
#undef	I
#undef	N
};


static bool has_pledges_on[CURTAIN_ON_COUNT];
static bool has_customs_on[CURTAIN_ON_COUNT];
static struct curtain_slot *always_slot;
static struct curtain_slot *root_slot_on[CURTAIN_ON_COUNT];
static struct curtain_slot *promise_sysfil_slots[PROMISE_COUNT];
static struct curtain_slot *promise_unveil_slots[PROMISE_COUNT];
static struct curtain_slot *custom_slot_on[CURTAIN_ON_COUNT];


static int
parse_promises(enum curtain_state *promises, const char *promises_str)
{
	const char *p = promises_str;
	do {
		/* skip spaces */
		while (*p == ' ')
			p++;
		if (!*p) /* whole string processed */
			break;
		/* get next promise name */
		char name[PROMISE_NAME_SIZE] = { '\0' }, *q = name;
		do {
			if (q == &name[sizeof name])
				goto inval; /* name too long */
			*q++ = *p++;
		} while (*p && *p != ' ');
		/* search for name in table */
		enum promise_type type = 0;
		do {
			if (type >= PROMISE_COUNT)
				goto inval; /* not found */
			if (memcmp(name, names_table[type].name, sizeof name) == 0)
				break;
			type++;
		} while (true);
		promises[type] = CURTAIN_ENABLED; /* found */
	} while (true);
	return (0);
inval:	errno = EINVAL;
	return (-1);
}


static unveil_perms
uperms_for_promises(const enum curtain_state *promises)
{
	unveil_perms uperms = UPERM_NONE;
	if (promises[PROMISE_RPATH] >= CURTAIN_ENABLED) uperms |= UPERM_RPATH;
	if (promises[PROMISE_WPATH] >= CURTAIN_ENABLED) uperms |= UPERM_WPATH;
	if (promises[PROMISE_CPATH] >= CURTAIN_ENABLED) uperms |= UPERM_CPATH;
	if (promises[PROMISE_EXEC]  >= CURTAIN_ENABLED) uperms |= UPERM_XPATH;
	if (promises[PROMISE_FATTR] >= CURTAIN_ENABLED) uperms |= UPERM_APATH;
	return (uperms);
}

static void
sysfils_for_uperms(struct curtain_slot *slot, unveil_perms uperms)
{
	if (uperms & UPERM_RPATH) curtain_sysfil(slot, SYSFIL_RPATH);
	if (uperms & UPERM_WPATH) curtain_sysfil(slot, SYSFIL_WPATH);
	if (uperms & UPERM_CPATH) curtain_sysfil(slot, SYSFIL_CPATH);
	if (uperms & UPERM_XPATH) curtain_sysfil(slot, SYSFIL_EXEC);
	/* Note that UPERM_APATH does not imply SYSFIL_FATTR. */
	if (uperms & UPERM_TMPPATH) {
		/* TODO: could probably reduce this  */
		curtain_sysfil(slot, SYSFIL_RPATH);
		curtain_sysfil(slot, SYSFIL_WPATH);
		curtain_sysfil(slot, SYSFIL_CPATH);
	}
}


static void
do_promises_slots(enum curtain_on on,
    const enum curtain_state sysfil_promises[],
    const enum curtain_state unveil_promises[])
{
	bool fill_sysfils[PROMISE_COUNT], fill_unveils[PROMISE_COUNT];
	const struct promise_unveil *pu;
	const struct promise_sysfil *ps;
	bool tainted;

	/*
	 * Initialize promise slots on first use.  Sysfil and unveils are
	 * separated because unveil() needs to deal with them differently when
	 * unveil() is done before pledge().
	 */

	for (enum promise_type promise = 0; promise < PROMISE_COUNT; promise++) {
		enum curtain_state state;
		if ((state = sysfil_promises[promise]) >= CURTAIN_RESERVED) {
			if ((fill_sysfils[promise] = !promise_sysfil_slots[promise]))
				promise_sysfil_slots[promise] = curtain_slot_neutral();
		} else
			fill_sysfils[promise] = false;
		if (promise_sysfil_slots[promise])
			curtain_state(promise_sysfil_slots[promise], on, state);

		if ((state = unveil_promises[promise]) >= CURTAIN_RESERVED) {
			if ((fill_unveils[promise] = !promise_unveil_slots[promise]))
				promise_unveil_slots[promise] = curtain_slot_neutral();
		} else
			fill_unveils[promise] = false;
		if (promise_unveil_slots[promise])
			curtain_state(promise_unveil_slots[promise], on, state);
	}

	tainted = issetugid() != 0;
	for (pu = unveils_table; pu != &unveils_table[nitems(unveils_table)]; pu++) {
		if (fill_unveils[pu->type]) {
			const char *path = pu->path;
			if (!tainted && path == tmp_path) {
				char *tmpdir;
				if ((tmpdir = getenv("TMPDIR")))
					path = tmpdir;
			}
			curtain_unveil(promise_unveil_slots[pu->type], path,
			    CURTAIN_UNVEIL_INHERIT, pu->uperms);
		}
		if (fill_sysfils[pu->type])
			sysfils_for_uperms(promise_sysfil_slots[pu->type], pu->uperms);
	}

	for (ps = sysfils_table; ps != &sysfils_table[nitems(sysfils_table)]; ps++)
		if (fill_sysfils[ps->type])
			curtain_sysfil(promise_sysfil_slots[ps->type], ps->sysfil);

	if (!always_slot) {
		always_slot = curtain_slot_neutral();
		/*
		 * Always allow to reduce unveil permissions later on.
		 * This is different from the "unveil" promise which is handled
		 * specially in do_pledge().
		 */
		curtain_sysfil(always_slot, SYSFIL_UNVEIL);
		/*
		 * Always keep the root directory inspectable.  This is so that
		 * child processes can do their own unveil inheritance on it.
		 * This is currently the only way to propagate permissions to
		 * all reachable unveils.
		 *
		 * XXX This leaks some stat() information on the root directory.
		 */
		curtain_unveil(always_slot, root_path, 0, UPERM_INSPECT);
	}
	curtain_enable(always_slot, on);
}


static void unveil_enable_delayed(enum curtain_on);

static int
do_pledge(enum curtain_state *promises_on[CURTAIN_ON_COUNT])
{
	for (enum curtain_on on = 0; on < CURTAIN_ON_COUNT; on++) {
		unveil_perms wanted_uperms;
		if (!promises_on[on])
			continue;
		has_pledges_on[on] = true;
		wanted_uperms = uperms_for_promises(promises_on[on]);
		do_promises_slots(on, promises_on[on], promises_on[on]);
		if (custom_slot_on[on]) {
			curtain_unveils_limit(custom_slot_on[on], wanted_uperms);
			unveil_enable_delayed(on); /* see do_unveil_both() */
		}
		if (!has_customs_on[on] ||
		    promises_on[on][PROMISE_UNVEIL] >= CURTAIN_RESERVED) {
			if (!root_slot_on[on])
				root_slot_on[on] = curtain_slot_neutral();
			curtain_state(root_slot_on[on], on,
			    has_customs_on[on] ? CURTAIN_RESERVED : CURTAIN_ENABLED);
		}
		if (root_slot_on[on])
			/* XXX problem when / isn't inspectable */
			curtain_unveil(root_slot_on[on], root_path, 0, wanted_uperms);
	}
	return (curtain_enforce());
}

int
pledge(const char *promises_str, const char *execpromises_str)
{
	enum curtain_state self_promises[PROMISE_COUNT] = { 0 };
	enum curtain_state exec_promises[PROMISE_COUNT] = { 0 };
	enum curtain_state *promises_on[CURTAIN_ON_COUNT] = { 0 };
	int r;
	if (promises_str) {
		r = parse_promises(self_promises, promises_str);
		if (r < 0)
			return (-1);
		promises_on[CURTAIN_ON_SELF] = self_promises;
	}
	if (execpromises_str) {
		r = parse_promises(exec_promises, execpromises_str);
		if (r < 0)
			return (-1);
		promises_on[CURTAIN_ON_EXEC] = exec_promises;
	}
	return (do_pledge(promises_on));
}


/*
 * Most of the complexity here is to deal with the case where unveil() is
 * called before pledge().  On OpenBSD, pledges and unveils can be set up
 * independently.  Not so in this implementation.
 */

static void
unveil_enable_delayed(enum curtain_on on)
{
	if (custom_slot_on[on]) {
		curtain_enable(custom_slot_on[on], on);
		has_customs_on[on] = true;
	}
}

static void
do_unveil_init_on(enum curtain_on on)
{
	if (!custom_slot_on[on])
		custom_slot_on[on] = curtain_slot_neutral();
	curtain_enable(custom_slot_on[on], on);
	if (!has_pledges_on[on] && !has_customs_on[on]) {
		enum curtain_state sysfil_promises[PROMISE_COUNT],
		                   unveil_promises[PROMISE_COUNT];
		/*
		 * unveil() was called before pledge().  Enable sysfils for all
		 * promises and reserve their unveils.
		 */
		for (enum promise_type i = 0; i < PROMISE_COUNT; i++) {
			sysfil_promises[i] = CURTAIN_ENABLED;
			unveil_promises[i] = CURTAIN_RESERVED;
		}
		do_promises_slots(on, sysfil_promises, unveil_promises);
	}
	if (root_slot_on[on])
		curtain_disable(root_slot_on[on], on);
	has_customs_on[on] = true;
}

static int
do_unveil_on(enum curtain_on on, const char *path, unveil_perms uperms)
{
	do_unveil_init_on(on);
	if (path) {
		int r;
		r = curtain_unveil(custom_slot_on[on], path, 0, uperms);
		if (r < 0)
			return (r);
		return (curtain_apply());
	} else /* unveil(NULL, NULL) */
		return (curtain_enforce());
}

static int
do_unveil_both(const char *path, unveil_perms uperms)
{
	/*
	 * On OpenBSD, unveils are discarded on exec if the process does not
	 * have on-exec pledges (which then allows it to run setuid binaries
	 * or do its own unveiling, for example).
	 *
	 * To implement this, delay enabling the on-exec slot for unveils added
	 * with unveil() until an on-exec pledge() or an unveilexec() is
	 * explicitly done.  If the current process already had inherited
	 * on-exec unveils, they will be left unmodified and an exec will
	 * revert the process' unveils to those initially inherited unveils.
	 * Otherwise, the process will be unrestricted on exec as on OpenBSD.
	 */
	bool ignore_on[CURTAIN_ON_COUNT] = { 0 };
	ignore_on[CURTAIN_ON_EXEC] = !has_pledges_on[CURTAIN_ON_EXEC] &&
	                             !has_customs_on[CURTAIN_ON_EXEC];
	for (enum curtain_on on = 0; on < CURTAIN_ON_COUNT; on++)
		if (ignore_on[on]) {
			if (!custom_slot_on[on])
				custom_slot_on[on] = curtain_slot_neutral();
		} else
			do_unveil_init_on(on);
	if (path) {
		int r = 0;
		/* XXX would be better to do a single unveilreg() for this */
		for (enum curtain_on on = 0; on < CURTAIN_ON_COUNT; on++)
			if (!ignore_on[on])
				r = curtain_unveil(custom_slot_on[on], path, 0, uperms);
		if (r < 0)
			return (r);
		return (curtain_apply());
	} else /* unveil(NULL, NULL) */
		return (curtain_enforce());
}

int
unveil_parse_perms(unveil_perms *uperms, const char *s)
{
	*uperms = UPERM_NONE;
	while (*s)
		switch (*s++) {
		case 'r': *uperms |= UPERM_RPATH; break;
		case 'm': *uperms |= UPERM_WPATH; break;
		case 'w': *uperms |= UPERM_WPATH; /* FALLTHROUGH */
		case 'a': *uperms |= UPERM_APATH; break;
		case 'c': *uperms |= UPERM_CPATH; break;
		case 'x': *uperms |= UPERM_XPATH; break;
		case 'i': *uperms |= UPERM_INSPECT; break;
		case 't': *uperms |= UPERM_TMPPATH; break;
		default:
			return (-1);
		}
	return (0);
}

static int
unveil_on(enum curtain_on on, const char *path, const char *perms)
{
	unveil_perms uperms;
	int r;
	if ((perms == NULL) != (path == NULL))
		return ((errno = EINVAL), -1);
	if (perms) {
		r = unveil_parse_perms(&uperms, perms);
		if (r < 0)
			return ((errno = EINVAL), -1);
	} else
		uperms = UPERM_NONE;
	return (do_unveil_on(on, path, uperms));
}

int
unveilself(const char *path, const char *perms)
{ return (unveil_on(CURTAIN_ON_SELF, path, perms)); }

int
unveilexec(const char *path, const char *perms)
{ return (unveil_on(CURTAIN_ON_EXEC, path, perms)); }

int
unveil(const char *path, const char *perms)
{
	unveil_perms uperms;
	int r;
	if ((perms == NULL) != (path == NULL))
		return ((errno = EINVAL), -1);
	if (perms) {
		r = unveil_parse_perms(&uperms, perms);
		if (r < 0)
			return ((errno = EINVAL), -1);
	} else
		uperms = UPERM_NONE;
	return (do_unveil_both(path, uperms));
}

