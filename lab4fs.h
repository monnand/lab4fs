#ifndef __LAB4FS_H
#define __LAB4FS_H

#include <linux/config.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/ext2_fs.h>
#include <linux/spinlock.h>

/*      
 * Ext2 directory file types.  Only the low 3 bits are used.  The
 * other bits are reserved for now.
 */     
#define LAB4FS_FT_UNKNOWN     0
#define LAB4FS_FT_REG_FILE    1
#define LAB4FS_FT_DIR     2
#define LAB4FS_FT_CHRDEV      3
#define LAB4FS_FT_BLKDEV      4
#define LAB4FS_FT_FIFO        5
#define LAB4FS_FT_SOCK        6
#define LAB4FS_FT_SYMLINK     7

#define LAB4FS_FT_MAX     8


/*
 * EXT2_DIR_PAD defines the directory entries boundaries
 *
 * NOTE: It must be a multiple of 4
 */
#define EXT2_DIR_PAD            4
#define EXT2_DIR_ROUND          (EXT2_DIR_PAD - 1)
#define LAB4FS_DIR_REC_LEN(name_len)  (((name_len) + 8 + EXT2_DIR_ROUND) & \
                     ~EXT2_DIR_ROUND)

#define LAB4BAD_INO         0
#define LAB4FS_ROOT_INO     1

#define LAB4FS_DEF_RESUID	0
#define LAB4FS_DEF_RESGID	0

#define lab4fs_max_size(x)	8192

#define LAB4FS_NDIR_BLOCKS  7
#define LAB4FS_IND_BLOCKS   8
#define LAB4FS_N_BLOCKS     8

#define LAB4FS_BLOCK_SIZE(s)		((s)->s_blocksize)

#define	LAB4FS_ADDR_PER_BLOCK(s)		(LAB4FS_BLOCK_SIZE(s) / sizeof (__u32))
#define LAB4FS_ADDR_PER_BLOCK_BITS(s)   4

#define LAB4FS_SUPER_MAGIC	0x1ab4f5 /* lab4fs */

#define LAB4ERROR(string, args...)	do {	\
	printk(KERN_WARNING "[lab4fs] " string, ##args);	\
} while (0)

#ifdef CONFIG_LAB4FS_DEBUG
#define LAB4DEBUG(string, args...)	do {	\
	printk(KERN_DEBUG "[lab4fs][%s:%d] "string, \
            __FILE__, __LINE__, ##args);	\
} while(0)
#endif

#define LAB4FS_FIRST_INO(s)   (LAB4FS_SB(s)->s_first_ino)
#define LAB4FS_INODE_SIZE(s)   (LAB4FS_SB(s)->s_inode_size)

struct lab4fs_super_block {
	__le32 s_magic;

	__le32 s_blocks_count;
	__le32 s_block_size;

	__le32 s_inodes_count;
	__le32 s_inode_size;

	__le32 s_first_block;

	__le32 s_inode_bitmap;
	__le32 s_data_bitmap;

	__le32 s_inode_table;
	__le32 s_data_blocks;

	__le32 s_root_inode;
    __le32 s_first_inode;

	__le32 s_free_inode_count;
	__le32 s_free_data_blocks_count;
};

struct lab4fs_inode {
	__le16	i_mode;		/* File mode */
	__le16	i_links_count;	/* Links count */
	__le32	i_size;		/* Size in bytes */
	__le32	i_atime;	/* Access time */
	__le32	i_ctime;	/* Creation time */
	__le32	i_mtime;	/* Modification time */
	__le32	i_dtime;	/* Deletion Time */
	__le32  i_gid;		/* Low 16 bits of Group Id */
	__le32  i_uid;		/* Low 16 bits of Owner Uid */
	__le32	i_blocks;	/* Blocks count */
	__le32	i_block[LAB4FS_N_BLOCKS];/* Pointers to blocks */
	__le32	i_file_acl;	/* File ACL */
	__le32	i_dir_acl;	/* Directory ACL */
};

struct lab4fs_bitmap {
    int nr_bhs;
    rwlock_t rwlock;
    int nr_valid_bits;
    __u32 log_nr_bits_per_block;
    __u32 nr_bits_per_block;
    struct buffer_head **bhs;
};

struct lab4fs_sb_info {
	struct lab4fs_super_block *s_sb;
	struct buffer_head *s_sbh;
	unsigned s_blocks_count;
	unsigned s_inodes_count;
    unsigned s_log_block_size;
    unsigned s_log_inode_size;
    unsigned s_first_ino;
    unsigned s_inode_size;
    __u32 s_root_inode;
	__u32 s_inode_table;
	__u32 s_data_blocks;

    struct lab4fs_bitmap s_inode_bitmap;
    struct lab4fs_bitmap s_data_bitmap;
};

struct lab4fs_inode_info {
	__u16	i_mode;		/* File mode */
	__u16	i_links_count;	/* Links count */
	__u32	i_size;		/* Size in bytes */
	__u32	i_atime;	/* Access time */
	__u32	i_ctime;	/* Creation time */
	__u32	i_mtime;	/* Modification time */
	__u32	i_dtime;	/* Deletion Time */
	__u32  	i_gid;		/* Low 16 bits of Group Id */
	__u32	i_uid;		/* Low 16 bits of Owner Uid */
	__u32	i_blocks;	/* Blocks count */
	__le32	i_block[LAB4FS_N_BLOCKS];/* Pointers to blocks */
	__u32	i_file_acl;	/* File ACL */
	__u32	i_dir_acl;	/* Directory ACL */
    rwlock_t rwlock;
    struct inode vfs_inode;
    struct buffer_head *bh;
};

#define LAB4FS_NAME_LEN     255

struct lab4fs_dir_entry {
	__le32	inode;			/* Inode number */
	__le16	rec_len;		/* Directory entry length */
	__u8    name_len;		/* Name length */
    __u8    file_type;
	char	name[LAB4FS_NAME_LEN];	/* File name */
};

static inline struct lab4fs_sb_info *LAB4FS_SB(struct super_block *sb)
{
    return sb->s_fs_info;
}

static inline struct lab4fs_inode_info *LAB4FS_I(struct inode *inode)
{
    return container_of(inode, struct lab4fs_inode_info, vfs_inode);
}

extern struct address_space_operations lab4fs_aops;
extern struct file_operations lab4fs_dir_operations;

void lab4fs_read_inode(struct inode *inode);
int bitmap_setup(struct lab4fs_bitmap *bitmap, struct super_block *sb,
        __u32 start_block);
void bitmap_set_bit(struct lab4fs_bitmap *bitmap, int nr);
int bitmap_test_and_set_bit(struct lab4fs_bitmap *bitmap, int nr);
void bitmap_clear_bit(struct lab4fs_bitmap *bitmap, int nr);
int bitmap_test_and_clear_bit(struct lab4fs_bitmap *bitmap, int nr);
int bitmap_test_bit(struct lab4fs_bitmap *bitmap, int nr);

#endif

