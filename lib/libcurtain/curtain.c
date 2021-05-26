#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <err.h>
#include <sysexits.h>
#include <sys/unveil.h>
#include <sys/sysfil.h>

#include <curtain.h>

struct curtain_slot {
	struct curtain_slot *next;
	struct unveil_mode *unveil_modes;
	struct sysfil_mode *sysfil_modes;
	enum curtain_state state_on[CURTAIN_ON_COUNT];
};

struct sysfil_mode {
	struct curtain_slot *slot;
	struct sysfil_node *node;
	struct sysfil_mode *node_next, *slot_next;
};

struct sysfil_node {
	struct sysfil_node *next;
	struct sysfil_mode *modes;
	int sysfil;
};

struct unveil_mode {
	struct curtain_slot *slot;
	struct unveil_node *node;
	struct unveil_mode *node_next, *slot_next;
	struct unveil_mode *inherit_next, **inherit_saved_link, *inherit_saved;
	bool inherit, inspect;
	unveil_perms uperms;
	unveil_perms inherited_uperms;
};

struct unveil_node {
	struct unveil_node *parent, *children, *sibling;
	struct unveil_mode *modes;
	unsigned unveil_idx;
	bool initialized;
};


static struct curtain_slot *curtain_slots = NULL;

static size_t sysfils_count = 0;
static struct sysfil_node *sysfils_list = NULL;

static size_t unveils_count = 0;
static struct unveil_node **unveils_table = NULL;
static size_t unveils_table_size = 0;


static struct curtain_slot *
curtain_slot_1(const enum curtain_state state_on[CURTAIN_ON_COUNT])
{
	struct curtain_slot *slot;
	slot = malloc(sizeof *slot);
	if (!slot)
		return (NULL);
	*slot = (struct curtain_slot){ 0 };
	memcpy(slot->state_on, state_on, sizeof slot->state_on);
	slot->next = curtain_slots;
	curtain_slots = slot;
	return (slot);
}

struct curtain_slot *
curtain_slot(void)
{
	enum curtain_state state_on[CURTAIN_ON_COUNT] = {
		[CURTAIN_ON_SELF] = CURTAIN_ENABLED,
		[CURTAIN_ON_EXEC] = CURTAIN_ENABLED,
	};
	return (curtain_slot_1(state_on));
}

struct curtain_slot *
curtain_slot_neutral(void)
{
	enum curtain_state state_on[CURTAIN_ON_COUNT];
	for (enum curtain_on on = 0; on < CURTAIN_ON_COUNT; on++)
		state_on[on] = CURTAIN_NEUTRAL;
	return (curtain_slot_1(state_on));
}

struct curtain_slot *
curtain_slot_on(enum curtain_on on)
{
	enum curtain_state state_on[CURTAIN_ON_COUNT] = { 0 };
	state_on[on] = CURTAIN_ENABLED;
	return (curtain_slot_1(state_on));
}

void
curtain_enable(struct curtain_slot *slot, enum curtain_on on)
{ slot->state_on[on] = CURTAIN_ENABLED; };

void
curtain_disable(struct curtain_slot *slot, enum curtain_on on)
{ slot->state_on[on] = CURTAIN_DISABLED; };

void
curtain_state(struct curtain_slot *slot, enum curtain_on on, enum curtain_state state)
{ slot->state_on[on] = state; }


static struct sysfil_node *
get_sysfil(int sysfil)
{
	struct sysfil_node *node, **link;
	for (link = &sysfils_list; (node = *link); link = &node->next)
		if (node->sysfil == sysfil)
			break;
	if (!node) {
		node = malloc(sizeof *node);
		if (!node)
			err(EX_TEMPFAIL, "malloc");
		*node = (struct sysfil_node){ .sysfil = sysfil, .next = *link };
		*link = node;
	} else { /* move-to-front heuristic */
		*link = node->next;
		node->next = sysfils_list;
		sysfils_list = node;
	}
	return (node);
}

int
curtain_sysfil(struct curtain_slot *slot, int sysfil)
{
	struct sysfil_node *node;
	struct sysfil_mode *mode, **link;
	node = get_sysfil(sysfil);
	if (!node)
		return (-1);
	for (link = &node->modes; (mode = *link); link = &mode->node_next)
		if (mode->slot == slot)
			break;
	if (mode)
		return (0);
	if (!mode) {
		mode = malloc(sizeof *mode);
		if (!mode)
			err(EX_TEMPFAIL, "malloc");
		*mode = (struct sysfil_mode){
			.slot = slot,
			.node = node,
			.slot_next = slot->sysfil_modes,
			.node_next = *link,
		};
		slot->sysfil_modes = *link = mode;
		sysfils_count++;
	}
	return (0);
}

