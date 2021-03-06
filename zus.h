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

/* sys/stat.h must be included the very first */
#include <sys/stat.h>

#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <sched.h>
#include <sys/mman.h>

/* On old GCC systems we do not yet have STATX stuff in sys/stat.h
 * so in this case include also the kernel header.
 */
#ifndef STATX_MODE
#include <linux/stat.h>
#endif

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

#include "md.h"
#include "movnt.h"

#define MAX_LFS_FILESIZE 	((loff_t)0x7fffffffffffffffLL)
#define ZUS_MAX_OP_SIZE		(PAGE_SIZE * 16)

/* Time-stamps in zufs at inode and device-table are of this format */
#ifndef NSEC_PER_SEC
	#define NSEC_PER_SEC 1000000000UL
#endif

/* utils.c */
void zus_dump_stack(FILE *fp, bool warn, const char *fmt, ...);
void zus_warn(const char *cond, const char *file, int line);
void zus_bug(const char *cond, const char *file, int line);
int zus_increase_max_files(void);

#define dump_stack() \
	zus_dump_stack(stderr, false, "<5>%s: (%s:%d)\n", \
			__func__, __FILE__, __LINE__)

#define ZUS_WARN_ON(x_) ({ \
	int __ret_warn_on = !!(x_); \
	if (unlikely(__ret_warn_on)) \
		zus_warn(#x_, __FILE__, __LINE__); \
	unlikely(__ret_warn_on); \
})

