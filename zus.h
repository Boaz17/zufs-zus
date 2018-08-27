/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * zus.h - C Wrappers over the ZUFS_IOCTL Api
 *
 * Copyright (c) 2018 NetApp, Inc. All rights reserved.
 *
 * See module.c for LICENSE details.
 *
 * Authors:
 *	Boaz Harrosh <boazh@netapp.com>
 */
#ifndef __ZUS_H__
#define __ZUS_H__

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <sched.h>

#include <linux/stat.h>
/* This is a nasty hack for getting O_TMPFILE into centos 7.4
 * it already exists on centos7.4 but with a diffrent name
 * the value is the same
 * kernel support was introduced in kernel 3.11
 */

#ifndef O_TMPFILE
#define O_TMPFILE (__O_TMPFILE | O_DIRECTORY)
#endif

#include "zus_api.h"
#include "_pr.h"
#include "a_list.h"

/* FIXME: Move to K_in_U include structure */
#ifndef likely
#define likely(x_)	__builtin_expect(!!(x_), 1)
#define unlikely(x_)	__builtin_expect(!!(x_), 0)
#endif

#include "md.h"
#include "movnt.h"

#define MAX_LFS_FILESIZE 	((loff_t)0x7fffffffffffffffLL)
#define ZUS_MAX_OP_SIZE		(PAGE_SIZE * 8)

/* Time-stamps in zufs at inode and device-table are of this format */
#ifndef NSEC_PER_SEC
	#define NSEC_PER_SEC 1000000000UL
#endif

/* utils.c */
void zus_warn(const char *cond, const char *file, int line);
void zus_bug(const char *cond, const char *file, int line);

#define ZUS_WARN_ON(x_) ({ \
	int __ret_warn_on = !!(x_); \
	if (unlikely(__ret_warn_on)) \
		zus_warn(#x_, __FILE__, __LINE__); \
	unlikely(__ret_warn_on); \
})

#define ZUS_WARN_ON_ONCE(x_) ({				\
	int __ret_warn_on = !!(x_);			\
	static bool __once = false;			\
	if (unlikely(__ret_warn_on && !__once))	{	\
		zus_warn(#x_, __FILE__, __LINE__); 	\
		__once = true;				\
	}						\
	unlikely(__ret_warn_on);			\
})

#define ZUS_BUG_ON(x_) ({ \
	int __ret_bug_on = !!(x_); \
	if (unlikely(__ret_bug_on)) \
		zus_bug(#x_, __FILE__, __LINE__); \
	unlikely(__ret_bug_on); \
})

static inline __le32 le32_add(__le32 *val, __s16 add)
{
	return *val = cpu_to_le32(le32_to_cpu(*val) + add);
}

static inline __s64 _z_div_s64_rem(__s64 X, __s32 y, __u32 *rem)
{
	*rem = X % y;
	return X / y;
}

static inline void timespec_to_zt(__le64 *mt, struct timespec *t)
{
	*mt = cpu_to_le64(t->tv_sec * NSEC_PER_SEC + t->tv_nsec);
}

static inline void zt_to_timespec(struct timespec *t, __le64 *mt)
{
	__u32 nsec;

	t->tv_sec = _z_div_s64_rem(le64_to_cpu(*mt), NSEC_PER_SEC, &nsec);
	t->tv_nsec = nsec;
}

static inline
zu_dpp_t pmem_dpp_t(ulong offset) { return (zu_dpp_t)offset; }


/* ~~~~ zus fs_info super_blocks inodes ~~~~ */

struct zus_fs_info;
struct zus_sb_info;

