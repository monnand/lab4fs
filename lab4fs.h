#ifndef __LAB4FS_H
#define __LAB4FS_H

#include <linux/fs.h>
#include <linux/ext2_fs.h>

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

#define LAB4FS_SUPER_MAGIC	0x1ab4f5 /* lab4fs */
#define LAB4BAD_INO         0
#define LAB4FS_ROOT_INO     1

#define LAB4FS_DEF_RESUID	0
#define LAB4FS_DEF_RESGID	0

#define lab4fs_max_size(x)	8192

#define LAB4FS_NDIR_BLOCKS  7
#define LAB4FS_IND_BLOCKS   8
#define LAB4FS_N_BLOCKS     8

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
	__le16	i_uid;		/* Low 16 bits of Owner Uid */
	__le32	i_size;		/* Size in bytes */
	__le32	i_atime;	/* Access time */
	__le32	i_ctime;	/* Creation time */
	__le32	i_mtime;	/* Modification time */
	__le32	i_dtime;	/* Deletion Time */
	__le16	i_gid;		/* Low 16 bits of Group Id */
	__le16	i_links_count;	/* Links count */
	__le32	i_blocks;	/* Blocks count */
	__le32	i_block[LAB4FS_N_BLOCKS];/* Pointers to blocks */
};

struct lab4fs_sb_info {
	struct lab4fs_super_block *s_sb;
	struct buffer_head *s_sbh;
	unsigned s_blocks_count;
	unsigned s_inodes_count;
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

#endif

