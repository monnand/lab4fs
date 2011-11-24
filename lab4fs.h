#ifndef __LAB4FS_H
#define __LAB4FS_H

#include <linux/fs.h>
#include <linux/ext2_fs.h>

#define LAB4FS_SUPER_MAGIC	0x1ab4f5 /* lab4fs */

#define LAB4FS_DEF_RESUID	0
#define LAB4FS_DEF_RESGID	0

#define lab4fs_max_size(x)	8192

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
	__le32 s_free_inode_count;
	__le32 s_free_data_blocks_count;
};

struct lab4fs_sb_info {
	struct lab4fs_super_block *s_sb;
	struct buffer_head *s_sbh;
	unsigned s_blocks_count;
	unsigned s_inodes_count;
};

#endif