#define ZUS_WARN_ON_ONCE(x_) ({				\
	int __ret_warn_on = !!(x_);			\
	static bool __once;				\
	if (unlikely(__ret_warn_on && !__once))	{	\
		zus_warn(#x_, __FILE__, __LINE__);	\
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

#ifndef BUILD_BUG_ON
#	define BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2*!!(condition)]))
#endif

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
	int (*pre_read)(void *app_ptr, struct zufs_ioc_IO *io);
	int (*write)(void *app_ptr, struct zufs_ioc_IO *io);
	int (*get_put_multy)(struct zus_inode_info *zii,
			 struct zufs_ioc_IO *io);
	int (*mmap_close)(struct zus_inode_info *zii,
			 struct zufs_ioc_mmap_close *mmap_close);
	int (*get_symlink)(struct zus_inode_info *zii, void **symlink);
	int (*setattr)(struct zus_inode_info *zii, uint attr_bits);
	int (*sync)(struct zus_inode_info *zii,
		    struct zufs_ioc_sync *ioc_range);
	int (*fallocate)(struct zus_inode_info *zii,
			 struct zufs_ioc_IO *ioc_IO);
	int (*seek)(struct zus_inode_info *zii, struct zufs_ioc_seek *ioc_seek);
	int (*ioctl)(struct zus_inode_info *zii,
		     struct zufs_ioc_ioctl *ioc_ioctl);
	int (*getxattr)(struct zus_inode_info *zii,
			struct zufs_ioc_xattr *ioc_xattr);
	int (*setxattr)(struct zus_inode_info *zii,
			struct zufs_ioc_xattr *ioc_xattr);
	int (*listxattr)(struct zus_inode_info *zii,
			 struct zufs_ioc_xattr *ioc_xattr);
	int (*fiemap)(void *app_ptr, struct zufs_ioc_fiemap *zif);
};

struct zus_inode_info {
	const struct zus_zii_operations *op;

	struct zus_sb_info *sbi;
	struct zus_inode *zi;
};

struct zus_sbi_operations {
	int (*new_inode)(struct zus_sb_info *sbi, void *app_ptr,
			 struct zufs_ioc_new_inode *ioc_new);
	void (*free_inode)(struct zus_inode_info *zii);

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
	int (*show_options)(struct zus_sb_info *sbi,
			    struct zufs_ioc_mount_options *zim);
};

struct fba {
	int fd; void *ptr;
	size_t size;
	void *orig_ptr;
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

static inline void zus_sbi_set_flag(struct zus_sb_info *sbi, int flag)
{
	sbi->flags |= (1 << flag);
}

static inline int zus_sbi_test_flag(struct zus_sb_info *sbi, int flag)
{
	return (sbi->flags & (1UL << flag));
}

static inline int _buf_puts(char **buffer, ssize_t *size, const char *option)
{
	ssize_t len = strlen(option);

	if (*size + len > ZUFS_MO_MAX)
		return 0;

	*size += len;
	memcpy(*buffer, option, len);
	*buffer += len;

	return len;
}

struct zus_zfi_operations {
	struct zus_sb_info *(*sbi_alloc)(struct zus_fs_info *zfi);
	void (*sbi_free)(struct zus_sb_info *sbi);

	int (*sbi_init)(struct zus_sb_info *sbi, struct zufs_mount_info *zmi);
	int (*sbi_fini)(struct zus_sb_info *sbi);
	int (*sbi_remount)(struct zus_sb_info *sbi, struct zufs_mount_info *zmi);
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

int zus_zt_signal_pending(void);

int zus_numa_map_init(int fd);
int zus_init_zuf(const char *zuf_path);
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

extern struct zufs_ioc_numa_map *zus_numa_map;
extern unsigned int zus_nr_cpu_ids;
extern cpu_set_t *zus_cpu_possible_mask;
extern cpu_set_t *zus_cpu_online_mask;
int zus_cpu_to_node(int cpu);
bool zus_cpu_online(int cpu);
int zus_current_onecpu(void);
int zus_current_cpu(void);
int zus_current_cpu_silent(void);
int zus_current_nid(void);
unsigned int zus_cpumask_next(int n, cpu_set_t *srcp);

#define zus_num_possible_nodes() (zus_numa_map->possible_nodes)
#define zus_num_possible_cpus()  (zus_numa_map->possible_cpus)
#define zus_num_online_nodes()   (zus_numa_map->online_nodes)
#define zus_num_online_cpus()    (zus_numa_map->online_cpus)

#define zus_for_each_cpu(cpu, mask)			\
	for ((cpu) = -1;				\
		(cpu) = zus_cpumask_next((cpu), (mask)),\
		(cpu) < zus_nr_cpu_ids;)

void *zus_private_get(void);
void zus_private_set(void*);
ulong zus_thread_self(void);

struct zus_thread_params {
	const char *name; /* only used for the duration of the call */
	int policy;
	int rr_priority;
	uint one_cpu;	/* either set this one. Else ZUS_CPU_ALL */
	uint nid;	/* Or set this one. Else ZUS_NUMA_NO_NID */
	ulong __flags; /* warnings on/off */
};

#define ZTP_INIT(ztp)				\
{						\
	memset((ztp), 0, sizeof(*(ztp)));	\
	(ztp)->nid = (ztp)->one_cpu = (-1);	\
}

typedef void *(*__start_routine) (void *); /* pthread programming style NOT */
int zus_thread_create(pthread_t *new_tread, struct zus_thread_params *params,
		      __start_routine fn, void *user_arg);
int zus_thread_current_init(void);
void zus_thread_current_fini(void);
int zus_alloc_exec_buff(struct zus_sb_info *sbi, uint max_bytes, uint pool_num,
			struct fba *fba);

/* zus-vfs.c */
int zus_register_all(int fd);
void zus_unregister_all(void);
int zus_register_one(int fd, struct zus_fs_info *p_zfi);

int zus_mount(int fd, struct zufs_ioc_mount *zim);
int zus_umount(int fd, struct zufs_ioc_mount *zim);
int zus_remount(int fd, struct zufs_ioc_mount *zim);
struct zus_inode_info *zus_iget(struct zus_sb_info *sbi, ulong ino);
int zus_do_command(void *app_ptr, struct zufs_ioc_hdr *hdr);
const char *ZUFS_OP_name(enum e_zufs_operation op);

int zus_private_mount(struct zus_fs_info *zfi, const char *options, ulong flags,
		      struct zufs_ioc_mount_private **zip_out);
int zus_private_umount(struct zufs_ioc_mount_private *zip);

/* dyn_pr.c */
int zus_add_module_ddbg(const char *fs_name, void *handle);
void zus_free_ddbg_db(void);
int zus_ddbg_read(struct zufs_ddbg_info *zdi);
int zus_ddbg_write(struct zufs_ddbg_info *zdi);

/* pa.c */

/* fba - File backed Allocator
 * Gives user an allocated pointer which is derived from a /tmp/O_TMPFILE mmap.
 * The size is round up to 4K alignment.
 */
int  fba_alloc(struct fba *fba, size_t size);
int  fba_alloc_align(struct fba *fba, size_t size, bool huge);
void fba_free(struct fba *fba);
int fba_punch_hole(struct fba *fba, ulong index, uint nump);

/* pa - Page Allocator
 * Gives users a page Allocator pa_pages are ref-counted last put
 * frees the page. (pa_free is just a pa_put_page
 */
int zus_setup_pa_size(size_t size);
int pa_init(struct zus_sb_info *sbi);
void pa_fini(struct zus_sb_info *sbi);

/* This must be the same as struct zus_page */
struct pa_page {
	unsigned long		flags;
	int			use_count;
	int			refcount;

	unsigned long		index;
	void			*owner;

	union {
		struct a_list_head	list;
		struct slab_info {
			int	slab_uc;
			int	slab_cpu;
		} sinfo;
	};

	unsigned long		private;
	void			*private2;
};

/* page-flags operations */
#ifdef __BITS_PER_LONG
#define ZUS_BITS_PER_LONG __BITS_PER_LONG
#else
#define ZUS_BITS_PER_LONG (ULONG_MAX == 0xFFFFFFFFUL ? 32 : 64)
#endif
#define ZUS_BIT_MASK(nr)	(1UL << ((nr) % ZUS_BITS_PER_LONG))
#define ZUS_BIT_WORD(nr)	((nr) / ZUS_BITS_PER_LONG)


static inline int _zus_test_bit(long nr, volatile unsigned long *addr)
{
	return (1UL & (addr[ZUS_BIT_WORD(nr)]) >>
			(nr & (ZUS_BITS_PER_LONG - 1)));
}

static inline void _zus_set_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = ZUS_BIT_MASK(nr);
	volatile unsigned long *p = addr + ZUS_BIT_WORD(nr);

	__atomic_or_fetch(p, mask, __ATOMIC_SEQ_CST);
}

static inline void _zus_clear_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = ZUS_BIT_MASK(nr);
	volatile unsigned long *p = addr + ZUS_BIT_WORD(nr);

	__atomic_and_fetch(p, ~mask, __ATOMIC_SEQ_CST);
}

static inline bool zus_test_pf(struct pa_page *p, long nr)
{
	return _zus_test_bit(nr, &p->flags);
}

static inline void zus_set_pf(struct pa_page *p, long nr)
{
	_zus_set_bit(nr, &p->flags);
}

static inline void zus_clear_pf(struct pa_page *p, long nr)
{
	_zus_clear_bit(nr, &p->flags);
}

#define PA_MAX_ORDER 5

struct pa_page *pa_alloc_order(struct zus_sb_info *sbi, int order);
static inline struct pa_page *pa_alloc(struct zus_sb_info *sbi)
{
	return pa_alloc_order(sbi, 0);
}

/* Must not be used by users, private to pa implementation */
void __pa_free(struct pa_page *page);

#define ZONE_BITLEN	4
#define ZONE_SHIFT	(64 - ZONE_BITLEN) /* last 4 bits */
#define ZONE_MASK	(((1UL << ZONE_BITLEN)-1) << ZONE_SHIFT)

#define NODES_BITLEN	4
#define NODES_PGSHIFT	(ZONE_SHIFT - NODES_BITLEN) /* 4 bits */
#define NODES_MASK	(((1UL << NODES_BITLEN)-1) << NODES_PGSHIFT)

static inline ulong get_bit_range(ulong x, int start_bit, ulong mask)
{
	return (x & mask) >> start_bit;
}

static inline void set_bit_range(ulong *x, int start_bit, ulong mask, ulong val)
{
	*x &= ~mask;
	*x |= (val << start_bit);
}

static inline void pa_set_page_zone(struct pa_page *page, int zone)
{
	ZUS_WARN_ON_ONCE(zone >> ZONE_BITLEN);
	set_bit_range(&page->flags, ZONE_SHIFT, ZONE_MASK, zone);
}

static inline int pa_page_zone(struct pa_page *page)
{
	return get_bit_range(page->flags, ZONE_SHIFT, ZONE_MASK);
}

static inline void pa_page_nid_set(struct pa_page *page, unsigned long node)
{
	ZUS_WARN_ON_ONCE(node >> NODES_BITLEN);
	set_bit_range(&page->flags, NODES_PGSHIFT, NODES_MASK, node);
}

static inline int pa_page_to_nid(struct pa_page *page)
{
	return get_bit_range(page->flags, NODES_PGSHIFT, NODES_MASK);
}

static inline int _zu_atomic_add_unless(int *v, int a, int u)
{
	int c, old;

	c = __atomic_load_n(v, __ATOMIC_SEQ_CST);
	old = c;
	while (c != u) {
		if (__atomic_compare_exchange_n(v, &old, c + a, false,
						__ATOMIC_SEQ_CST,
						__ATOMIC_SEQ_CST))
			break;

		c = old;
	}
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

static inline int pa_page_count(struct pa_page *page)
{
	return __atomic_load_n(&page->refcount, __ATOMIC_SEQ_CST);
}

static inline void pa_page_count_set(struct pa_page *page, int v)
{
	__atomic_store_n(&page->refcount, v, __ATOMIC_SEQ_CST);
}

static inline int pa_page_count_inc(struct pa_page *page)
{
	return __atomic_add_fetch(&page->refcount, 1, __ATOMIC_SEQ_CST);
}

static inline int pa_page_count_dec(struct pa_page *page)
{
	return __atomic_sub_fetch(&page->refcount, 1, __ATOMIC_SEQ_CST);
}

static inline int pa_page_use_count(struct pa_page *page)
{
	return __atomic_load_n(&page->use_count, __ATOMIC_SEQ_CST);
}

static inline void pa_page_use_count_set(struct pa_page *page, int v)
{
	__atomic_store_n(&page->use_count, v, __ATOMIC_SEQ_CST);
}

static inline int pa_page_use_count_inc(struct pa_page *page)
{
	return __atomic_add_fetch(&page->use_count, 1, __ATOMIC_SEQ_CST);
}

static inline int pa_page_use_count_dec(struct pa_page *page)
{
	return __atomic_sub_fetch(&page->use_count, 1, __ATOMIC_SEQ_CST);
}

/* Allow using 'page->use_count' as private meta-info when not in use */
static inline int _pa_page_meta(struct pa_page *page)
{
	return page->use_count;
}

static inline void _pa_page_meta_set(struct pa_page *page, int v)
{
	page->use_count = v;
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
#define ZUFS_PA_SIZE		"ZUFS_PA_SIZE"

/* declare so compiler will not complain */
extern int register_fs(int fd);
/* below two need to match. C is not bash */
#define REGISTER_FS_FN		 register_fs
#define REGISTER_FS_NAME	"register_fs"

enum zus_mlock_mode {
	MLOCK_NONE = 0,	/* do not call mlock */
	MLOCK_CURRENT,	/* mlockall(MCL_CURRENT) and mlock per alloc */
	MLOCK_ALL,	/* mlockall(MCL_CURRENT | MCL_FUTURE) */
};

extern int g_mlock;
#define NEED_MLOCK	(g_mlock == MLOCK_CURRENT)

/* ~~~ memory allocator ~~~ */
#define ZUS_ZERO 1

int zus_slab_init(void);
void zus_slab_fini(void);
void *zus_malloc(size_t size);
void *zus_calloc(size_t nmemb, size_t size);
void *zus_realloc(void *ptr, size_t size);
void zus_free(void *ptr);
struct pa_page *zus_alloc_page(int mask);
void zus_free_page(struct pa_page *);
void *zus_page_address(struct pa_page *page);
void *zus_virt_to_page(void *addr);
struct zus_sb_info *zus_global_sbi(void);

#endif /* define __ZUS_H__ */
