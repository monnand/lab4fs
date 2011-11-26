#include <linux/config.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/parser.h>
#include <linux/random.h>
#include <linux/buffer_head.h>
#include <linux/smp_lock.h>
#include <linux/vfs.h>
#include <asm/uaccess.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/version.h>
#include <linux/nls.h>
#include "lab4fs.h"

static struct lab4fs_inode *lab4fs_get_inode(struct super_block *sb,
        ino_t ino, struct buffer_head **p)
{
    struct buffer_head *bh;
    __u32 block, offset;

    *p = NULL;
    if ((ino != LAB4FS_ROOT_INO && ino < LAB4FS_FIRST_INO(sb)) ||
            ino > le32_to_cpu(LAB4FS_SB(sb)->s_sb->s_inodes_count))
        goto Einval;
    block = ino / LAB4FS_INODE_SIZE(sb);
    offset = ino % LAB4FS_INODE_SIZE(sb);
    block += LAB4FS_SB(sb)->s_inode_table;
	if (!(bh = sb_bread(sb, block)))
        goto Eio;
    *p = bh;
    offset = offset << LAB4FS_SB(sb)->s_log_inode_size;
	return (struct lab4fs_inode *) (bh->b_data + offset);

Einval:
    LAB4ERROR("lab4fs_get_inode: bad inode number: %lu\n",
            (unsigned long) ino);
    return ERR_PTR(-EINVAL);
Eio:
    LAB4ERROR("lab4fs_get_inode: "
		   "unable to read inode block - inode=%lu, block=%lu",
		   (unsigned long) ino, block);
Egdp:
	return ERR_PTR(-EIO);
}

void lab4fs_read_inode(struct inode *inode)
{
    struct lab4fs_inode_info *ei = LAB4FS_I(inode);
    ino_t ino = inode->i_ino;
    struct lab4fs_inode *raw_inode = lab4fs_get_inode(inode->i_sb, ino, &bh);

	if (IS_ERR(raw_inode))
 		goto bad_inode;
	inode->i_mode = le16_to_cpu(raw_inode->i_mode);
	inode->i_uid = (uid_t)le32_to_cpu(raw_inode->i_uid);
	inode->i_gid = (gid_t)le32_to_cpu(raw_inode->i_gid);
	inode->i_nlink = le16_to_cpu(raw_inode->i_links_count);
    inode->i_size = le32_to_cpu(raw_inode->i_size);
	inode->i_atime.tv_sec = le32_to_cpu(raw_inode->i_atime);
	inode->i_ctime.tv_sec = le32_to_cpu(raw_inode->i_ctime);
	inode->i_mtime.tv_sec = le32_to_cpu(raw_inode->i_mtime);
	inode->i_atime.tv_nsec = inode->i_mtime.tv_nsec = inode->i_ctime.tv_nsec = 0;
	ei->i_dtime = le32_to_cpu(raw_inode->i_dtime);

	if (inode->i_nlink == 0 && (inode->i_mode == 0 || ei->i_dtime)) {
		/* this inode is deleted */
		brelse (bh);
		goto bad_inode;
	}
	inode->i_blksize = PAGE_SIZE;	/* This is the optimal IO size (for stat), not the fs block size */
	inode->i_blocks = le32_to_cpu(raw_inode->i_blocks);
	ei->i_file_acl = le32_to_cpu(raw_inode->i_file_acl);
	ei->i_dir_acl = 0;

    if (!S_ISREG(inode->i_mode))
        ei->i_dir_acl = le32_to_cpu(raw_inode->i_dir_acl);

	/*
	 * NOTE! The in-memory inode i_data array is in little-endian order
	 * even on big-endian machines: we do NOT byteswap the block numbers!
	 */
	for (n = 0; n < EXT2_N_BLOCKS; n++)
		ei->i_data[n] = raw_inode->i_block[n];

    /*
     * TODO set operations
     */
}