struct zus_zii_operations {
	void (*evict)(struct zus_inode_info *zii);
	int (*read)(void *app_ptr, struct zufs_ioc_IO *io);
	int (*write)(void *app_ptr, struct zufs_ioc_IO *io);
	int (*get_block)(struct zus_inode_info *zii,
			 struct zufs_ioc_IO *get_block);
	int (*put_block)(struct zus_inode_info *zii,
			 struct zufs_ioc_IO *put_block);
	int (*mmap_close)(struct zus_inode_info *zii,
			 struct zufs_ioc_mmap_close *mmap_close);
	int (*get_symlink)(struct zus_inode_info *zii, void **symlink);
	int (*setattr)(struct zus_inode_info *zii,
		       uint enable_bits, ulong truncate_size);
	int (*sync)(struct zus_inode_info *zii,
		    struct zufs_ioc_range *ioc_range);
	int (*fallocate)(struct zus_inode_info *zii,
			 struct zufs_ioc_range *ioc_range);
	int (*seek)(struct zus_inode_info *zii, struct zufs_ioc_seek *ioc_seek);
	int (*ioctl)(struct zus_inode_info *zii,
		     struct zufs_ioc_ioctl *ioc_ioctl);
	int (*getxattr)(struct zus_inode_info *zii,
			struct zufs_ioc_xattr *ioc_xattr);
	int (*setxattr)(struct zus_inode_info *zii,
			struct zufs_ioc_xattr *ioc_xattr);
	int (*listxattr)(struct zus_inode_info *zii,
			 struct zufs_ioc_xattr *ioc_xattr);
};

struct zus_inode_info {
	const struct zus_zii_operations *op;

	struct zus_sb_info *sbi;
	struct zus_inode *zi;
};

struct zus_sbi_operations {
	struct zus_inode_info *(*zii_alloc)(struct zus_sb_info *sbi);
	void (*zii_free)(struct zus_inode_info *zii);

	int (*new_inode)(struct zus_sb_info *sbi, struct zus_inode_info *zii,
			 void *app_ptr, struct zufs_ioc_new_inode *ioc_new);
	int (*free_inode)(struct zus_inode_info *zii);

	ulong (*lookup)(struct zus_inode_info *dir_ii, struct zufs_str *str);
	int (*add_dentry)(struct zus_inode_info *dir_ii,
			  struct zus_inode_info *zii, struct zufs_str *str);
	int (*remove_dentry)(struct zus_inode_info *dir_ii,
			struct zus_inode_info *zii, struct zufs_str *str);
	int (*iget)(struct zus_sb_info *sbi, ulong ino,
		    struct zus_inode_info **zii);
	int (*rename)(struct zufs_ioc_rename *zir);
	int (*readdir)(void *app_ptr, struct zufs_ioc_readdir *zir);
	int (*clone)(struct zufs_ioc_clone *ioc_clone);
	int (*statfs)(struct zus_sb_info *sbi,
		      struct zufs_ioc_statfs *ioc_statfs);
};

#define ZUS_MAX_POOLS	7
struct pa {
	struct fba pages;
	struct fba data;
	struct a_list_head head;
	size_t size;
	pthread_spinlock_t lock;
};

struct zus_sb_info {
	struct multi_devices	md;
	struct zus_fs_info	*zfi;
	const struct zus_sbi_operations *op;

	struct zus_inode_info	*z_root;
	ulong			flags;
	__u64			kern_sb_id;
	struct pa		pa[ZUS_MAX_POOLS];
};

enum E_zus_sbi_flags {
	ZUS_SBIF_ERROR = 0,

	ZUS_SBIF_LAST,
};

static inline void _z_set_bit(uint flag, ulong *val) { *val |= (1 << flag); }
static inline
void zus_sbi_flag_set(struct zus_sb_info *sbi, int flag)
{
	_z_set_bit(flag, &sbi->flags);
}

struct zus_zfi_operations {
	struct zus_sb_info *(*sbi_alloc)(struct zus_fs_info *zfi);
	void (*sbi_free)(struct zus_sb_info *sbi);

	int (*sbi_init)(struct zus_sb_info *sbi, struct zufs_ioc_mount *zim);
	int (*sbi_fini)(struct zus_sb_info *sbi);
};

struct zus_fs_info {
	struct register_fs_info rfi;
	const struct zus_zfi_operations *op;
	const struct zus_sbi_operations *sbi_op;

	uint			user_page_size;
	uint			next_sb_id;
};

/* POSIX protocol helpers every one must use */

static inline bool zi_isdir(const struct zus_inode *zi)
{
	return S_ISDIR(le16_to_cpu(zi->i_mode));
}
static inline bool zi_isreg(const struct zus_inode *zi)
{
	return S_ISREG(le16_to_cpu(zi->i_mode));
}
static inline bool zi_islnk(const struct zus_inode *zi)
{
	return S_ISLNK(le16_to_cpu(zi->i_mode));
}
static inline ulong zi_ino(const struct zus_inode *zi)
{
	return le64_to_cpu(zi->i_ino);
}