static void
fill_sysfils(unsigned *ents[CURTAIN_ON_COUNT], enum curtain_state min_state)
{
	struct sysfil_node *node;
	struct sysfil_mode *mode;
	for (node = sysfils_list; node; node = node->next)
		for (enum curtain_on on = 0; on < CURTAIN_ON_COUNT; on++)
			for (mode = node->modes; mode; mode = mode->node_next)
				if (mode->slot->state_on[on] >= min_state) {
					*ents[on]++ = node->sysfil;
					break;
				}
}


static struct unveil_node **
get_unveil_index_link(unsigned idx)
{
	if (idx >= unveils_table_size) {
		void *ptr;
		ptr = realloc(unveils_table, (idx + 1) * sizeof *unveils_table);
		if (!ptr)
			return (NULL);
		unveils_table = ptr;
		while (idx >= unveils_table_size)
			unveils_table[unveils_table_size++] = NULL;
	}
	return (&unveils_table[idx]);
}

static struct unveil_node *
get_unveil_index(unsigned idx)
{
	struct unveil_node **link;
	link = get_unveil_index_link(idx);
	if (!link)
		return (NULL);
	if (*link) {
		assert((**link).unveil_idx == idx);
	} else {
		*link = malloc(sizeof **link);
		if (!*link)
			return (NULL);
		**link = (struct unveil_node){ .unveil_idx = idx };
		unveils_count++;
	}
	return (*link);
}

static struct unveil_node *
get_unveil_index_pair(unsigned parent_idx, unsigned child_idx)
{
	struct unveil_node *parent, *child;
	if (parent_idx != child_idx) {
		parent = get_unveil_index(parent_idx);
		if (!parent)
			return (NULL);
	} else
		parent = NULL;
	child = get_unveil_index(child_idx);
	if (!child)
		return (NULL);
	if (!child->initialized) {
		/* XXX not reparenting nodes yet */
		if ((child->parent = parent)) {
			child->sibling = parent->children;
			parent->children = child;
		}
		child->initialized = true;
	}
	return (child);
}

static struct unveil_mode *
get_unveil_mode(struct curtain_slot *slot, struct unveil_node *node)
{
	struct unveil_mode *mode, **link;
	for (link = &node->modes; (mode = *link); link = &mode->node_next)
		/* keep list ordered */
		if ((uintptr_t)slot <= (uintptr_t)mode->slot) {
			if (slot != mode->slot)
				mode = NULL;
			break;
		}
	if (!mode) {
		mode = malloc(sizeof *mode);
		if (!mode)
			err(EX_TEMPFAIL, "malloc");
		*mode = (struct unveil_mode){
			.slot = slot,
			.node = node,
			.slot_next = slot->unveil_modes,
			.node_next = *link,
		};
		slot->unveil_modes = *link = mode;
	}
	return (mode);
}

int
curtain_unveil(struct curtain_slot *slot,
    const char *path, unsigned flags, unveil_perms uperms)
{
	struct unveil_node *node;
	struct unveil_mode *mode;
	unveil_index tev[UNVEILREG_MAX_TE][2];
	struct unveilreg reg = {
		.atfd = AT_FDCWD,
		.path = path,
		.tec = UNVEILREG_MAX_TE,
		.tev = tev,
	};
	ssize_t ter;
	ter = unveilreg(UNVEILREG_REGISTER |
	    UNVEILREG_INTERMEDIATE | UNVEILREG_NONDIRBYNAME, &reg);
	if (ter < 0) {
		if (errno != ENOENT && errno != EACCES && errno != ENOSYS)
			warn("%s: %s", __FUNCTION__, path);
		return (-1);
	}
	node = NULL;
	mode = NULL;
	for (ssize_t i = 0; i < ter; i++) {
		bool last = ter - i <= 1, follow = !last && tev[i][1] != tev[i + 1][0];
		node = get_unveil_index_pair(tev[i][0], tev[i][1]);
		if (!node)
			err(EX_TEMPFAIL, "malloc");
		if (last || follow || flags & CURTAIN_UNVEIL_INSPECT) {
			mode = get_unveil_mode(slot, node);
			if (!mode)
				return (-1);
			if (flags & CURTAIN_UNVEIL_INSPECT)
				mode->uperms |= UPERM_INSPECT;
			if (follow)
				/*
				 * XXX This is added even for unveils that only
				 * request write permissions, but it gets wiped
				 * by a curtain_unveils_limit() with the same
				 * permissions.
				 */
				mode->uperms |= UPERM_FOLLOW;
		}
	}
	if (mode) {
		mode->inherit = flags & CURTAIN_UNVEIL_INHERIT;
		mode->inspect = flags & CURTAIN_UNVEIL_INSPECT;
		mode->uperms = uperms_expand(uperms);
	}
	return (0);
}

