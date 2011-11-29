#include "lab4fs.h"

#define log2(n) ffz(~(n))

#ifdef CONFIG_LAB4FS_DEBUG
static void print_super(struct lab4fs_super_block *sb)
{
    LAB4DEBUG("Number of blocks: %lu\n", (unsigned long)le32_to_cpu(sb->s_blocks_count));
    LAB4DEBUG("Block size: %lu\n", (unsigned long)le32_to_cpu(sb->s_block_size));
    LAB4DEBUG("Number of inodes: %lu\n", (unsigned long)le32_to_cpu(sb->s_inodes_count));
    LAB4DEBUG("inode size: %lu\n", (unsigned long)le32_to_cpu(sb->s_inode_size));
}
#else
#define print_super(sb)
#endif

static void lab4fs_put_super(struct super_block *sb)
{
    struct lab4fs_sb_info *sbi;
    sbi = LAB4FS_SB(sb);
    if (sbi == NULL)
        return;
    kfree(sbi);
    return;
}

static kmem_cache_t *lab4fs_inode_cachep;

static struct inode *lab4fs_alloc_inode(struct super_block *sb)
{
    struct lab4fs_inode_info *ei;
	ei = (struct lab4fs_inode_info *)kmem_cache_alloc(lab4fs_inode_cachep, SLAB_KERNEL);
	if (!ei)
		return NULL;
    ei->vfs_inode.i_sb = sb;
    ei->i_dir_start_lookup = 0;
    rwlock_init(&ei->rwlock);
	return &ei->vfs_inode;
}

static void lab4fs_destroy_inode(struct inode *inode)
{
	kmem_cache_free(lab4fs_inode_cachep, LAB4FS_I(inode));
}

void lab4fs_write_super (struct super_block * sb)
{
    struct lab4fs_super_block *es;
    lock_kernel();
    es = LAB4FS_SB(sb)->s_sb;
    mark_buffer_dirty(LAB4FS_SB(sb)->s_sbh);
    sb->s_dirt = 0;
    unlock_kernel();
}

static 
int lab4fs_statfs(struct super_block *sb, struct kstatfs *buf)
{
    struct lab4fs_sb_info *sbi = LAB4FS_SB(sb);
    buf->f_type = sb->s_magic;
	buf->f_bsize = 1024;
	buf->f_namelen = 255;
    buf->f_bfree = sbi->s_free_data_blocks_count;
    return 0;
}

struct super_operations lab4fs_super_ops = {
    .alloc_inode    = lab4fs_alloc_inode,
    .destroy_inode  = lab4fs_destroy_inode,
    .read_inode     = lab4fs_read_inode,
    .write_inode    = lab4fs_write_inode,
    .statfs         = lab4fs_statfs,
    .put_super      = lab4fs_put_super,
    .write_super    = lab4fs_write_super,
};

