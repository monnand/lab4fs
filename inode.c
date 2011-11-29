#include "lab4fs.h"
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/mpage.h>

typedef struct {
	__le32	*p;
	__le32	key;
	struct buffer_head *bh;
} Indirect;

static inline void add_chain(Indirect *p, struct buffer_head *bh, __le32 *v)
{
	p->key = *(p->p = v);
	p->bh = bh;
}

static inline int verify_chain(Indirect *from, Indirect *to)
{
	while (from <= to && from->key == *from->p)
		from++;
	return (from > to);
}

#ifdef CONFIG_LAB4FS_DEBUG
void print_buffer_head(struct buffer_head *bh, int start, int len)
{
    __u8 *data;
    int i;

    data = (__u8 *)(bh->b_data + start);
    LAB4DEBUG("Printing buffer head@%dB, size=%d: \n" KERN_INFO,
            start, len);
    for (i = 0; i < len; i++) {
        if (i % 32 == 0)
            printk("\n" KERN_INFO);
        printk("%02x ", data[i]);
    }
    printk("\n");
}
#else
#define print_buffer_head(bh, start, len)
#endif

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
    offset = offset << (LAB4FS_SB(sb)->s_log_inode_size);
	return (struct lab4fs_inode *) (bh->b_data + offset);

Einval:
    LAB4ERROR("lab4fs_get_inode: bad inode number: %lu\n",
            (unsigned long) ino);
    return ERR_PTR(-EINVAL);
Eio:
    LAB4ERROR("lab4fs_get_inode: "
		   "unable to read inode block - inode=%lu, block=%u\n",
		   (unsigned long) ino, block);
	return ERR_PTR(-EIO);
}

#ifdef CONFIG_LAB4FS_DEBUG
void print_raw_inode(struct lab4fs_inode *raw_inode)
{
    int i = 0;
    LAB4DEBUG("mode: %X\n", le32_to_cpu(raw_inode->i_mode));
    LAB4DEBUG("nlink: %u\n", le32_to_cpu(raw_inode->i_links_count));
    LAB4DEBUG("data blocks: ");
    for (i = 0; i < LAB4FS_N_BLOCKS; i++) {
        printk("%u ", le32_to_cpu(raw_inode->i_block[i]));
    } 
    printk("\n");
}

void print_inode(struct inode *inode)
{
    struct lab4fs_inode_info *ei = LAB4FS_I(inode);
    int i = 0;
    LAB4DEBUG("mode: %X\n", inode->i_mode);
    LAB4DEBUG("nlink: %u\n", inode->i_nlink);
    LAB4DEBUG("size: %lu\n", (unsigned long)inode->i_size);
    LAB4DEBUG("blocks: %lu\n", (unsigned long)inode->i_blocks);
    LAB4DEBUG("data blocks: ");
    for (i = 0; i < LAB4FS_N_BLOCKS; i++) {
        printk("%u ", le32_to_cpu(ei->i_block[i]));
    } 
    printk("\n");
}
#endif

void lab4fs_read_inode(struct inode *inode)
{
    struct lab4fs_inode_info *ei = LAB4FS_I(inode);
    ino_t ino = inode->i_ino;
    int n;
	struct buffer_head * bh;
    struct lab4fs_inode *raw_inode = lab4fs_get_inode(inode->i_sb, ino, &bh);

	if (IS_ERR(raw_inode))
 		goto bad_inode;

    write_lock(&ei->rwlock);
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
    /* This is the optimal IO size (for stat), not the fs block size */
	inode->i_blksize = PAGE_SIZE;
    inode->i_blkbits = LAB4FS_SB(inode->i_sb)->s_log_block_size;
	inode->i_blocks = le32_to_cpu(raw_inode->i_blocks);
	ei->i_file_acl = le32_to_cpu(raw_inode->i_file_acl);
	ei->i_dir_acl = 0;

    if (!S_ISREG(inode->i_mode))
        ei->i_dir_acl = le32_to_cpu(raw_inode->i_dir_acl);

	/*
	 * NOTE! The in-memory inode i_block array is in little-endian order
	 * even on big-endian machines: we do NOT byteswap the block numbers!
	 */
	for (n = 0; n < LAB4FS_N_BLOCKS; n++)
		ei->i_block[n] = raw_inode->i_block[n];

    /*
     * TODO set operations
     */

    if (S_ISREG(inode->i_mode)) {
		inode->i_op = &lab4fs_file_inode_operations;
		inode->i_fop = &lab4fs_file_operations;
        inode->i_mapping->a_ops = &lab4fs_aops;
    } else if (S_ISDIR(inode->i_mode)) {
        inode->i_op = &lab4fs_dir_inode_operations;
        inode->i_fop = &lab4fs_dir_operations;
        inode->i_mapping->a_ops = &lab4fs_aops;
    } else {
        LAB4DEBUG("Not implemented\n");
    }
	brelse (bh);
    write_unlock(&ei->rwlock);
    return;
bad_inode:
    LAB4DEBUG("A bad inode!\n");
    make_bad_inode(inode);
    write_unlock(&ei->rwlock);
    return;
}

