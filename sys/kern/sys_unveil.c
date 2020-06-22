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
#include <sys/sysfil.h>
#include <sys/unveil.h>

#ifdef UNVEIL

MALLOC_DEFINE(M_UNVEIL, "unveil", "unveil");

/* TODO: global on/off switch for unveil support */

static unsigned int unveil_max_nodes_per_process = 100;
SYSCTL_UINT(_kern, OID_AUTO, maxunveilsperproc, CTLFLAG_RW,
	&unveil_max_nodes_per_process, 0, "Maximum unveils allowed per process");


unveil_perms_t
unveil_node_soft_perms(struct unveil_node *node, enum unveil_role role)
{
	struct unveil_node *node1;
	unveil_perms_t inherited_perms[UNVEIL_SLOT_COUNT], soft_perms;
	bool all_filled;
	int i;
	for (i = 0; i < UNVEIL_SLOT_COUNT; i++)
		inherited_perms[i] = UNVEIL_PERM_NONE;
	/*
	 * Go up the node chain until all wanted permissions have been found
	 * without any more inheritance required.
	 */
	node1 = node;
	do {
		all_filled = true;
		for (i = 0; i < UNVEIL_SLOT_COUNT; i++) {
			if (!(inherited_perms[i] & UNVEIL_PERM_FILLED))
				inherited_perms[i] |= node1->want_perms[role][i];
			if (!(inherited_perms[i] & UNVEIL_PERM_FILLED))
				all_filled = false;
		}
	} while (!all_filled && (node1 = node1->cover));
	/*
	 * Merge wanted permissions and mask them with the hard permissions.
	 */
	soft_perms = UNVEIL_PERM_NONE;
	for (i = 0; i < UNVEIL_SLOT_COUNT; i++)
		soft_perms |= inherited_perms[i];
	soft_perms &= node->hard_perms[role];
	return (soft_perms);
}

static void
unveil_node_harden(struct unveil_node *node, enum unveil_role role, unveil_perms_t keep)
{
	node->hard_perms[role] &= keep | unveil_node_soft_perms(node, role);
}

static void
unveil_node_exec_to_curr(struct unveil_node *node)
{
	int i;
	node->hard_perms[UNVEIL_ROLE_CURR] = node->hard_perms[UNVEIL_ROLE_EXEC];
	for (i = 0; i < UNVEIL_SLOT_COUNT; i++)
		node->want_perms[UNVEIL_ROLE_CURR][i] = node->want_perms[UNVEIL_ROLE_EXEC][i];
}

static void
unveil_node_flatten(struct unveil_node *node)
{
	int i, j;
	for (i = 0; i < UNVEIL_ROLE_COUNT; i++) {
		unveil_node_harden(node, i, UNVEIL_PERM_NONE);
		for (j = 0; j < UNVEIL_SLOT_COUNT; j++)
			node->want_perms[i][j] = node->hard_perms[i] | UNVEIL_PERM_FILLED;
	}
}


static int
unveil_node_cmp(struct unveil_node *a, struct unveil_node *b)
{
	uintptr_t ak = (uintptr_t)a->vp, bk = (uintptr_t)b->vp;
	return (ak > bk ? 1 : ak < bk ? -1 : 0);
}

RB_GENERATE_STATIC(unveil_node_tree, unveil_node, entry, unveil_node_cmp);


void
unveil_init(struct unveil_base *base)
{
	*base = (struct unveil_base){
		.root = RB_INITIALIZER(&base->root),
		.active = false,
	};
}