int
curtain_unveils_limit(struct curtain_slot *slot, unveil_perms uperms)
{
	struct unveil_mode *mode;
	uperms = uperms_expand(uperms);
	for (mode = slot->unveil_modes; mode; mode = mode->slot_next)
		mode->uperms &= uperms;
	return (0);
}

static void
fill_unveils_1(struct unveil_node *node, struct unveil_mode *inherit_head,
    struct curtainent_unveil *ents[CURTAIN_ON_COUNT], enum curtain_state min_state)
{
	struct unveil_mode *mode, *cmode, *imode, **ilink, *inherit_saved;
	struct unveil_node *child;
	unveil_perms uperms_on[CURTAIN_ON_COUNT];

#if 0
	for (mode = node->modes; mode && mode->node_next; mode = mode->node_next)
		assert((uintptr_t)mode->node_next->slot > (uintptr_t)mode->slot);
	for (mode = inherit_head; mode && mode->inherit_next; mode = mode->inherit_next)
		assert((uintptr_t)mode->inherit_next->slot > (uintptr_t)mode->slot);
#endif

	/*
	 * Merge join the inherited and current node's modes to handle
	 * inheritance between modes of corresponding slots.  The current
	 * node's modes are spliced into the inherited list replacing inherited
	 * modes for the same slot (if any).  The list is restored to its
	 * previous state before returning.
	 */
	for (enum curtain_on on = 0; on < CURTAIN_ON_COUNT; on++)
		uperms_on[on] = UPERM_NONE;
	cmode = node->modes;
	inherit_saved = inherit_head;
	imode = *(ilink = &inherit_head);
	while (cmode || imode) {
		assert(!cmode || !cmode->node_next ||
		    (uintptr_t)cmode->slot < (uintptr_t)cmode->node_next->slot);
		assert(!imode || !imode->inherit_next ||
		    (uintptr_t)imode->slot < (uintptr_t)imode->inherit_next->slot);
		assert(*ilink == imode);
		if (imode && (!cmode || (uintptr_t)cmode->slot > (uintptr_t)imode->slot)) {
			/*
			 * Inherited mode with no corresponding current node
			 * mode.  Permissions carry through nodes without
			 * (explicit) modes for a given slot.
			 */
			for (enum curtain_on on = 0; on < CURTAIN_ON_COUNT; on++)
				if (imode->slot->state_on[on] >= min_state)
					uperms_on[on] |= uperms_inherit(
					    imode->uperms | imode->inherited_uperms);
			imode = *(ilink = &imode->inherit_next);
		} else {
			/*
			 * Current node mode with or without a corresponding
			 * inherited mode.  Splice the current node in the
			 * inherited list with updated permissions.
			 */
			bool match, carry;
			match = imode && imode->slot == cmode->slot;
			cmode->inherited_uperms = match && cmode->inherit ?
			    uperms_inherit(imode->inherited_uperms | imode->uperms) :
			    UPERM_NONE;
			carry = false;
			for (enum curtain_on on = 0; on < CURTAIN_ON_COUNT; on++)
				if (cmode->slot->state_on[on] >= min_state) {
					unveil_perms uperms = cmode->uperms |
					    cmode->inherited_uperms;
					uperms_on[on] |= uperms;
					if (uperms != UPERM_NONE)
						carry = true;
				}
			cmode->inherit_saved_link = ilink;
			cmode->inherit_saved = imode;
			if (match)
				imode = imode->inherit_next;
			if (carry) {
				*ilink = cmode;
				*(ilink = &cmode->inherit_next) = imode;
			} else /* no permissions to inherit from this node */
				*ilink = imode;
			cmode = cmode->node_next;
		}
	}
#if 0
	for (mode = inherit_head; mode && mode->inherit_next; mode = mode->inherit_next)
		assert((uintptr_t)mode->inherit_next->slot > (uintptr_t)mode->slot);
#endif

	for (enum curtain_on on = 0; on < CURTAIN_ON_COUNT; on++)
		*ents[on]++ = (struct curtainent_unveil){
			.index = node->unveil_idx,
			.uperms = uperms_on[on],
		};

