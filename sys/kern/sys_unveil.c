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
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/sysproto.h>
#include <sys/systm.h>
#include <sys/ucred.h>
#include <sys/syslog.h>
#include <sys/jail.h>
#include <sys/namei.h>
#include <sys/unveil.h>

#ifdef UNVEIL

MALLOC_DEFINE(M_UNVEIL, "unveil", "unveil");

static bool unveil_enabled = true;
SYSCTL_BOOL(_kern, OID_AUTO, unveil_enabled, CTLFLAG_RW,
	&unveil_enabled, 0, "Allow unveil usage");

static unsigned int unveil_max_nodes_per_process = 100;
SYSCTL_UINT(_kern, OID_AUTO, maxunveilsperproc, CTLFLAG_RW,
	&unveil_max_nodes_per_process, 0, "Maximum unveils allowed per process");


unveil_perms_t
unveil_node_soft_perms(struct unveil_node *node, enum unveil_role role)
{
	struct unveil_node *node1;
	unveil_perms_t inherited_perms[UNVEIL_SLOT_COUNT], soft_perms, mask;
	bool all_final;
	int i;
	for (i = 0; i < UNVEIL_SLOT_COUNT; i++)
		inherited_perms[i] = UNVEIL_PERM_NONE;
	/*
	 * Go up the node chain until all wanted permissions have been found
	 * without any more inheritance required.
	 */
	node1 = node;
	mask = UNVEIL_PERM_FULL_MASK;
	do {
		all_final = true;
		for (i = 0; i < UNVEIL_SLOT_COUNT; i++) {
			if (!(inherited_perms[i] & UNVEIL_PERM_FINAL))
				inherited_perms[i] |= node1->wanted_perms[role][i] & mask;
			if (!(inherited_perms[i] & UNVEIL_PERM_FINAL))
				all_final = false;
		}
		mask &= ~UNVEIL_PERM_NONINHERITED_MASK;
	} while (!all_final && (node1 = node1->cover));
	/*
	 * Merge wanted permissions and mask them with the frozen permissions.
	 */
	soft_perms = UNVEIL_PERM_NONE;
	for (i = 0; i < UNVEIL_SLOT_COUNT; i++)
		soft_perms |= inherited_perms[i];
	soft_perms &= node->frozen_perms[role];
	return (soft_perms);
}

static void
unveil_node_freeze(struct unveil_node *node, enum unveil_role role, unveil_perms_t keep)
{
	node->frozen_perms[role] &= keep | unveil_node_soft_perms(node, role);
}

static void
unveil_node_exec_to_curr(struct unveil_node *node)
{
	int i;
	node->frozen_perms[UNVEIL_ROLE_CURR] = node->frozen_perms[UNVEIL_ROLE_EXEC];
	for (i = 0; i < UNVEIL_SLOT_COUNT; i++)
		node->wanted_perms[UNVEIL_ROLE_CURR][i] = node->wanted_perms[UNVEIL_ROLE_EXEC][i];
}


static inline int
memcmplen(const char *p0, size_t l0, const char *p1, size_t l1)
{
	int r;
	r = memcmp(p0, p1, MIN(l0, l1));
	if (r != 0)
		return (r);
	return (l0 > l1 ? 1 : l0 < l1 ? -1 : 0);
}

static int
unveil_node_cmp(struct unveil_node *n0, struct unveil_node *n1)
{
	uintptr_t p0 = (uintptr_t)n0->vp, p1 = (uintptr_t)n1->vp;
	return (p0 > p1 ? 1 : p0 < p1 ? -1 :
	    memcmplen(n0->name, n0->name_len, n1->name, n1->name_len));
}

RB_GENERATE_STATIC(unveil_node_tree, unveil_node, entry, unveil_node_cmp);


void
unveil_init(struct unveil_base *base)
{
	*base = (struct unveil_base){
		.root = RB_INITIALIZER(&base->root),
	};
}

void
unveil_merge(struct unveil_base *dst, struct unveil_base *src)
{
	struct unveil_node *dst_node, *src_node;
	dst->active = src->active;
	dst->exec_active = src->exec_active;
	/* first pass, copy the nodes without cover links */
	RB_FOREACH(src_node, unveil_node_tree, &src->root) {
		dst_node = unveil_insert(dst, src_node->vp,
		    src_node->name, src_node->name_len, NULL);
		memcpy(dst_node->frozen_perms, src_node->frozen_perms,
		    sizeof (dst_node->frozen_perms));
		memcpy(dst_node->wanted_perms, src_node->wanted_perms,
		    sizeof (dst_node->wanted_perms));
	}
	/* second pass, fixup the cover links */
	RB_FOREACH(src_node, unveil_node_tree, &src->root) {
		if (!src_node->cover)
			continue;
		dst_node = unveil_lookup(dst, src_node->vp,
		    src_node->name, src_node->name_len);
		KASSERT(dst_node, ("unveil node missing"));
		dst_node->cover = unveil_lookup(dst, src_node->cover->vp,
		    src_node->cover->name, src_node->cover->name_len);
		KASSERT(dst_node->cover, ("cover unveil node missing"));
	}
}

