#include "lab4fs.h"
#include <linux/time.h>

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

static inline int lab4fs_match(int len, const char *const name,
        struct lab4fs_dir_entry *de)
{
    if (len != de->name_len)
        return 0;
    if (!de->inode)
        return 0;
    return !memcmp(name, de->name, len);
}

static inline struct lab4fs_dir_entry *
lab4fs_next_entry(struct lab4fs_dir_entry *p)
{
    return (struct lab4fs_dir_entry *)
        ((char *)p + le16_to_cpu(p->rec_len));
}

static inline void lab4fs_inc_count(struct inode *inode)
{
    inode->i_nlink++;
    mark_inode_dirty(inode);
}

static inline void lab4fs_dec_count(struct inode *inode)
{
	inode->i_nlink--;
	mark_inode_dirty(inode);
}

static inline unsigned lab4fs_chunk_size(struct inode *inode)
{
	return inode->i_sb->s_blocksize;
}

#define S_SHIFT 12
static unsigned char lab4fs_type_by_mode[S_IFMT >> S_SHIFT] = {
	[S_IFREG >> S_SHIFT]	= LAB4FS_FT_REG_FILE,
	[S_IFDIR >> S_SHIFT]	= LAB4FS_FT_DIR,
	[S_IFCHR >> S_SHIFT]	= LAB4FS_FT_CHRDEV,
	[S_IFBLK >> S_SHIFT]	= LAB4FS_FT_BLKDEV,
	[S_IFIFO >> S_SHIFT]	= LAB4FS_FT_FIFO,
	[S_IFSOCK >> S_SHIFT]	= LAB4FS_FT_SOCK,
	[S_IFLNK >> S_SHIFT]	= LAB4FS_FT_SYMLINK,
};

static inline void lab4fs_set_de_type(struct lab4fs_dir_entry *de, struct inode *inode)
{
	mode_t mode = inode->i_mode;
    de->file_type = lab4fs_type_by_mode[(mode & S_IFMT)>>S_SHIFT];
}

static int lab4fs_commit_chunk(struct page *page, unsigned from, unsigned to)
{
    struct inode *dir = page->mapping->host;
    int err = 0;
    dir->i_version++;
    err = page->mapping->a_ops->commit_write(NULL, page, from, to);
    if (err < 0) {
        unlock_page(page);
        LAB4DEBUG("error on commit chunk\n");
        return err;
    }
    if (IS_DIRSYNC(dir))
        err = write_one_page(page, 1);
    else
        unlock_page(page);
    return err;
}