static int lab4fs_block_to_path(struct inode *inode,
			long i_block, int offsets[4], int *boundary)
{
    int ptrs = LAB4FS_ADDR_PER_BLOCK(inode->i_sb);
    const long direct_blocks = LAB4FS_NDIR_BLOCKS;
    const long indirect_blocks = ptrs;
    /*
    struct lab4fs_sb_info *sbi = LAB4FS_SB(inode->i_sb);
    */
    int final = 0;
    int n = 0;

    if (i_block < 0) {
        LAB4ERROR("block %d < 0\n", (int)i_block);
        return 0;
    } else if (i_block < direct_blocks) {
        offsets[n++] = i_block;
        final = direct_blocks;
    } else if ( (i_block -= direct_blocks) < indirect_blocks) {
		offsets[n++] = LAB4FS_IND_BLOCK;
        offsets[n++] = i_block;
        final = ptrs;
    } else {
        LAB4ERROR("block > big\n");
        return 0;
    }
	if (boundary)
		*boundary = (i_block & (ptrs - 1)) == (final - 1);
    return n;
}

static Indirect *lab4fs_get_branch(struct inode *inode,
				 int depth,
				 int *offsets,
				 Indirect chain[4],
				 long *err)
{
	struct super_block *sb = inode->i_sb;
	Indirect *p = chain;
	struct buffer_head *bh;

	*err = 0;

    /* First layer index; NULL for bh member */
	add_chain (chain, NULL, LAB4FS_I(inode)->i_block + *offsets);
    if (!p->key)
        goto no_block;
 	while (--depth) {
		bh = sb_bread(sb, le32_to_cpu(p->key));
		if (!bh)
			goto failure;
		read_lock(&LAB4FS_I(inode)->rwlock);
		if (!verify_chain(chain, p))
			goto changed;
		add_chain(++p, bh, (__le32*)bh->b_data + *++offsets);
		read_unlock(&LAB4FS_I(inode)->rwlock);
		if (!p->key)
			goto no_block;
	}
	return NULL;
   
changed:
    read_unlock(&LAB4FS_I(inode)->rwlock);
	brelse(bh);
	*err = -EAGAIN;
	goto no_block;

failure:
	*err = -EIO;
no_block:
	return p;
}

static __u32 lab4fs_alloc_data_block(struct inode *inode, __u32 perfered, long *err)
{
	struct super_block *sb = inode->i_sb;
    struct lab4fs_sb_info *sbi = LAB4FS_SB(sb);
    __u32 start;
    __u32 found;

    start = perfered - sbi->s_data_blocks;
    found = bitmap_find_next_zero_bit(&sbi->s_data_bitmap, start, 1);

    if (found > sbi->s_data_bitmap.nr_valid_bits) {
        found = bitmap_find_next_zero_bit(&sbi->s_data_bitmap, 0, 1);
        if (found > sbi->s_data_bitmap.nr_valid_bits)
            goto no_space;
        goto found_one_free;
    }
found_one_free:
    found += sbi->s_data_blocks;
    if (found >= sbi->s_blocks_count)
        goto io_err;
    write_lock(&sbi->rwlock);
    sbi->s_free_data_blocks_count--;
    write_unlock(&sbi->rwlock);

    return found;
io_err:
    *err = -EIO;
    return 0;
no_space:
    *err = -ENOSPC;
    return 0;
}