static void
unveil_node_remove(struct unveil_base *base, struct unveil_node *node)
{
	RB_REMOVE(unveil_node_tree, &base->root, node);
	base->node_count--;
	vrele(node->vp);
	free(node, M_UNVEIL);
}

void
unveil_clear(struct unveil_base *base)
{
	struct unveil_node *node, *node_tmp;
	RB_FOREACH_SAFE(node, unveil_node_tree, &base->root, node_tmp)
	    unveil_node_remove(base, node);
	MPASS(base->node_count == 0);
}

void
unveil_free(struct unveil_base *base)
{
	unveil_clear(base);
}

struct unveil_node *
unveil_insert(struct unveil_base *base,
    struct vnode *vp, const char *name, size_t name_len, struct unveil_node *cover)
{
	struct unveil_node *new, *old;
	int i;
	new = malloc(sizeof *new + name_len + 1, M_UNVEIL, M_WAITOK);
	*new = (struct unveil_node){
		.cover = cover,
		.vp = vp,
		.name_len = name_len,
		.name = (char *)(new + 1),
	};
	memcpy(new->name, name, name_len);
	new->name[name_len] = '\0'; /* not required by this code */
	old = RB_INSERT(unveil_node_tree, &base->root, new);
	if (old) {
		free(new, M_UNVEIL);
		return (old);
	}
	for (i = 0; i < UNVEIL_ROLE_COUNT; i++)
		new->frozen_perms[i] = cover ? cover->frozen_perms[i] :
		                       base->active ? UNVEIL_PERM_NONE :
		                                      UNVEIL_PERM_ALL;
	vref(vp);
	base->node_count++;
	return (new);
}

struct unveil_node *
unveil_lookup(struct unveil_base *base, struct vnode *vp, const char *name, size_t name_len)
{
	struct unveil_node key;
	key.vp = vp;
	key.name = __DECONST(char *, name);
	key.name_len = name_len;
	return (RB_FIND(unveil_node_tree, &base->root, &key));
}


void
unveil_fd_init(struct filedesc *fd)
{
	unveil_init(&fd->fd_unveil);
}

void
unveil_fd_merge(struct filedesc *dst_fdp, struct filedesc *src_fdp)
{
	unveil_merge(&dst_fdp->fd_unveil, &src_fdp->fd_unveil);
}

void
unveil_fd_free(struct filedesc *fdp)
{
	unveil_free(&fdp->fd_unveil);
}

void
unveil_proc_exec_switch(struct thread *td)
{
	struct filedesc *fdp = td->td_proc->p_fd;
	struct unveil_base *base = &fdp->fd_unveil;
	struct unveil_node *node, *node_tmp;
	base->active = base->exec_active;
	RB_FOREACH_SAFE(node, unveil_node_tree, &base->root, node_tmp)
		unveil_node_exec_to_curr(node);
}


#define	FOREACH_SLOT_FLAGS(flags, i, j) \
	for (i = 0; i < UNVEIL_ROLE_COUNT; i++) \
		if ((flags) & (1 << (UNVEIL_FLAG_ROLE_SHIFT + i))) \
			for (j = 0; j < UNVEIL_SLOT_COUNT; j++) \
				if ((flags) & (1 << (UNVEIL_FLAG_SLOT_SHIFT + j)))

struct unveil_namei_data {
	int flags;
	unveil_perms_t perms;
};

int
unveil_traverse_save(struct unveil_base *base,
    struct unveil_namei_data *data, struct unveil_node **cover,
    struct vnode *dvp, const char *name, size_t name_len, struct vnode *vp, bool last)
{
	int flags = data->flags;
	unveil_perms_t perms = data->perms;
	struct unveil_node *node;
	int i, j;
	if (!(last || (flags & UNVEIL_FLAG_INTERMEDIATE)))
		return (unveil_traverse(base, data, cover, dvp, name, name_len, vp, last));

	if (name_len > NAME_MAX)
		return (ENAMETOOLONG);
	if (base->node_count >= unveil_max_nodes_per_process)
		return (E2BIG);

