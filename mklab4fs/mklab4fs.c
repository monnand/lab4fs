#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <byteswap.h>

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
 * Ext2/linux mode flags.  We define them here so that we don't need
 * to depend on the OS's sys/stat.h, since we may be compiling on a
 * non-Linux system.
 */
#define LINUX_S_IFMT  00170000
#define LINUX_S_IFSOCK 0140000
#define LINUX_S_IFLNK    0120000
#define LINUX_S_IFREG  0100000
#define LINUX_S_IFBLK  0060000
#define LINUX_S_IFDIR  0040000
#define LINUX_S_IFCHR  0020000
#define LINUX_S_IFIFO  0010000
#define LINUX_S_ISUID  0004000
#define LINUX_S_ISGID  0002000
#define LINUX_S_ISVTX  0001000

#define LINUX_S_IRWXU 00700
#define LINUX_S_IRUSR 00400
#define LINUX_S_IWUSR 00200
#define LINUX_S_IXUSR 00100

#define LINUX_S_IRWXG 00070
#define LINUX_S_IRGRP 00040
#define LINUX_S_IWGRP 00020
#define LINUX_S_IXGRP 00010

#define LINUX_S_IRWXO 00007
#define LINUX_S_IROTH 00004
#define LINUX_S_IWOTH 00002
#define LINUX_S_IXOTH 00001

#define LINUX_S_ISLNK(m)    (((m) & LINUX_S_IFMT) == LINUX_S_IFLNK)
#define LINUX_S_ISREG(m)    (((m) & LINUX_S_IFMT) == LINUX_S_IFREG)
#define LINUX_S_ISDIR(m)    (((m) & LINUX_S_IFMT) == LINUX_S_IFDIR)
#define LINUX_S_ISCHR(m)    (((m) & LINUX_S_IFMT) == LINUX_S_IFCHR)
#define LINUX_S_ISBLK(m)    (((m) & LINUX_S_IFMT) == LINUX_S_IFBLK)
#define LINUX_S_ISFIFO(m)   (((m) & LINUX_S_IFMT) == LINUX_S_IFIFO)
#define LINUX_S_ISSOCK(m)   (((m) & LINUX_S_IFMT) == LINUX_S_IFSOCK)

/*
 * EXT2_DIR_PAD defines the directory entries boundaries
 *
 * NOTE: It must be a multiple of 4
 */
#define EXT2_DIR_PAD            4
#define EXT2_DIR_ROUND          (EXT2_DIR_PAD - 1)
#define LAB4FS_DIR_REC_LEN(name_len)  (((name_len) + 8 + EXT2_DIR_ROUND) & \
                     ~EXT2_DIR_ROUND)


#define LAB4FS_MAGIC    0x1ab4f5

#define VERBOSE(string, args...)  do {\
    if (verbose) printf(string, ##args); \
} while (0)

#define INODESIZE   128
#define NR_BLKS_PER_FILE    1.0

#ifndef htole32
#define htole32(x) (bswap_32(htonl(x)))
#endif

#ifndef htole16
#define htole16(x) (bswap_16(htons(x)))
#endif

#define LAB4FS_ROOT_INO     1
#define LAB4FS_FIRST_INO    2

#define LAB4FS_NDIR_BLOCKS  7
#define LAB4FS_IND_BLOCKS   8
#define LAB4FS_N_BLOCKS     8

#define __le32 uint32_t
#define __le16 uint16_t
#define __u8 uint8_t

static int verbose = 1;

/*
 * first 1024B: MBR + Other stuff
 * 1024 ~ 2048: Super block
 */
