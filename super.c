
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

#define LAB4FS_MAGIC	0x1ab4f5 /* lab4fs */

#define LAB4ERROR(string, args...)	do {	\
	printk(KERN_WARNING "[lab4fs] " string, ##args);	\
} while (0)

#ifdef CONFIG_LAB4FS_DEBUG
#define LAB4DEBUG(string, args...)	do {	\
	printk(KERN_DEBUG "[lab4fs] "string, ##args);	\
} while(0)
#endif

#ifdef CONFIG_LAB4FS_DEBUG
static void print_super(struct lab4fs_super_block *sb)
{
}
#else
#define print_super(sb)
#endif

static int lab4fs_fill_super(struct super_block * sb, void * data, int silent)
{
	struct buffer_head * bh;
	int blocksize = BLOCK_SIZE;
	unsigned long logic_sb_block;
	unsigned offset = 0;
	unsigned long sb_block = 1;
	struct lab4fs_super_block *es;
	struct lab4fs_sb_info *sbi;
	struct inode *root;
	int hblock;

	sbi = kmalloc(sizeof(*sbi), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;

	sb->s_fs_info = sbi;
	memset(sbi, 0, sizeof(*sbi));
	blocksize = sb_min_blocksize(sb, BLOCK_SIZE);
	if (!blocksize) {
		LAB4ERROR("unable to set blocksize\n");
		goto out_fail;
	}

	/*
	 * If the superblock doesn't start on a hardware sector boundary,
	 * calculate the offset.  
	 */
	if (blocksize != BLOCK_SIZE) {
		logic_sb_block = (sb_block * BLOCK_SIZE) / blocksize;
		offset = (sb_block * BLOCK_SIZE) % blocksize;
	} else {
		logic_sb_block = sb_block;
	}

	if (!(bh = sb_bread(sb, logic_sb_block))) {
		LAB4ERROR("unable to read super block\n");
		goto out_fail;
	}

	es = (struct lab4fs_super_block *) (((char *)bh->b_data) + offset);
	sbi->s_sb = es;
	sb->s_magic = le32_to_cpu(es->s_magic);
	if (sb->s_magic != LAB4FS_SUPER_MAGIC) {
		if (!silent)
			LAB4ERROR("VFS: Can't find lab4fs filesystem on dev %s.\n",
					sb->s_id);
		goto failed_mount;
	}

	blocksize = le32_to_cpu(es->s_block_size);
	hblock = bdev_hardsect_size(sb->s_bdev);
	if (sb->s_blocksize != blocksize) {
		/*
		 * Make sure the blocksize for the filesystem is larger
		 * than the hardware sectorsize for the machine.
		 */
		if (blocksize < hblock) {
			LAB4ERROR("blocksize %d too small for "
			       "device blocksize %d.\n", blocksize, hblock);
			goto failed_mount;
		}

		brelse (bh);
		sb_set_blocksize(sb, blocksize);
		logic_sb_block = (sb_block * BLOCK_SIZE) / blocksize;
		offset = (sb_block * BLOCK_SIZE) % blocksize;
		bh = sb_bread(sb, logic_sb_block);
		if (!bh) {
			LAB4ERROR("Can't read superblock on 2nd try.\n");
			goto failed_mount;
		}
		es = (struct lab4fs_super_block *)(((char *)bh->b_data) + offset);
		sbi->s_sb = es;
		if (es->s_magic != cpu_to_le32(LAB4FS_SUPER_MAGIC)) {
			LAB4ERROR("Magic mismatch, very weird !\n");
			goto failed_mount;
		}
	}
	sb->s_maxbytes = lab4fs_max_size(es);
	sbi->s_sbh = bh;

failed_mount:
out_fail:
	kfree(sbi);
	return 0;
}

struct super_block * lab4fs_get_sb(struct file_system_type *fs_type,
        int flags, const char *dev_name, void *data)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, lab4fs_fill_super);
}

static struct file_system_type lab4fs_fs_type = {
	.owner = THIS_MODULE,
	.name = "lab4fs",
	.get_sb = lab4fs_get_sb,
	.kill_sb = kill_block_super,
	/*  .fs_flags */
};


static int __init init_lab4fs_fs(void)
{
	return register_filesystem(&lab4fs_fs_type);
}

static void __exit exit_lab4fs_fs(void)
{
	unregister_filesystem(&lab4fs_fs_type);
}

module_init(init_lab4fs_fs)
module_exit(exit_lab4fs_fs)

MODULE_LICENSE("GPL");