	if ((flags & UNVEIL_FLAG_NONDIRBYNAME) && last && dvp && (!vp || vp->v_type != VDIR))
		node = unveil_insert(base, dvp, name, name_len, *cover);
	else if (vp)
		node = unveil_insert(base, vp, "", 0, *cover);
	else
		return (ENOTDIR); /* XXX */

	if (last) {
		if (flags & UNVEIL_FLAG_NOINHERIT)
			perms |= UNVEIL_PERM_FINAL;
		FOREACH_SLOT_FLAGS(flags, i, j)
			node->wanted_perms[i][j] = perms;
	} else if (flags & UNVEIL_FLAG_INSPECTABLE) {
		FOREACH_SLOT_FLAGS(flags, i, j)
			node->wanted_perms[i][j] |= UNVEIL_PERM_INSPECT;
	}
	*cover = node;
	return (0);
}

int
unveil_traverse(struct unveil_base *base,
    struct unveil_namei_data *data, struct unveil_node **cover,
    struct vnode *dvp, const char *name, size_t name_len, struct vnode *vp, bool last)
{
	struct unveil_node *node;
	if (vp)
		node = unveil_lookup(base, vp, "", 0);
	else
		node = NULL;
	if (!node && last && dvp && (!vp || vp->v_type != VDIR))
		node = unveil_lookup(base, dvp, name, name_len);
	if (node)
		*cover = node;
	return (0);
}


static void
do_unveil_limit(struct unveil_base *base, int flags, unveil_perms_t perms)
{
	struct unveil_node *node;
	int i, j;
	perms |= UNVEIL_PERM_FINAL;
	RB_FOREACH(node, unveil_node_tree, &base->root)
		FOREACH_SLOT_FLAGS(flags, i, j)
			node->wanted_perms[i][j] &= perms;
}

static void
do_unveil_freeze(struct unveil_base *base, int flags, unveil_perms_t perms)
{
	struct unveil_node *node;
	int i;
	RB_FOREACH(node, unveil_node_tree, &base->root)
		for (i = 0; i < UNVEIL_ROLE_COUNT; i++)
			if (flags & (1 << (UNVEIL_FLAG_ROLE_SHIFT + i)))
				unveil_node_freeze(node, i, perms);
}

static void
do_unveil_sweep(struct unveil_base *base, int flags)
{
	struct unveil_node *node;
	int i, j;
	RB_FOREACH(node, unveil_node_tree, &base->root)
		FOREACH_SLOT_FLAGS(flags, i, j)
			node->wanted_perms[i][j] = UNVEIL_PERM_NONE;
}

#endif /* UNVEIL */

int
sys_unveilctl(struct thread *td, struct unveilctl_args *uap)
{
#ifdef UNVEIL
	struct filedesc *fdp = td->td_proc->p_fd;
	struct unveil_base *base = &fdp->fd_unveil;
	int flags = uap->flags;
	unveil_perms_t perms = uap->perms;

	perms &= ~(unveil_perms_t)UNVEIL_PERM_FINAL;

	if (!unveil_enabled)
		return (EPERM);

	if (uap->path != NULL) {
		struct unveil_namei_data data = { flags, perms };
		struct nameidata nd;
		int error;
		int nd_flags;
		nd_flags = flags & UNVEIL_FLAG_NOFOLLOW ? 0 : FOLLOW;
		NDINIT_ATRIGHTS(&nd, LOOKUP, nd_flags,
		    UIO_USERSPACE, uap->path, uap->atfd, &cap_no_rights, td);
		/* this will cause namei() to call unveil_traverse_save() */
		nd.ni_unveil_save = &data;
		error = namei(&nd);
		if (error)
			return (error);
		NDFREE(&nd, 0);
	}

	FILEDESC_XLOCK(fdp);

	if (flags & UNVEIL_FLAG_ACTIVATE) {
		if (flags & UNVEIL_FLAG_FOR_CURR)
			base->active = true;
		if (flags & UNVEIL_FLAG_FOR_EXEC)
			base->exec_active = true;
	}
	if (flags & UNVEIL_FLAG_LIMIT)
		do_unveil_limit(base, flags, perms);
	if (flags & UNVEIL_FLAG_FREEZE)
		do_unveil_freeze(base, flags, perms);
	if (flags & UNVEIL_FLAG_SWEEP)
		do_unveil_sweep(base, flags);

	FILEDESC_XUNLOCK(fdp);
	return (0);
#else
	return (ENOSYS);
#endif /* UNVEIL */
}