struct lab4fs_sb_info {
    uint32_t magic;
    uint32_t block_count;
    uint32_t block_size; 
    uint32_t inode_count;
    uint32_t inode_size;
    uint32_t first_available_block;
    uint32_t first_inode_bitmap_block;
    uint32_t first_data_bitmap_block;
    uint32_t first_inode_block;
    uint32_t first_data_block;
    uint32_t root_inode;
    uint32_t first_inode;
    uint32_t free_inode_count;
    uint32_t free_data_block_count;
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

#define LAB4FS_NAME_LEN     255

struct lab4fs_dir_entry {
	__le32	inode;			/* Inode number */
	__le16	rec_len;		/* Directory entry length */
	__u8	name_len;		/* Name length */
	__u8	file_type;
	char	name[LAB4FS_NAME_LEN];	/* File name */
};

/* Copy from kernel */
int set_bit(int nr,long * addr)
{
    int mask, retval;

    addr += nr >> 5;
    mask = 1 << (nr & 0x1f);
    retval = (mask & *addr) != 0;
    *addr |= mask;
    return retval;
}

int clear_bit(int nr, long * addr)
{
    int mask, retval;

    addr += nr >> 5;
    mask = 1 << (nr & 0x1f);
    retval = (mask & *addr) != 0;
    *addr &= ~mask;
    return retval;
}

int test_bit(int nr, const unsigned long * addr)
{
    int mask;

    addr += nr >> 5;
    mask = 1 << (nr & 0x1f);
    return ((mask & *addr) != 0); 
}

/* Well... Nothing but rename... */
void bit_set(uint8_t *buf, uint32_t bit)
{
    set_bit((int)bit, (long *)buf);
}

int bit_test(uint8_t *buf, uint32_t bit)
{
    return test_bit((int)bit, (long *)buf);
}

int total_space(char *filename, unsigned long *nr_blks, unsigned long *blk_size)
{
    struct stat fstat;
    int fd;
    if (stat(filename, &fstat) < 0) {
        return -1;
    }
    if (S_ISREG(fstat.st_mode)) {
        *blk_size = 1024;
        *nr_blks = fstat.st_size >> 10;
        VERBOSE("This is a regular file, size: %uK\n", *nr_blks); 
    } else if (S_ISBLK(fstat.st_mode)) {
        *blk_size = 1024;
        fd = open(filename, O_RDONLY);
        ioctl(fd, BLKGETSIZE, nr_blks);
        *nr_blks = *nr_blks >> 1;
        VERBOSE("This is a block device, block size: %u, nr blocks: %u\n", *blk_size, *nr_blks); 
    } else
        return -1;
    return 0;
}

struct lab4fs_sb_info *get_sb(unsigned long nr_blks, unsigned long blk_size)
{
    struct lab4fs_sb_info *sb;
    uint32_t i, j;
    double d;

    sb = (struct lab4fs_sb_info *)malloc(sizeof(struct lab4fs_sb_info));
    memset(sb, 0, sizeof(struct lab4fs_sb_info));
    sb->magic = LAB4FS_MAGIC;
    sb->block_size = blk_size;
    sb->block_count = nr_blks;

    /* First available block. */
    i = 2048 / blk_size;
    i += (2048 % blk_size? 1 : 0); 
    sb->first_available_block = i;

    sb->first_inode_bitmap_block = sb->first_available_block;

    /* Number of blocks for inode bitmap, inode and data */
    j = nr_blks - sb->first_available_block;

    /* 
     * Number of bytes per file, including:
     *  NR_BLKS_PER_FILE * blocksize for data.
     *  INODESIZE bytes for inode;
     *  one bit in inode bitmap; 
     *  NR_BLKS_PER_FILE bits in data block bitmap
     */
    d = sb->block_size * NR_BLKS_PER_FILE + INODESIZE + 0.125 + 0.125 * NR_BLKS_PER_FILE;

    /* 
     * Decide how many inode we need inside this filesystem.
     * One inode per file
     */
    sb->inode_count = (j * sb->block_size) / d;
    if (j = (sb->inode_count * INODESIZE) % sb->block_size)
        sb->inode_count += j / INODESIZE;

    /* Number of bytes for inode bitmap */
    i = sb->inode_count >> 3;
    sb->inode_count = i << 3;
    sb->free_inode_count = sb->inode_count - LAB4FS_FIRST_INO;

    /* Number of blocks for inode bitmap */
    j = (i % sb->block_size) ? 1 : 0;
    j += (i / sb->block_size);
    sb->first_data_bitmap_block = j + sb->first_inode_bitmap_block;

