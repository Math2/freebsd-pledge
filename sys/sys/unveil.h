#ifndef	_SYS_UNVEIL_H_
#define	_SYS_UNVEIL_H_

#include <sys/types.h>
#include <sys/malloc.h>
#include <sys/vnode.h>
#include <sys/queue.h>
#include <sys/tree.h>
#include <sys/_unveil.h>
#ifdef _KERNEL
#include <sys/systm.h>
#include <sys/limits.h>
#include <sys/capsicum.h>
#include <sys/proc.h>
#endif

enum {
	UPERM_NONE = 0,
	UPERM_INSPECT = 1 << 0,
	UPERM_RPATH = 1 << 1,
	UPERM_WPATH = 1 << 2,
	UPERM_CPATH = 1 << 3,
	UPERM_XPATH = 1 << 4,
	UPERM_APATH = 1 << 5,
	UPERM_TMPPATH = 1 << 6,
	UPERM_ALL = -1,
};

#define	UNVEILCTL_UNVEIL	(1 << 0)
#define	UNVEILCTL_FREEZE	(1 << 2)
#define	UNVEILCTL_LIMIT		(1 << 3)
#define	UNVEILCTL_SWEEP		(1 << 4)
#define	UNVEILCTL_ENABLE	(1 << 5)
#define	UNVEILCTL_DISABLE	(1 << 6)
#define	UNVEILCTL_NOINHERIT	(1 << 8)
#define	UNVEILCTL_INTERMEDIATE	(1 << 9)
#define	UNVEILCTL_INSPECTABLE	(1 << 10)
#define	UNVEILCTL_NONDIRBYNAME	(1 << 11)
#define	UNVEILCTL_ON_SELF	(1 << 16)
#define	UNVEILCTL_ON_EXEC	(1 << 17)
#define	UNVEILCTL_ON_BOTH	(UNVEILCTL_ON_SELF | UNVEILCTL_ON_EXEC)

#define	UNVEIL_SUPPORTED_SLOTS	8

struct unveilctl {
	int reserved;
	int atfd;
	const char *path;
	int atflags;
	unsigned slots;
	unsigned uperms;
};

int unveilctl(int flags, struct unveilctl *);

#ifdef _KERNEL

#ifdef UNVEIL
MALLOC_DECLARE(M_UNVEIL);
#endif

enum unveil_on {
	UNVEIL_ON_SELF,
	UNVEIL_ON_EXEC,
};

static inline bool
unveil_is_active(struct thread *td)
{
#ifdef UNVEIL
	return (td->td_proc->p_unveils.on[UNVEIL_ON_SELF].active);
#else
	return (false);
#endif
}

static inline bool
unveil_exec_is_active(struct thread *td)
{
#ifdef UNVEIL
	return (td->td_proc->p_unveils.on[UNVEIL_ON_EXEC].active);
#else
	return (false);
#endif
}


void unveil_proc_exec_switch(struct thread *);

void unveil_base_init(struct unveil_base *);
void unveil_base_copy(struct unveil_base *dst, struct unveil_base *src);
void unveil_base_clear(struct unveil_base *);
void unveil_base_reset(struct unveil_base *);
void unveil_base_free(struct unveil_base *);

int unveil_traverse_begin(struct thread *, struct unveil_traversal *,
    struct vnode *);
int unveil_traverse(struct thread *, struct unveil_traversal *,
    struct vnode *dvp, const char *name, size_t name_len, struct vnode *vp,
    bool final);
void unveil_traverse_dotdot(struct thread *, struct unveil_traversal *,
    struct vnode *);
unveil_perms unveil_traverse_effective_uperms(struct thread *, struct unveil_traversal *);
void unveil_traverse_effective_rights(struct thread *, struct unveil_traversal *,
    cap_rights_t *, int *suggested_error);
void unveil_traverse_end(struct thread *, struct unveil_traversal *);

void unveil_uperms_rights(unveil_perms, cap_rights_t *);

#endif /* _KERNEL */

#endif