int lab4fs_add_link (struct dentry *dentry, struct inode *inode)
{	
    struct inode *dir = dentry->d_parent->d_inode;
	const char *name = dentry->d_name.name;
	int namelen = dentry->d_name.len;
	unsigned chunk_size = lab4fs_chunk_size(dir);
	unsigned reclen = LAB4FS_DIR_REC_LEN(namelen);
	unsigned short rec_len, name_len;
	struct page *page = NULL;
	struct lab4fs_dir_entry *de;
	unsigned long npages = dir_pages(dir);
	unsigned long n;
	char *kaddr;
	unsigned from, to;
	int err;

    for (n = 0; n <= npages; n++) {
        char *dir_end;

        page = lab4fs_get_page(dir, n);
        err = PTR_ERR(page);
        if (IS_ERR(page))
            goto out;

        lock_page(page);
        kaddr = page_address(page);

        dir_end = kaddr + lab4fs_last_byte(dir, n);
        de = (struct lab4fs_dir_entry *)kaddr;
        kaddr += PAGE_CACHE_SIZE - reclen;

        while((char *)de <= kaddr) {
            if ((char *)de == dir_end) {
                name_len = 0;
                rec_len = chunk_size;
                de->rec_len = cpu_to_le16(chunk_size);
                de->inode = 0;
                goto got_it;
            }
            if (de->rec_len == 0) {
                LAB4ERROR("0-len dir entry\n");
                err = -EIO;
                goto out_unlock;
            }
            err = -EEXIST;
            if (lab4fs_match(namelen, name, de))
                goto out_unlock;
            name_len = LAB4FS_DIR_REC_LEN(de->name_len);
            rec_len = le16_to_cpu(de->rec_len);
            if (!de->inode && rec_len >= reclen)
                goto got_it;
            if (rec_len >= name_len + reclen)
                goto got_it;
            de = (struct lab4fs_dir_entry *) ((char *) de + rec_len);
        }
        unlock_page(page);
        lab4fs_put_page(page);
        LAB4DEBUG("no space on %dth page. try next\n", n);
    }

    BUG();
    return -EINVAL;

got_it:
    from = (char *)de - (char *)page_address(page);
    to = from + rec_len;
    if (to > PAGE_CACHE_SIZE) {
        LAB4DEBUG("look! I want to write on %dth page, but will fail!\n", n);
        LAB4DEBUG("from: %u; to %u\n", (unsigned)from, (unsigned)to);
        goto out_unlock;
    }
    err = page->mapping->a_ops->prepare_write(NULL, page, from, to);
    if (err)
        goto out_unlock;
    if (de->inode) {
        struct lab4fs_dir_entry *de1 = (struct lab4fs_dir_entry *)
            ((char *) de + name_len);
        de1->rec_len = cpu_to_le16(rec_len - name_len);
        de->rec_len = cpu_to_le16(name_len);
        de = de1;
    }
    de->name_len = namelen;
    memcpy(de->name, name, namelen);
    de->inode = cpu_to_le32(inode->i_ino);
    lab4fs_set_de_type(de, inode);

    err = lab4fs_commit_chunk(page, from, to);
    dir->i_mtime = dir->i_ctime = CURRENT_TIME;
	mark_inode_dirty(dir);
out_put:
    lab4fs_put_page(page);
out:
    return err;
out_unlock:
    unlock_page(page);
    goto out_put;
}