static Indirect *lab4fs_alloc_branch(struct inode *inode, int depth,
        int *offsets, Indirect *chain, Indirect *partial, long *err)
{
    Indirect *end = chain + depth;
    Indirect *p = partial;
    int n = partial - chain;
    __u32 block;
    struct buffer_head *bh;
	struct super_block *sb = inode->i_sb;
    struct lab4fs_inode_info *ei = LAB4FS_I(inode);
    struct lab4fs_sb_info *sbi = LAB4FS_SB(sb);


    block = lab4fs_alloc_data_block(inode, 0, err);
    if (*err)
        return p;
    write_lock(&LAB4FS_I(inode)->rwlock);
    p->key = cpu_to_le32(block);
    *(p->p) = p->key;
    if (p->bh == NULL)
        mark_inode_dirty(inode);
    else
        mark_buffer_dirty(p->bh);
    ei->i_blocks++;
    write_unlock(&LAB4FS_I(inode)->rwlock);

    write_lock(&sbi->rwlock);
    sbi->s_free_data_blocks_count--;
	sb->s_dirt = 1;
    write_unlock(&sbi->rwlock);
    n++;
    p++;

#ifdef CONFIG_LAB4FS_DEBUG
    if (depth > 1) {
        LAB4DEBUG("Indirect block alloc; we use block %u to store addresses\n",
                block);
        LAB4DEBUG("p->p = 0x%X; &i_data[7] = 0x%X\n",
                (unsigned )((p-1)->p), (unsigned)(&ei->i_block[7]));
        LAB4DEBUG("This block should be added into the end of i_block field:\n");
        print_inode(inode);
        LAB4DEBUG("Does it work?\n");
    }
#endif
    
    while (p < end) {
        bh = sb_bread(sb, block);

        if (!bh) {
            *err = -EIO;
            return p;
        }

		read_lock(&LAB4FS_I(inode)->rwlock);
		add_chain(p, bh, (__le32*)bh->b_data + offsets[n]);
		read_unlock(&LAB4FS_I(inode)->rwlock);

        block = lab4fs_alloc_data_block(inode, 0, err);
        if (*err)
            return p;

		write_lock(&LAB4FS_I(inode)->rwlock);
        p->key = cpu_to_le32(block);
        *(p->p) = p->key;
        mark_buffer_dirty(p->bh);
        ei->i_blocks++;
		write_unlock(&LAB4FS_I(inode)->rwlock);

        write_lock(&sbi->rwlock);
        sbi->s_free_data_blocks_count--;
        sb->s_dirt = 1;
        write_unlock(&sbi->rwlock);

        p++;
        n++;
#ifdef CONFIG_LAB4FS_DEBUG
        if (depth > 1) {
            LAB4DEBUG("Indirect block; we use block %u to store data\n",
                    block);
            LAB4DEBUG("Now let's see the inode again\n");
            print_inode(inode);
            LAB4DEBUG("Anything wrong?\n");
        }
#endif
    }

    *err = 0;
    return p;
}

#ifdef CONFIG_LAB4FS_DEBUG
static void print_block_path(struct inode *inode, sector_t iblock, 
        int *offsets, int depth)
{
    int i;
    LAB4DEBUG("It is block %lu for inode %lu\n", iblock, inode->i_ino);
    for (i = 0; i < depth; i++) {
        LAB4DEBUG("The %dth offset is %d\n", i, offsets[i]);
    }
}
#else
#define print_block_path(inode, iblock, offsets, depth)
#endif

static int lab4fs_get_block(struct inode *inode, sector_t iblock,
        struct buffer_head *bh_result, int create)
{
	long err = -EIO;
	int offsets[4];
	Indirect chain[4];
	Indirect *partial;
    int boundary = 0;
    int depth = lab4fs_block_to_path(inode, iblock, offsets, &boundary);

    if (depth == 0)
        goto out;

#ifdef CONFIG_LAB4FS_DEBUG
    if (depth > 1) {
        LAB4DEBUG("We have to deal with indirect block.\n");
        print_block_path(inode, iblock, offsets, depth);
    }
#endif
reread:
	partial = lab4fs_get_branch(inode, depth, offsets, chain, &err);

	/* Simplest case - block found, no allocation needed */
	if (!partial) {
got_it:
#ifdef CONFIG_LAB4FS_DEBUG
        if (depth > 1) {
            LAB4DEBUG("We get the block %lu for inode %lu, it's %u on disk.\n",
                    iblock, inode->i_ino,
                    le32_to_cpu(chain[depth-1].key));
            print_inode(inode);
        }
#endif
		map_bh(bh_result, inode->i_sb, le32_to_cpu(chain[depth-1].key));
		if (boundary)
			set_buffer_boundary(bh_result);
		/* Clean up and exit */
		partial = chain+depth-1; /* the whole chain */
		goto cleanup;
	}
	/* Next simple case - plain lookup or failed read of indirect block */
	if (!create || err == -EIO) {
cleanup:
		while (partial > chain) {
			brelse(partial->bh);
			partial--;
		}
out:
#ifdef CONFIG_LAB4FS_DEBUG
        if (depth > 1) {
            LAB4DEBUG("Safely returned\n");
        }
#endif
        return err;
    }

