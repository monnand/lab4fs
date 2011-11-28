#include "lab4fs.h"

static inline unsigned lab4fs_chunk_size(struct inode *inode)
{
	return inode->i_sb->s_blocksize;
}

static inline void lab4fs_put_page(struct page *page)
{
	kunmap(page);
	page_cache_release(page);
}

static inline unsigned long dir_pages(struct inode *inode)
{
	return (inode->i_size+PAGE_CACHE_SIZE-1)>>PAGE_CACHE_SHIFT;
}

static unsigned
lab4fs_last_byte(struct inode *inode, unsigned long page_nr)
{
	unsigned last_byte = inode->i_size;

	last_byte -= page_nr << PAGE_CACHE_SHIFT;
	if (last_byte > PAGE_CACHE_SIZE)
		last_byte = PAGE_CACHE_SIZE;
	return last_byte;
}

/* TODO */
#define lab4fs_check_page(page) 

static struct page *lab4fs_get_page(struct inode *dir, unsigned long n)
{
    struct address_space *mapping = dir->i_mapping;
	struct page *page = read_cache_page(mapping, n,
				(filler_t*)mapping->a_ops->readpage, NULL);
    if (!IS_ERR(page)) {
        wait_on_page_locked(page);
        kmap(page);
        if (!PageUptodate(page))
            goto fail;
        if (!PageChecked(page))
            lab4fs_check_page(page);
        if (PageError(page))
            goto fail;
    }
    return page;

fail:
    lab4fs_put_page(page);
    return ERR_PTR(-EIO);
}


static inline struct lab4fs_dir_entry *
lab4fs_next_entry(struct lab4fs_dir_entry *p)
{
    return (struct lab4fs_dir_entry *)
        ((char *)p + le16_to_cpu(p->rec_len));
}

static unsigned char lab4fs_filetype_table[LAB4FS_FT_MAX] = {
	[LAB4FS_FT_UNKNOWN]	= DT_UNKNOWN,
	[LAB4FS_FT_REG_FILE]	= DT_REG,
	[LAB4FS_FT_DIR]		= DT_DIR,
	[LAB4FS_FT_CHRDEV]	= DT_CHR,
	[LAB4FS_FT_BLKDEV]	= DT_BLK,
	[LAB4FS_FT_FIFO]		= DT_FIFO,
	[LAB4FS_FT_SOCK]		= DT_SOCK,
	[LAB4FS_FT_SYMLINK]	= DT_LNK,
};

static int
lab4fs_readdir (struct file * filp, void * dirent, filldir_t filldir)
{
    loff_t pos = filp->f_pos;
    struct inode *inode = filp->f_dentry->d_inode;
    struct super_block *sb = inode->i_sb;
    unsigned int offset = pos & ~PAGE_CACHE_MASK;
    unsigned long n = pos >> PAGE_CACHE_SHIFT;
    unsigned long npages = dir_pages(inode);
	unsigned chunk_mask = ~(lab4fs_chunk_size(inode)-1);
	unsigned char *types = NULL;
	int ret;

#ifdef CONFIG_LAB4FS_DEBUG
    char filename[255];
    memset(filename, 0, 255);
#endif

	if (pos > inode->i_size - LAB4FS_DIR_REC_LEN(1))
		goto success;

    types = lab4fs_filetype_table;

    /*
    LAB4DEBUG("I will read %lu pages for this dir\n", npages);
    */

    for (; n < npages; n++ offset = 0) {
        char *kaddr, *limit;
        struct lab4fs_dir_entry *de;

        struct page *page = lab4fs_get_page(inode, n);

        if(IS_ERR(page)) {
            LAB4ERROR("bad page in #%lu\n", inode->i_ino);
            filp->f_op += PAGE_CACHE_SIZE - offset;
            continue;
        }
		kaddr = page_address(page);
		de = (struct lab4fs_dir_entry *)(kaddr+offset);
		limit = kaddr + lab4fs_last_byte(inode, n) - LAB4FS_DIR_REC_LEN(1);
		for ( ;(char*)de <= limit; de = lab4fs_next_entry(de)) {
			if (de->rec_len == 0) {
                LAB4ERROR("zero-length directory entry\n");
                ret = -EIO;
                lab4fs_put_page(page);
                goto done;
            }
			if (de->inode) {
                int over;
                unsigned char d_type = DT_UNKNOWN;

                d_type = types[de->file_type];

                offset = (char *)de - kaddr;
#ifdef CONFIG_LAB4FS_DEBUG
                memcpy(filename, de->name, de->name_len);
                filename[de->name_len] = 0;
                LAB4DEBUG("I got a file %s\n", filename);
#endif
                over = filldir(dirent, de->name, de->name_len,
						(n<<PAGE_CACHE_SHIFT) | offset,
						le32_to_cpu(de->inode), d_type);
                if (over) {
                    lab4fs_put_page(page);
                    goto success;
                }
            }
            filp->f_pos += le16_to_cpu(de->rec_len);
        }
        lab4fs_put_page(page);
    }

success:
	ret = 0;
done:
	return ret;
}

struct file_operations lab4fs_dir_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.readdir	= lab4fs_readdir,
};
