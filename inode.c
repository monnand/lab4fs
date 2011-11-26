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
}

void lab4fs_read_inode(struct inode *inode)
{
    struct lab4fs_inode_info *ei = LAB4FS_I(inode);
    ino_t ino = inode->i_ino;
    struct lab4fs_inode *raw_inode = lab4fs_get_inode(inode->i_sb, ino, &bh);
}