	/*
	 * Indirect block might be removed by truncate while we were
	 * reading it. Handling of that case (forget what we've got and
	 * reread) is taken out of the main path.
	 */
	if (err == -EAGAIN)
		goto changed;

    partial = lab4fs_alloc_branch(inode, depth, offsets, chain, partial, &err);
    if (err)
        return err;
    goto got_it;

changed:
	while (partial > chain) {
		brelse(partial->bh);
		partial--;
	}
	goto reread;
}

static int lab4fs_update_inode(struct inode *inode, int do_sync)
{
    struct lab4fs_inode_info *ei = LAB4FS_I(inode);
    struct super_block *sb = inode->i_sb;
    ino_t ino = inode->i_ino;
    uid_t uid = inode->i_uid;
    gid_t gid = inode->i_gid;

    struct buffer_head *bh;

    struct lab4fs_inode *raw_inode = lab4fs_get_inode(sb, ino, &bh);

    int n;
    int err = 0;

    if (IS_ERR(raw_inode))
        return -EIO;

    LAB4DEBUG("update inode: %lu\n", inode->i_ino);
    print_inode(inode);
    raw_inode->i_mode = cpu_to_le16(inode->i_mode);
    raw_inode->i_uid = cpu_to_le32(uid);
    raw_inode->i_gid = cpu_to_le32(gid);
    raw_inode->i_links_count = cpu_to_le16(inode->i_nlink);
    raw_inode->i_size = cpu_to_le32(inode->i_size);
	raw_inode->i_atime = cpu_to_le32(inode->i_atime.tv_sec);
	raw_inode->i_ctime = cpu_to_le32(inode->i_ctime.tv_sec);
	raw_inode->i_mtime = cpu_to_le32(inode->i_mtime.tv_sec);
	raw_inode->i_blocks = cpu_to_le32(inode->i_blocks);

	raw_inode->i_dtime = cpu_to_le32(ei->i_dtime);
	raw_inode->i_file_acl = cpu_to_le32(ei->i_file_acl);
	if (!S_ISREG(inode->i_mode))
		raw_inode->i_dir_acl = cpu_to_le32(ei->i_dir_acl);
	for (n = 0; n < LAB4FS_N_BLOCKS; n++)
		raw_inode->i_block[n] = ei->i_block[n];
	mark_buffer_dirty(bh);
	if (do_sync) {
		sync_dirty_buffer(bh);
		if (buffer_req(bh) && !buffer_uptodate(bh)) {
			printk ("IO error syncing lab4fs inode [%s:%08lx]\n",
				sb->s_id, (unsigned long) ino);
			err = -EIO;
		}
	}
	brelse (bh);
	return err;
}

int lab4fs_write_inode(struct inode *inode, int wait)
{
    return lab4fs_update_inode(inode, wait);
}

int lab4fs_sync_inode(struct inode *inode)
{
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_ALL,
		.nr_to_write = 0,	/* sys_fsync did this */
	};
	return sync_inode(inode, &wbc);
}

/* Create a new inode under dir */
struct inode *lab4fs_new_inode(struct inode *dir, int mode)
{
    struct super_block *sb;
    struct lab4fs_inode_info *ei;
    struct lab4fs_sb_info *sbi;
    struct inode *inode;
    int err = 0;
	ino_t ino = 0;

    sb = dir->i_sb;
	inode = new_inode(sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);

    ei = LAB4FS_I(inode);
    sbi = LAB4FS_SB(sb);

    read_lock(&sbi->rwlock);
    if (sbi->s_free_inodes_count == 0) {
        read_unlock(&sbi->rwlock);
        err = -ENOSPC;
        goto fail;
    }
    read_unlock(&sbi->rwlock);

    ino = bitmap_find_next_zero_bit(&sbi->s_inode_bitmap, sbi->s_first_ino, 1);

    if (ino >= sbi->s_inodes_count || ino < sbi->s_first_ino) {
        err = -ENOSPC;
        goto fail;
    }

