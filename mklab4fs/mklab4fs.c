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

#define LAB4FS_MAGIC    0x1ab4f5

#define VERBOSE(string, args...)  do {\
    if (verbose) printf(string, ##args); \
} while (0)

#define INODESIZE   128
#define NR_BLKS_PER_FILE    1.0

#ifndef htole32
#include <byteswap.h>
#define htole32(x) (bswap_32(htonl(x)))

#endif

#define LAB4FS_ROOT_INO     1
#define LAB4FS_FIRST_INO    2

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

int write_inode_bitmap(int fd, struct lab4fs_sb_info *sb)
{
    return bzero_blocks(fd, sb, sb->first_inode_bitmap_block,
            sb->first_data_bitmap_block - sb->first_inode_bitmap_block);
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

    fd = open(filename, O_WRONLY);
    if (fd < 0) {
        fprintf(stderr, "%s cannot be open with write-only flag\n", filename);
        return -1;
    }

    sb = get_sb(nr_blks, blk_size);

    write_super_block(fd, sb);
    write_data_bitmap(fd, sb);
    write_inode_bitmap(fd, sb);
    return 0;
}