	for (child = node->children; child; child = child->sibling) {
		assert(child->parent == node);
		fill_unveils_1(child, inherit_head, ents, min_state);
	}

	for (mode = inherit_saved; mode; mode = mode->inherit_next)
		if (mode->node == node && mode->inherit_saved_link)
			*mode->inherit_saved_link = mode->inherit_saved;
}

static void
fill_unveils(struct curtainent_unveil *ents[CURTAIN_ON_COUNT], enum curtain_state min_state)
{
	struct unveil_node *node, **link;
	/* TODO optimize */
	for (link = unveils_table; link < &unveils_table[unveils_table_size]; link++)
		if ((node = *link) && !node->parent)
			fill_unveils_1(node, NULL, ents, min_state);
}


static const int curtainctl_flags[CURTAIN_ON_COUNT] = {
	[CURTAIN_ON_SELF] = CURTAINCTL_ON_SELF,
	[CURTAIN_ON_EXEC] = CURTAINCTL_ON_EXEC,
};

static const int curtainreq_flags[CURTAIN_ON_COUNT] = {
	[CURTAIN_ON_SELF] = CURTAINREQ_ON_SELF,
	[CURTAIN_ON_EXEC] = CURTAINREQ_ON_EXEC,
};

static int
curtain_submit_1(int flags, enum curtain_state min_state)
{
	struct curtainreq reqv[2 * CURTAIN_ON_COUNT], *reqp = reqv;
	unsigned sysfils_v[CURTAIN_ON_COUNT][sysfils_count];
	unsigned *sysfils_p[CURTAIN_ON_COUNT];
	struct curtainent_unveil unveils_v[CURTAIN_ON_COUNT][unveils_count];
	struct curtainent_unveil *unveils_p[CURTAIN_ON_COUNT];

	for (enum curtain_on on = 0; on < CURTAIN_ON_COUNT; on++) {
		sysfils_p[on] = sysfils_v[on];
		unveils_p[on] = unveils_v[on];
	}
	fill_sysfils(sysfils_p, min_state);
	fill_unveils(unveils_p, min_state);

	for (enum curtain_on on = 0; on < CURTAIN_ON_COUNT; on++)
		if (flags & curtainctl_flags[on]) {
			size_t sysfils_c, unveils_c;
			if ((sysfils_c = sysfils_p[on] - sysfils_v[on]) != 0)
				*reqp++ = (struct curtainreq){
					.type = CURTAIN_SYSFIL,
					.flags = curtainreq_flags[on],
					.data = sysfils_v[on],
					.size = sysfils_c * sizeof **sysfils_v,
				};
			if ((unveils_c = unveils_p[on] - unveils_v[on]) != 0)
				*reqp++ = (struct curtainreq){
					.type = CURTAIN_UNVEIL,
					.flags = curtainreq_flags[on],
					.data = unveils_v[on],
					.size = unveils_c * sizeof **unveils_v,
				};
		}

	return (curtainctl(flags, reqp - reqv, reqv));
}

static int
curtain_submit(bool enforce)
{
	bool neutral_on[CURTAIN_ON_COUNT], neutral, reserve;
	int r, flags;

	neutral = true, reserve = false;
	for (enum curtain_on on = 0; on < CURTAIN_ON_COUNT; on++)
		neutral_on[on] = true;
	for (struct curtain_slot *slot = curtain_slots; slot; slot = slot->next) {
		for (enum curtain_on on = 0; on < CURTAIN_ON_COUNT; on++) {
			if (slot->state_on[on] > CURTAIN_NEUTRAL)
				neutral = neutral_on[on] = false;
			if (slot->state_on[on] == CURTAIN_RESERVED)
				reserve = true;
		}
		if (!neutral && reserve)
			break;
	}

	flags = 0;
	for (enum curtain_on on = 0; on < CURTAIN_ON_COUNT; on++)
		if (!neutral_on[on])
			flags |= curtainctl_flags[on];
	flags |= CURTAINCTL_ENGAGE;
	if (enforce) {
		int flags1 = flags | CURTAINCTL_ENFORCE;
		if (reserve) {
			r = curtain_submit_1(flags1, CURTAIN_RESERVED);
			if (r < 0 && errno != ENOSYS)
				err(EX_OSERR, "curtainctl");
		} else
			flags = flags1;
	}
	r = curtain_submit_1(flags, CURTAIN_ENABLED);
	if (r < 0 && errno != ENOSYS)
		err(EX_OSERR, "curtainctl");
	return (r);
}

int
curtain_apply(void) { return (curtain_submit(false)); }

int
curtain_enforce(void) { return (curtain_submit(true)); }