    write_lock(&sbi->rwlock);
    sbi->s_free_inodes_count--;
	sb->s_dirt = 1;
	inode->i_generation = sbi->s_next_generation++;
    write_unlock(&sbi->rwlock);

	inode->i_ino = ino;
	inode->i_mode = mode;
    inode->i_uid = current->fsuid;
    inode->i_gid = current->fsgid;

	inode->i_blksize = PAGE_SIZE;	/* This is the optimal IO size (for stat), not the fs block size */
	inode->i_blocks = 0;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	memset(ei->i_block, 0, sizeof(ei->i_block));
	ei->i_file_acl = 0;
	ei->i_dir_acl = 0;
	inode->i_generation = sbi->s_next_generation++;

	insert_inode_hash(inode);
    mark_inode_dirty(inode);
    return inode;

fail:
	make_bad_inode(inode);
	iput(inode);
	return ERR_PTR(err);
}

int lab4fs_setattr(struct dentry *dentry, struct iattr *iattr)
{
    struct inode *inode = dentry->d_inode;
    int error;

    error = inode_change_ok(inode, iattr);
    if (error)
        return error;
	error = inode_setattr(inode, iattr);
    return error;
}

int lab4fs_permission(struct inode *inode, int mask, struct nameidata *nd)
{
	int mode = inode->i_mode;
	/* Nobody gets write access to a read-only fs */
	if ((mask & MAY_WRITE) && IS_RDONLY(inode) &&
	    (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode)))
	/* Nobody gets write access to an immutable file */
	if ((mask & MAY_WRITE) && IS_IMMUTABLE(inode))
	    return -EACCES;
    /* Pervious two if-stmt seems nothing to do with our fs... */
    /* if the current user owns this inode */
	if (current->fsuid == inode->i_uid) {
		mode >>= 6;
	} else {
        /* if the current user is in the group */
		if (in_group_p(inode->i_gid))
			mode >>= 3;
	}
	if ((mode & mask & S_IRWXO) == mask)
		return 0;
	/* Allowed to override Discretionary Access Control? */
	if (!(mask & MAY_EXEC) ||
	    (inode->i_mode & S_IXUGO) || S_ISDIR(inode->i_mode))
		if (capable(CAP_DAC_OVERRIDE))
			return 0;
	/* Read and search granted if capable(CAP_DAC_READ_SEARCH) */
	if (capable(CAP_DAC_READ_SEARCH) && ((mask == MAY_READ) ||
	    (S_ISDIR(inode->i_mode) && !(mask & MAY_WRITE))))
		return 0;

	return -EACCES;
}

static int lab4fs_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, lab4fs_get_block, wbc);
}

static int lab4fs_readpage(struct file *file, struct page *page)
{
	return mpage_readpage(page, lab4fs_get_block);
}

static int
lab4fs_readpages(struct file *file, struct address_space *mapping,
		struct list_head *pages, unsigned nr_pages)
{
	return mpage_readpages(mapping, pages, nr_pages, lab4fs_get_block);
}

static int
lab4fs_prepare_write(struct file *file, struct page *page,
			unsigned from, unsigned to)
{
	return block_prepare_write(page,from,to,lab4fs_get_block);
}

static sector_t lab4fs_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping,block,lab4fs_get_block);
}

static int
lab4fs_get_blocks(struct inode *inode, sector_t iblock, unsigned long max_blocks,
			struct buffer_head *bh_result, int create)
{
	int ret;

	ret = lab4fs_get_block(inode, iblock, bh_result, create);
	if (ret == 0)
		bh_result->b_size = (1 << inode->i_blkbits);
	return ret;
}

static ssize_t
lab4fs_direct_IO(int rw, struct kiocb *iocb, const struct iovec *iov,
			loff_t offset, unsigned long nr_segs)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;

	return blockdev_direct_IO(rw, iocb, inode, inode->i_sb->s_bdev, iov,
				offset, nr_segs, lab4fs_get_blocks, NULL);
}

static int
lab4fs_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	return mpage_writepages(mapping, wbc, lab4fs_get_block);
}

struct address_space_operations lab4fs_aops = {
	.readpage		= lab4fs_readpage,
	.readpages		= lab4fs_readpages,
	.writepage		= lab4fs_writepage,
	.sync_page		= block_sync_page,
	.prepare_write		= lab4fs_prepare_write,
	.commit_write		= generic_commit_write,
	.bmap			= lab4fs_bmap,
	.direct_IO		= lab4fs_direct_IO,
	.writepages		= lab4fs_writepages,
};