/* Caller checks if (zi_isdir(zi)) */
static inline void zus_std_new_dir(struct zus_inode *dir_zi, struct zus_inode *zi)
{
	/* Directory points to itself (POSIX for you) */
	zi->i_dir.parent = dir_zi->i_ino;
	zi->i_nlink = cpu_to_le32(1);
}

static inline void zus_std_add_dentry(struct zus_inode *dir_zi,
				     struct zus_inode *zi)
{
	zi->i_nlink = le32_add(&zi->i_nlink, 1);

	if (zi_isdir(zi))
		le32_add(&dir_zi->i_nlink, 1);
}

static inline void zus_std_remove_dentry(struct zus_inode *dir_zi,
					struct zus_inode *zi)
{
	if (zi_isdir(zi)) {
		le32_add(&zi->i_nlink, -1);
		le32_add(&dir_zi->i_nlink, -1);
	}

	le32_add(&zi->i_nlink, -1);
}

/* zus-core */

/* Open an O_TMPFILE on the zuf-root we belong to */
int zuf_root_open_tmp(int *fd);
void zuf_root_close(int *fd);

/* ~~~ CPU & NUMA topology by zus ~~~ */

/* For all these to work user must create
 * his threads with zus_create_thread() below
 * Or from ZTs, or from mount-thread
 */
#define ZUS_NUMA_NO_NID	(~0U)
#define ZUS_CPU_ALL	(~0U)

extern struct zufs_ioc_numa_map g_zus_numa_map;
int zus_cpu_to_node(int cpu);
int zus_current_onecpu(void);
int zus_current_cpu(void);
int zus_current_nid(void);

int zus_get_cpu(void);
void zus_put_cpu(int cpu);

struct zus_thread_params {
	const char *name; /* only used for the duration of the call */
	int policy;
	int rr_priority;
	uint one_cpu;	/* either set this one. Else ZUS_CPU_ALL */
	uint nid;	/* Or set this one. Else ZUS_NUMA_NO_NID */
	ulong __flags; /* warnings on/off */
};

#define ZTP_INIT(ztp) 			\
{					\
	memset((ztp), 0, sizeof(*(ztp)));	\
	(ztp)->nid = (ztp)->one_cpu = (-1);	\
}

typedef void *(*__start_routine) (void *); /* pthread programming style NOT */
int zus_thread_create(pthread_t *new_tread, struct zus_thread_params *params,
		      __start_routine fn, void *user_arg);

/* zus-vfs.c */
int zus_register_all(int fd);
void zus_unregister_all(void);
int zus_register_one(int fd, struct zus_fs_info *p_zfi);

int zus_mount(int fd, struct zufs_ioc_mount *zim);
int zus_umount(int fd, struct zufs_ioc_mount *zim);
struct zus_inode_info *zus_iget(struct zus_sb_info *sbi, ulong ino);
int zus_do_command(void *app_ptr, struct zufs_ioc_hdr *hdr);

/* do not use, please use _zus_iom_submit() in iom_enc.h */
int __zus_iom_exec(struct zus_sb_info *sbi, struct zufs_ioc_iomap_exec *ziome,
		   bool sync);

/* pa.c */

/* fba - File backed Allocator
 * Gives user an allocated pointer which is derived from a /tmp/O_TMPFILE mmap.
 * The size is round up to 4K alignment.
 */
int  fba_alloc(struct fba *fba, size_t size);
void fba_free(struct fba *fba);
int fba_punch_hole(struct fba *fba, ulong index, uint nump);

/* pa - Page Allocator
 * Gives users a page Allocator pa_pages are ref-counted last put
 * frees the page. (pa_free is just a pa_put_page
 */
int pa_init(struct zus_sb_info *sbi);
void pa_fini(struct zus_sb_info *sbi);

struct pa_page {
	unsigned long		flags;
	void			*owner;

	unsigned long		index;
	int			units;
	int			refcount;

	struct a_list_head	list;

	unsigned long		private;
	void			*private2;
} __aligned(64);

struct pa_page *pa_alloc_order(struct zus_sb_info *sbi, int order);
static inline struct pa_page *pa_alloc(struct zus_sb_info *sbi)
{
	return pa_alloc_order(sbi, 0);
}