/*
struct inode *lab4fs_get_inode(struct super_block *sb, int mode, dev_t dev)
{
    struct inode *inode;

    inode = new_inode(sb);

    if (inode) {
        inode->i_mode = mode;
        inode->i_uid = current->fsuid;
        inode->i_gid = current->fsgid;
        inode->i_blocks = 0;
        inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
        inode->i_sb = sb;

        LAB4DEBUG("inode mode: %u, is dir: %d\n", inode->i_mode, S_ISDIR(inode->i_mode));

        switch (mode & S_IFMT) {
        case S_IFDIR:
            inode->i_op = &simple_dir_inode_operations;
            inode->i_fop = &lab4fs_file_ops;
            inode->i_op = &lab4fs_inode_ops;
            inode->i_nlink++;
            break;
        case S_IFREG:
        default:
            init_special_inode(inode, mode, dev);
            break;
        }
    }
    return inode;
}
*/

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
    int err = 0;

    sbi = kmalloc(sizeof(*sbi), GFP_KERNEL);
    if (!sbi)
        return -ENOMEM;

    sb->s_fs_info = sbi;
    memset(sbi, 0, sizeof(*sbi));
    blocksize = sb_min_blocksize(sb, BLOCK_SIZE);
    if (!blocksize) {
        LAB4ERROR("unable to set blocksize\n");
        err = -EIO;
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
    sbi->s_log_block_size = log2(sb->s_blocksize);
    sbi->s_first_ino = le32_to_cpu(es->s_first_inode);
    sbi->s_inode_size = le32_to_cpu(es->s_inode_size);
    sbi->s_log_inode_size = log2(sbi->s_inode_size);
    sbi->s_inode_table = le32_to_cpu(es->s_inode_table);
    sbi->s_data_blocks = le32_to_cpu(es->s_data_blocks);
    sbi->s_next_generation = 0;
    sbi->s_free_inodes_count = le32_to_cpu(es->s_free_inodes_count);
    sbi->s_free_data_blocks_count = le32_to_cpu(es->s_free_data_blocks_count);
    sbi->s_inodes_count = le32_to_cpu(es->s_inodes_count);
    sbi->s_blocks_count = le32_to_cpu(es->s_blocks_count);

    sbi->s_inode_bitmap.nr_valid_bits = le32_to_cpu(es->s_inodes_count);
    sbi->s_data_bitmap.nr_valid_bits = le32_to_cpu(es->s_blocks_count) 
        - le32_to_cpu(es->s_data_blocks);

    sb->s_op = &lab4fs_super_ops;

    err = bitmap_setup(&sbi->s_inode_bitmap, sb, le32_to_cpu(es->s_inode_bitmap));
    if (err)
        goto out_fail;
    err = bitmap_setup(&sbi->s_data_bitmap, sb, le32_to_cpu(es->s_data_bitmap));
    if (err)
        goto out_fail;

    rwlock_init(&sbi->rwlock);
    print_super(es);
    sbi->s_root_inode = le32_to_cpu(es->s_root_inode);
    LAB4DEBUG("Now loading root dir\n");
    root = iget(sb, sbi->s_root_inode);
    if (root == NULL) {
        err = -EIO;
        goto failed_mount;
    }
    sb->s_root = d_alloc_root(root);
    if (!sb->s_root) {
        iput(root);
        kfree(sbi);
        return -ENOMEM;
    }

    return 0;

failed_mount:
out_fail:
	kfree(sbi);
	return err;
}

struct super_block *lab4fs_get_sb(struct file_system_type *fs_type,
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

static void init_once(void * foo, kmem_cache_t * cachep, unsigned long flags)
{
    struct lab4fs_inode_info *ei = (struct lab4fs_inode_info *) foo;
    inode_init_once(&ei->vfs_inode);
}

static int init_inodecache(void)
{
	lab4fs_inode_cachep = kmem_cache_create("lab4fs_inode_cache",
					     sizeof(struct lab4fs_inode_info),
					     0, SLAB_RECLAIM_ACCOUNT,
					     init_once, NULL);
	if (lab4fs_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static int __init init_lab4fs_fs(void)
{
    int err;
    char buf[4];
    memset(buf, 0, sizeof(buf));
    err = find_next_zero_bit(&buf, 4, 0);
    LAB4DEBUG("First 0 bit is %d\n", err);
    buf[0] = 0x08;
    err = find_next_zero_bit(&buf, 4, 0);
    LAB4DEBUG("First 0 bit is %d\n", err);
    buf[0] = 0;
    set_bit(&buf, 1);
    LAB4DEBUG("it should be %x %x %x %x\n", buf[0], buf[1], buf[2], buf[3]);
    err = init_inodecache();
    if (err)
        return err;
	return register_filesystem(&lab4fs_fs_type);
}

static void destroy_inodecache(void)
{
	if (kmem_cache_destroy(lab4fs_inode_cachep))
		printk(KERN_INFO "lab4fs_inode_cache: not all structures were freed\n");
}

static void __exit exit_lab4fs_fs(void)
{
	unregister_filesystem(&lab4fs_fs_type);
	destroy_inodecache();
}

module_init(init_lab4fs_fs)
module_exit(exit_lab4fs_fs)

MODULE_LICENSE("GPL");