void
unveil_merge(struct unveil_base *dst, struct unveil_base *src)
{
	struct unveil_node *dst_node, *src_node;
	dst->active = src->active;
	/* first pass, copy the nodes without cover links */
	RB_FOREACH(src_node, unveil_node_tree, &src->root) {
		dst_node = unveil_insert(dst, src_node->vp, NULL);
		memcpy(dst_node->hard_perms, src_node->hard_perms,
		    sizeof (dst_node->hard_perms));
		memcpy(dst_node->want_perms, src_node->want_perms,
		    sizeof (dst_node->want_perms));
	}
	/* second pass, fixup the cover links */
	RB_FOREACH(src_node, unveil_node_tree, &src->root) {
		if (!src_node->cover)
			continue;
		dst_node = unveil_lookup(dst, src_node->vp);
		KASSERT(dst_node, ("unveil node missing"));
		dst_node->cover = unveil_lookup(dst, src_node->cover->vp);
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
unveil_insert(struct unveil_base *base, struct vnode *vp, struct unveil_node *cover)
{
	struct unveil_node *new, *old;
	new = malloc(sizeof *new, M_UNVEIL, M_WAITOK);
	*new = (struct unveil_node){
		.cover = cover,
		.vp = vp,
	};
	old = RB_INSERT(unveil_node_tree, &base->root, new);
	if (old) {
		free(new, M_UNVEIL);
		new = old;
	} else {
		vref(vp);
		base->node_count++;
	}
	return (new);
}

struct unveil_node *
unveil_lookup(struct unveil_base *base, struct vnode *vp)
{
	struct unveil_node key = { .vp = vp };
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
	RB_FOREACH_SAFE(node, unveil_node_tree, &base->root, node_tmp) {
		unveil_node_flatten(node);
		unveil_node_exec_to_curr(node);
	}
}


#define	FOREACH_SLOT_FLAGS(flags, i, j) \
	for (i = 0; i < UNVEIL_ROLE_COUNT; i++) \
		if ((flags) & (1 << (UNVEIL_FLAG_ROLE_SHIFT + i))) \
			for (j = 0; j < UNVEIL_SLOT_COUNT; j++) \
				if ((flags) & (1 << (UNVEIL_FLAG_SLOT_SHIFT + j)))

static int
do_unveil_add(struct unveil_base *base, int flags, unveil_perms_t perms,
    struct vnode *vp, struct unveil_node *cover)
{
	struct unveil_node *node;
	int i, j;
	node = cover && cover->vp == vp ? cover : NULL;
	if (base->node_count >= unveil_max_nodes_per_process && !node)
		return (E2BIG);

	if (!node) {
		node = unveil_insert(base, vp, cover);
		if (cover) {
			memcpy(node->hard_perms, cover->hard_perms,
			    sizeof (node->hard_perms));
		} else if (!base->active) {
			for (i = 0; i < UNVEIL_ROLE_COUNT; i++)
				node->hard_perms[i] = UNVEIL_PERM_ALL;
		}
	}

	if (flags & UNVEIL_FLAG_NOINHERIT)
		perms |= UNVEIL_PERM_FILLED;
	FOREACH_SLOT_FLAGS(flags, i, j)
		node->want_perms[i][j] = perms;
	return (0);
}

static void
do_unveil_limit(struct unveil_base *base, int flags, unveil_perms_t perms)
{
	struct unveil_node *node;
	int i, j;
	perms |= UNVEIL_PERM_FILLED;
	RB_FOREACH(node, unveil_node_tree, &base->root)
		FOREACH_SLOT_FLAGS(flags, i, j)
			node->want_perms[i][j] &= perms;
}

static void
do_unveil_harden(struct unveil_base *base, int flags, unveil_perms_t perms)
{
	struct unveil_node *node;
	int i;
	RB_FOREACH(node, unveil_node_tree, &base->root)
		for (i = 0; i < UNVEIL_ROLE_COUNT; i++)
			if (flags & (1 << (UNVEIL_FLAG_ROLE_SHIFT + i)))
				unveil_node_harden(node, i, perms);
}

static void
do_unveil_sweep(struct unveil_base *base, int flags)
{
	struct unveil_node *node;
	int i, j;
	RB_FOREACH(node, unveil_node_tree, &base->root)
		FOREACH_SLOT_FLAGS(flags, i, j)
			node->want_perms[i][j] = UNVEIL_PERM_NONE;
}

static int
do_unveil(struct thread *td, int atfd, const char *path,
    int flags, unveil_perms_t perms)
{
	struct filedesc *fdp = td->td_proc->p_fd;
	struct unveil_base *base;
	struct nameidata nd;
	int error = 0;
	bool adding;

	perms &= ~(unveil_perms_t)UNVEIL_PERM_FILLED;

	if ((adding = path != NULL)) {
		cap_rights_t rights;
		int nd_flags = LOCKLEAF;
		if (!(flags & UNVEIL_FLAG_NOFOLLOW))
			nd_flags |= FOLLOW;
		NDINIT_ATRIGHTS(&nd, LOOKUP, nd_flags, UIO_SYSSPACE,
		    path, atfd, cap_rights_init_zero(&rights), td);
		error = namei(&nd);
		if (error)
			return (error);
	}

	FILEDESC_XLOCK(fdp);
	base = &fdp->fd_unveil;

	if (adding) {
		error = do_unveil_add(base, flags, perms, nd.ni_vp, nd.ni_unveil);
		if (error)
			goto out;
		base->active = true;
	}

	if (flags & UNVEIL_FLAG_LIMIT)
		do_unveil_limit(base, flags, perms);
	if (flags & UNVEIL_FLAG_HARDEN)
		do_unveil_harden(base, flags, perms);
	if (flags & UNVEIL_FLAG_SWEEP)
		do_unveil_sweep(base, flags);

out:	FILEDESC_XUNLOCK(fdp);
	if (adding)
		NDFREE(&nd, 0);
	return (error);
}

#endif /* UNVEIL */

int
sys_unveilctl(struct thread *td, struct unveilctl_args *uap)
{
#ifdef UNVEIL
	char *path;
	int error;
	if (uap->path) {
		path = malloc(MAXPATHLEN, M_TEMP, M_WAITOK);
		error = copyinstr(uap->path, path, MAXPATHLEN, NULL);
		if (error) {
			free(path, M_TEMP);
			return (error);
		}
	} else
		path = NULL;
	error = do_unveil(td, uap->atfd, path, uap->flags, uap->perms);
	if (path)
		free(path, M_TEMP);
	return (error);
#else
	return (ENOSYS);
#endif /* UNVEIL */
}