    /* Number of blocks for inodes */
    j = (sb->inode_count * INODESIZE) / sb->block_size;

    /* Number of blocks for data block bitmap, inodes, and data blocks */
    i = nr_blks - sb->first_data_bitmap_block;

    /* Number of blocks for data block bitmap and data blocks */
    i = i - j;

    /* Number of blocks for data block bitmap */
    j = i / (8 * sb->block_size + 1);
    i = j + (i % (8 * sb->block_size + 1)) ? 1 : 0;

    /* Number of blocks for inodes */
    j = (sb->inode_count * INODESIZE) / sb->block_size;
    sb->first_inode_block = sb->first_data_bitmap_block + i;
    sb->first_data_block =sb->first_inode_block + j;
    sb->free_data_block_count = nr_blks - sb->first_data_block;
    sb->inode_size = INODESIZE;
    sb->first_inode = LAB4FS_FIRST_INO;
    sb->root_inode = LAB4FS_ROOT_INO;
    return sb;
}

int write_blocks(int fd, struct lab4fs_sb_info *sb, uint32_t offset, uint32_t n, void *data)
{
    int ret = 0;
    if (ret = lseek(fd, offset * sb->block_size, SEEK_SET) < 0)
        return ret;

    return write(fd, data, sb->block_size * n);
}

int read_block(int fd, struct lab4fs_sb_info *sb, uint32_t block, void *data)
{
    int ret = 0;
    if (ret = lseek(fd, block * sb->block_size, SEEK_SET) < 0)
        return ret;
    return read(fd, data, sb->block_size);
}

int bzero_blocks(int fd, struct lab4fs_sb_info *sb, uint32_t offset, uint32_t n)
{
    void *buf;
    int ret;
    buf = malloc(n * sb->block_size);
    memset(buf, 0, sb->block_size * n);
    ret = write_blocks(fd, sb, offset, n, buf);
    free(buf);
    return ret;
}

#define write2buf32(n, buf, i) do { \
    uint32_t tmp;                   \
    VERBOSE(#n ": %u\n", n);        \
    tmp = htole32(n);               \
    memcpy((buf) + i, &tmp, 4);     \
    i += 4; \
} while (0)

#define write2buf16(n, buf, i) do { \
    uint16_t tmp;                   \
    VERBOSE(#n ": %u\n", n);        \
    tmp = htole16(n);               \
    memcpy((buf) + i, &tmp, 2);     \
    i += 2; \
} while (0)

int write_super_block(int fd, struct lab4fs_sb_info *sb)
{
    int ret = 0;
    uint8_t buf[1024];
    int i = 0;
    
    /* skip the boot sector */
    if (ret = lseek(fd, 1024, SEEK_SET) < 0)
        return ret;

    memset(buf, 0, sizeof(buf));
    write2buf32(sb->magic, buf, i);
    write2buf32(sb->block_count, buf, i);
    write2buf32(sb->block_size, buf, i);
    write2buf32(sb->inode_count, buf, i);
    write2buf32(sb->inode_size, buf, i);
    write2buf32(sb->first_available_block, buf, i);
    write2buf32(sb->first_inode_bitmap_block, buf, i);
    write2buf32(sb->first_data_bitmap_block, buf, i);
    write2buf32(sb->first_inode_block, buf, i);
    write2buf32(sb->first_data_block, buf, i);
    write2buf32(sb->root_inode, buf, i);
    write2buf32(sb->first_inode, buf, i);
    write2buf32(sb->free_inode_count, buf, i);
    write2buf32(sb->free_data_block_count, buf, i);

    return write(fd, buf, sizeof(buf));
}

int write_data_bitmap(int fd, struct lab4fs_sb_info *sb)
{
    return bzero_blocks(fd, sb, sb->first_data_bitmap_block,
            sb->first_inode_block - sb->first_data_bitmap_block);
}

#define MSB_MASK    128

int write_inode_bitmap(int fd, struct lab4fs_sb_info *sb)
{
    int i, n, offset;
    uint8_t mask;
    uint8_t *buf;
    i = bzero_blocks(fd, sb, sb->first_inode_bitmap_block,
            sb->first_data_bitmap_block - sb->first_inode_bitmap_block);
    if (i < 0)
        return i;
    buf = malloc(sb->block_size);
    memset(buf, 0, sb->block_size); 
    for (i = 0; i < sb->first_inode; i++)
        bit_set(buf, i);
    i = write_blocks(fd, sb, sb->first_inode_bitmap_block, 1, buf);
    free(buf);
    return i;
}

void locate_inode_bit(struct lab4fs_sb_info *sb,
        uint32_t ino, uint32_t *block, uint32_t *bit)
{
    uint32_t i;
    i = sb->block_size << 3;
    *block = ino / i + sb->first_inode_bitmap_block;
    *bit= ino % i;
}

void locate_inode(struct lab4fs_sb_info *sb,
        uint32_t ino, uint32_t *block, uint32_t *offset)
{
    uint32_t i;
    i = sb->block_size / sb->inode_size;
    *block = ino / i + sb->first_inode_block;
    *offset = (ino % i) * sb->inode_size;
}

void locate_datablock_bit(struct lab4fs_sb_info *sb,
        uint32_t absolute_block_num, uint32_t *block, uint32_t *bit)
{
    uint32_t i;
    i = sb->block_size << 3;

    if (absolute_block_num < sb->first_data_block) {
        *block = 0;
        *bit = 0;
        return;
    }
    absolute_block_num = absolute_block_num - sb->first_data_block;

    *block = absolute_block_num / i + sb->first_data_bitmap_block;
    *bit = absolute_block_num % i;
}

/* Return 0 on error!!! */
uint32_t first_free_data_block(int fd, struct lab4fs_sb_info *sb)
{
    uint8_t *buf;
    uint32_t offset, block;
    uint32_t i;

    buf = malloc(sb->block_size);

    offset = 0;
    for (block = sb->first_data_bitmap_block;
            block < sb->first_inode_block;
            block++, offset += sb->block_size << 3) {
        read_block(fd, sb, block, buf);
        for (i = 0; i < sb->block_size << 3; i++) {
            if (!bit_test(buf, i)) {
                if (i + offset + sb->first_data_block > sb->block_count)
                    goto no_data_block;
                free(buf);
                return i + offset + sb->first_data_block;
            }
        }
    }
no_data_block:
    free(buf);
    return 0;
}

/* Return 0 on error!!! */
uint32_t first_free_inode(int fd, struct lab4fs_sb_info *sb)
{
    uint8_t *buf;
    uint32_t offset, block;
    uint32_t i;

    buf = malloc(sb->block_size);

    offset = 0;
    for (block = sb->first_inode_bitmap_block;
            block < sb->first_data_bitmap_block;
            block++, offset += sb->block_size << 3) {
        read_block(fd, sb, block, buf);
        for (i = 0; i < sb->block_size << 3; i++) {
            if (!bit_test(buf, i)) {
                if (i + offset >= sb->inode_count)
                    goto no_inode;
                free(buf);
                return i + offset;
            }
        }
    }
no_inode:
    free(buf);
    return 0;
}


int write_data_blocks(int fd, struct lab4fs_sb_info *sb,
        uint32_t block, uint32_t n, void *data)
{
    uint32_t i;
    uint32_t current_block, goal, offset;
    uint8_t *buf;

    if (block < sb->first_data_block || block >= sb->block_count)
        return -1;

    buf = malloc(sb->block_size);
    current_block = 0;
    for (i = 0; i < n; i++) {
        locate_datablock_bit(sb, block, &goal, &offset);
        if (goal != current_block) {
            if (current_block != 0)
                write_blocks(fd, sb, current_block, 1, buf);
            read_block(fd, sb, goal, buf);
            current_block = goal;
        }
        bit_set(buf, offset);
    }
    if (current_block != 0)
        write_blocks(fd, sb, current_block, 1, buf);
    return write_blocks(fd, sb, block, n, data);
}

int write_to_free_data_blocks(int fd, struct lab4fs_sb_info *sb,
        uint32_t nr_blocks, void *data, uint32_t *selected_blocks)
{
    uint32_t left, ret;
    uint32_t block, i;
    i = 0;
    for (left = nr_blocks; left > 0; left--) {
        block = first_free_data_block(fd, sb);
        if (!block)
            return nr_blocks - left;
        ret = write_data_blocks(fd, sb, block, 1, data + (nr_blocks - left));
        sb->free_data_block_count--;
        if (ret < 0)
            return nr_blocks - left;
        selected_blocks[i] = block;
        i++;
    }
    return nr_blocks;
}

int write_inode(int fd, struct lab4fs_sb_info *sb,
        struct lab4fs_inode *inode, uint32_t ino)
{
    uint8_t *buf;
    uint32_t offset, block;
    uint32_t i;