/* Must not be used by users, private to pa implementation */
void __pa_free(struct pa_page *page);

#define ZONE_SHIFT 56 /* last 8 bits */
#define ZONE_MASK (~((1UL << ZONE_SHIFT) - 1))
static inline void pa_set_page_zone(struct pa_page *page, int zone)
{
	page->flags &= ~ZONE_MASK;
	page->flags |= ((ulong)zone << ZONE_SHIFT);
}
static inline int pa_page_zone(struct pa_page *page)
{
	return page->flags >> ZONE_SHIFT;
}

static inline int _zu_atomic_add_unless(int *v, int a, int u)
{
	int c, old;

	c = __atomic_load_n(v, __ATOMIC_SEQ_CST);
	while (c != u &&
	       (old = __atomic_exchange_n(v, c + a, __ATOMIC_SEQ_CST)) != c)
		c = old;
	return c;
}

static inline int __atomic_dec_testzero(int *v)
{
	return (__atomic_fetch_sub(v, 1, __ATOMIC_SEQ_CST) == 1);
}

/* Return the incremented refcount, unless 0 */
static inline int pa_get_page(struct pa_page *page)
{
	return _zu_atomic_add_unless(&page->refcount, 1, 0);
}

/* Return true if the refcount droped to 0 */
static inline int pa_put_page(struct pa_page *page)
{
	if (__atomic_dec_testzero(&page->refcount)) {
		__pa_free(page);
		return 1;
	}
	return 0;
}

static inline void pa_free(struct zus_sb_info *sbi, struct pa_page *page)
{
	pa_put_page(page);
}

/* TODO: let the user decide on the pool number */
#define POOL_NUM	1

static inline struct pa_page *pa_bn_to_page(struct zus_sb_info *sbi, ulong bn)
{
	return sbi->pa[POOL_NUM].pages.ptr + bn * sizeof(struct pa_page);
}

static inline ulong pa_page_to_bn(struct zus_sb_info *sbi, struct pa_page *page)
{
	return ((void *)page - sbi->pa[POOL_NUM].pages.ptr) / sizeof(*page);
}

static inline void *pa_page_address(struct zus_sb_info *sbi,
				    struct pa_page *page)
{
	ulong bn = pa_page_to_bn(sbi, page);

	return sbi->pa[POOL_NUM].data.ptr + bn * PAGE_SIZE;
}

static inline bool _pa_valid_addr(struct pa *pa, void *addr)
{
	if (ZUS_WARN_ON((addr < pa->data.ptr) ||
			((pa->data.ptr + pa->size * PAGE_SIZE) <= addr))) {
		ERROR("Invalid address=%p data.ptr=%p data.end=%p\n",
		      addr, pa->data.ptr,
		      (pa->data.ptr + pa->size * PAGE_SIZE));
		return false;
	}
	return true;
}

static inline
struct pa_page *pa_virt_to_page(struct zus_sb_info *sbi, void *addr)
{
	struct pa *pa = &sbi->pa[POOL_NUM];

	if (!_pa_valid_addr(pa, addr))
		return NULL;

	return pa_bn_to_page(sbi, md_o2p(addr - pa->data.ptr));
}

static inline
ulong pa_addr_to_offset(struct zus_sb_info *sbi, void *addr)
{
	struct pa *pa = &sbi->pa[POOL_NUM];

	if (!_pa_valid_addr(pa, addr))
		return 0;

	return (addr - pa->data.ptr);
}

static inline void *pa_addr(struct zus_sb_info *sbi, ulong offset)
{
	struct pa *pa = &sbi->pa[POOL_NUM];

	return offset ? pa->data.ptr + offset : NULL;
}

#define ZUS_LIBFS_MAX_NR	16	/* see also MAX_LOCKDEP_FSs in zuf */
#define ZUS_LIBFS_MAX_PATH	256
#define ZUS_LIBFS_DIR		"/usr/lib/zufs"
#define ZUFS_LIBFS_LIST		"ZUFS_LIBFS_LIST"

/* declare so compiler will not complain */
extern int register_fs(int fd);
/* below two need to match. C is not bash */
#define REGISTER_FS_FN		 register_fs
#define REGISTER_FS_NAME	"register_fs"

#endif /* define __ZUS_H__ */