static inline int lab4fs_add_nondir(struct dentry *dentry, struct inode *inode)
{
    int err = lab4fs_add_link(dentry, inode);
    if (!err) {
        d_instantiate(dentry, inode);
        return 0;
    }
    lab4fs_dec_count(inode);
    iput(inode);
    return err;
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
    unsigned int offset = pos & ~PAGE_CACHE_MASK;
    unsigned long n = pos >> PAGE_CACHE_SHIFT;
    unsigned long npages = dir_pages(inode);
	unsigned char *types = NULL;
	int ret;

#ifdef CONFIG_LAB4FS_DEBUG
    char filename[255];
    memset(filename, 0, 255);
#endif

	if (pos > inode->i_size - LAB4FS_DIR_REC_LEN(1))
		goto success;

    types = lab4fs_filetype_table;

    for (; n < npages; n++, offset = 0) {
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
#ifdef CONFIG_LAB4FS_READDIR_DEBUG
                LAB4DEBUG("I got a file %s\n", filename);
#endif
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

/*
 *	lab4fs_find_entry()
 *
 * finds an entry in the specified directory with the wanted name. It
 * returns the page in which the entry was found, and the entry itself
 * (as a parameter - res_dir). Page is returned mapped and unlocked.
 * Entry is guaranteed to be valid.
 */
struct lab4fs_dir_entry * lab4fs_find_entry (struct inode * dir,
			struct dentry *dentry, struct page ** res_page)
{
	const char *name = dentry->d_name.name;
	int namelen = dentry->d_name.len;
	unsigned reclen = LAB4FS_DIR_REC_LEN(namelen);
	unsigned long start, n;
	unsigned long npages = dir_pages(dir);
	struct page *page = NULL;
    struct lab4fs_inode_info *ei = LAB4FS_I(dir);
    struct lab4fs_dir_entry *de;

    if (npages == 0)
        goto out;

	/* OFFSET_CACHE */
	*res_page = NULL;

	start = ei->i_dir_start_lookup;
	if (start >= npages)
		start = 0;
	n = start;
    do {
        char *kaddr;
        page = lab4fs_get_page(dir, n);
        if (!IS_ERR(page)) {
            kaddr = page_address(page);
            de = (struct lab4fs_dir_entry *) kaddr;
            kaddr += lab4fs_last_byte(dir, n) - reclen;
            while ((char *) de <= kaddr) {
                if (de->rec_len == 0) {
                    LAB4ERROR("zero-length dir entry\n");
                    lab4fs_put_page(page);
                    goto out;
                }
                if (lab4fs_match(namelen, name, de))
                    goto found;
                de = lab4fs_next_entry(de);
            }
            lab4fs_put_page(page);
        }
        if (++n >= npages)
            n = 0;
    } while (n != start);
out:
    return NULL;
found:
    *res_page = page;
    return de;
}

ino_t lab4fs_inode_by_name(struct inode *dir, struct dentry *dentry)
{
    ino_t res = 0;
    struct lab4fs_dir_entry *de;
    struct page *page;
    de = lab4fs_find_entry(dir, dentry, &page);
    if (de) {
        res = le32_to_cpu(de->inode);
        kunmap(page);
        page_cache_release(page);
    }
    return res;
}

#ifdef CONFIG_LAB4FS_DEBUG

static inline int is_my_test(struct dentry *dentry)
{
    if (dentry->d_name.len == 1)
        if (dentry->d_name.name[0] < 'd' &&
                dentry->d_name.name[0] > 'a')
            return 1;
    return 0;
}

#define printmemaddr(s, mem)    do {    \
    LAB4DEBUG(#mem ": %x\n", (unsigned)(s)->mem);   \
} while(0)


static void print_fops(struct file_operations *fop)
{
    printmemaddr(fop, read);
    printmemaddr(fop, write);
    printmemaddr(fop, aio_read);
    printmemaddr(fop, aio_write);
}
#else
#define is_my_test(dentry)  0
#define print_fops(fop)
#endif

static struct dentry *lab4fs_lookup(struct inode *dir,
        struct dentry *dentry,
        struct nameidata *nd)
{
    struct inode *inode;
    ino_t ino;
	if (dentry->d_name.len > LAB4FS_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	ino = lab4fs_inode_by_name(dir, dentry);

	inode = NULL;
	if (ino) {
		inode = iget(dir->i_sb, ino);
		if (!inode)
			return ERR_PTR(-EACCES);
	}
	if (inode)
		return d_splice_alias(inode, dentry);
	d_add(dentry, inode);
	return NULL;
}

struct dentry *lab4fs_get_parent(struct dentry *child)
{
	unsigned long ino;
	struct dentry *parent;
	struct inode *inode;
	struct dentry dotdot;

	dotdot.d_name.name = "..";
	dotdot.d_name.len = 2;

	ino = lab4fs_inode_by_name(child->d_inode, &dotdot);
	if (!ino)
		return ERR_PTR(-ENOENT);
	inode = iget(child->d_inode->i_sb, ino);

	if (!inode)
		return ERR_PTR(-EACCES);
	parent = d_alloc_anon(inode);
	if (!parent) {
		iput(inode);
		parent = ERR_PTR(-ENOMEM);
	}
	return parent;
} 

static int lab4fs_create(struct inode *dir,
        struct dentry *dentry,
        int mode,
        struct nameidata *nd)
{
    struct inode *inode = lab4fs_new_inode(dir, mode);
	int err = PTR_ERR(inode);
	if (!IS_ERR(inode)) {
		inode->i_op = &simple_dir_inode_operations;
		inode->i_fop = &lab4fs_file_operations;
        inode->i_mapping->a_ops = &lab4fs_aops;
		mark_inode_dirty(inode);
		err = lab4fs_add_nondir(dentry, inode);
	}
	return err;

}

static int lab4fs_link(struct dentry *old_dentry, struct inode *dir,
        struct dentry *dentry)
{
    struct inode *inode = old_dentry->d_inode;

    if (inode->i_nlink >= LAB4FS_LINK_MAX)
        return -EMLINK;
    inode->i_ctime = CURRENT_TIME;
    lab4fs_inc_count(inode);
	atomic_inc(&inode->i_count);

    return lab4fs_add_nondir(dentry, inode);
}

struct inode_operations lab4fs_dir_inode_operations = {
    .create     = lab4fs_create,
    .lookup     = lab4fs_lookup,
    .link       = lab4fs_link,
};

struct file_operations lab4fs_dir_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.readdir	= lab4fs_readdir,
};