    buf = malloc(sb->block_size);
    /* first, set the bit in bitmap */

    locate_inode_bit(sb, ino, &block, &offset);

    read_block(fd, sb, block, buf);
    if (!bit_test(buf, offset)) {
        sb->free_inode_count--;
        bit_set(buf, offset);
        write_blocks(fd, sb, block, 1, buf);
    }

    /* which block should we write for the inode */
    locate_inode(sb, ino, &block, &offset);

    read_block(fd, sb, block, buf);

    i = offset;
    write2buf16(inode->i_mode, buf, i);
    write2buf16(inode->i_links_count, buf, i);
    write2buf32(inode->i_size, buf, i);
    write2buf32(inode->i_atime, buf, i);
    write2buf32(inode->i_ctime, buf, i);
    write2buf32(inode->i_mtime, buf, i);
    write2buf32(inode->i_dtime, buf, i);
    write2buf32(inode->i_gid, buf, i);
    write2buf32(inode->i_uid, buf, i);
    write2buf32(inode->i_blocks, buf, i);

    /* 
     * Since we used i as a counter for buffer.
     * We have to choose another one as counter for loop.
     */
    for (offset = 0; offset < LAB4FS_N_BLOCKS; offset++)
        write2buf32(inode->i_block[offset], buf, i);
    write2buf32(inode->i_file_acl, buf, i);
    write2buf32(inode->i_dir_acl, buf, i);
    write_blocks(fd, sb, block, 1, buf);
    free(buf);
}

#define MIN(x, y)   ((x) < (y) ? (x) : (y))

int write_inode_block_table(int fd, struct lab4fs_sb_info *sb,
        struct lab4fs_inode *inode, uint32_t *selected_blocks, uint32_t nr_blocks)
{
    uint32_t i;
    uint32_t *buf;

    memset(inode->i_block, 0, 4 * LAB4FS_N_BLOCKS);
    for (i = 0; i < MIN(nr_blocks, LAB4FS_NDIR_BLOCKS); i++)
        inode->i_block[i] = htole32(selected_blocks[i]);

    if (nr_blocks > LAB4FS_NDIR_BLOCKS) {
        buf = (uint32_t *)malloc(sb->block_size);
        memset(buf, 0, sb->block_size);
        nr_blocks -= LAB4FS_NDIR_BLOCKS;
        selected_blocks = &selected_blocks[LAB4FS_NDIR_BLOCKS];
        for (i = 0; i < MIN(nr_blocks, sb->block_size >> 2); i++)
            buf[i] = htole32(selected_blocks[i]);
        write_to_free_data_blocks(fd, sb, 1, buf, &i);
        inode->i_block[LAB4FS_IND_BLOCKS] = i;
        free(buf);
    }
    inode->i_blocks += nr_blocks;
    return 0;
}

int write_root_dir(int fd, struct lab4fs_sb_info *sb)
{
    uint32_t block, offset;
    struct lab4fs_inode inode;
    struct lab4fs_dir_entry *entry;

    uint8_t *buf;
    uint32_t uid, gid;

    locate_inode_bit(sb, sb->root_inode, &block, &offset);
    VERBOSE("The root dir inode bit is in block %lu, bit %lu\n",
            block, offset);

    locate_inode(sb, sb->root_inode, &block, &offset);
    VERBOSE("The root dir inode is in block %lu, byte %lu\n",
            block, offset);

    buf = malloc(sb->block_size);
    memset(buf, 0, sb->block_size);

    entry = (struct lab4fs_dir_entry *)buf;
    entry->inode = sb->root_inode;
    entry->rec_len = LAB4FS_DIR_REC_LEN(1);
    entry->name_len = 1;
    entry->name[0] = '.';
    entry->file_type = LAB4FS_FT_DIR;
    inode.i_size = entry->rec_len;

    entry = (struct lab4fs_dir_entry *)(buf + entry->rec_len);
    entry->inode = sb->root_inode;
    entry->rec_len = LAB4FS_DIR_REC_LEN(2);
    entry->name_len = 2;
    entry->name[0] = '.';
    entry->name[1] = '.';
    entry->file_type = LAB4FS_FT_DIR;
    inode.i_size += entry->rec_len;

    entry = (struct lab4fs_dir_entry *)((uint8_t *)entry + entry->rec_len);
    entry->inode = sb->first_inode;
    entry->rec_len = LAB4FS_DIR_REC_LEN(1);
    entry->name_len = 1;
    entry->name[0] = 'd';
    entry->file_type = LAB4FS_FT_DIR;
    inode.i_size += entry->rec_len;

    inode.i_mode = LINUX_S_IFDIR | 0755;
    uid = getuid();
    inode.i_uid = uid;
    if (uid) {
        gid = getgid();
        inode.i_gid = gid;
    } else {
        inode.i_gid = 0;
    }
    inode.i_atime = inode.i_ctime = inode.i_mtime = time(NULL);
    inode.i_dtime = 0;
    inode.i_links_count = 3;
    inode.i_blocks = 0;
    inode.i_dir_acl = 0755;
    inode.i_file_acl = 0755;

    write_to_free_data_blocks(fd, sb, 1, buf, &block); 
    write_inode_block_table(fd, sb, &inode, &block, 1);
    write_inode(fd, sb, &inode, sb->root_inode);

    /* Then write the directory under root */

    memset(buf, 0, sb->block_size);
    entry = (struct lab4fs_dir_entry *)buf;
    entry->inode = sb->first_inode;
    entry->rec_len = LAB4FS_DIR_REC_LEN(1);
    entry->name_len = 1;
    entry->name[0] = '.';
    entry->file_type = LAB4FS_FT_DIR;
    inode.i_size = entry->rec_len;

    entry = (struct lab4fs_dir_entry *)(buf + entry->rec_len);
    entry->inode = sb->root_inode;
    entry->rec_len = LAB4FS_DIR_REC_LEN(2);
    entry->name_len = 2;
    entry->name[0] = '.';
    entry->name[1] = '.';
    entry->file_type = LAB4FS_FT_DIR;
    inode.i_size += entry->rec_len;

    inode.i_atime = inode.i_ctime = inode.i_mtime = time(NULL);
    inode.i_dtime = 0;
    inode.i_links_count = 3;
    inode.i_blocks = 0;
    inode.i_dir_acl = 0755;
    inode.i_file_acl = 0755;

    write_to_free_data_blocks(fd, sb, 1, buf, &block); 
    write_inode_block_table(fd, sb, &inode, &block, 1);
    write_inode(fd, sb, &inode, sb->first_inode);

    free(buf);

    return 0;
}

int main(int argc, char *argv[])
{
    char *filename;
    unsigned long nr_blks, blk_size;
    struct lab4fs_sb_info *sb;
    int fd;

    if (argc < 2) {
        fprintf(stderr, "%s filename\n", argv[0]);
        return -1;
    }

    filename = argv[1];
    if (total_space(filename, &nr_blks, &blk_size) < 0) {
        fprintf(stderr, "%s is not a regular file nor a block device\n", filename);
        return -1;
    }

    fd = open(filename, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "%s cannot be open with write-only flag\n", filename);
        return -1;
    }

    sb = get_sb(nr_blks, blk_size);

    write_data_bitmap(fd, sb);
    write_inode_bitmap(fd, sb);

    write_root_dir(fd, sb);
    write_super_block(fd, sb);
    return 0;
}
