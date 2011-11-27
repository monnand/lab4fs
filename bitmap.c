#include "lab4fs.h"

#define log2(n) ffz(~(n))

int bitmap_setup(struct lab4fs_bitmap *bitmap, struct super_block *sb,
        __u32 start_block)
{
    int bits_per_block;
    int i;
    __u32 current_block = start_block;
    int nr_valid_bits = bitmap->nr_valid_bits;
    bits_per_block = sb->s_blocksize << 3;
    rwlock_init(&bitmap->rwlock);
    write_lock(&bitmap->rwlock);
    bitmap->log_nr_bits_per_block = log2(bits_per_block);
    bitmap->nr_bits_per_block = bits_per_block;

    bitmap->nr_bhs = nr_valid_bits >> bitmap->log_nr_bits_per_block;
    if (nr_valid_bits % bits_per_block)
        bitmap->nr_bhs++;
    bitmap->bhs = kmalloc(sizeof(struct buffer_head *) * bitmap->nr_bhs, GFP_KERNEL);
    if (bitmap->bhs == NULL)
        return -ENOMEM;


    for(i = 0; i < bitmap->nr_bhs; i++) {
        struct buffer_head *bh;
		bh = sb_bread(sb, current_block);
        if (!bh) {
            LAB4ERROR("Cannot load bitmap at block %u\n", current_block);
            write_unlock(&bitmap->rwlock);
            return -EIO;
        }
        bitmap->bhs[i] = bh;
    }
    write_unlock(&bitmap->rwlock);
    return 0;
}

void bitmap_set_bit(struct lab4fs_bitmap *bitmap, int nr)
{
    __u32 n, offset;
    void *data;
    if (unlikely(nr < 0 || nr >= bitmap->nr_valid_bits))
        return;
    write_lock(&bitmap->rwlock);
    n = nr >> bitmap->log_nr_bits_per_block;
    offset = n % bitmap->nr_bits_per_block;
    data = bitmap->bhs[n]->b_data;
    set_bit(offset, data);
    mark_buffer_dirty(bitmap->bhs[n]);
    write_unlock(&bitmap->rwlock);
}

int bitmap_test_and_set_bit(struct lab4fs_bitmap *bitmap, int nr)
{
    __u32 n, offset;
    void *data;
    int ret;
    if (unlikely(nr < 0 || nr >= bitmap->nr_valid_bits))
        return -1;
    write_lock(&bitmap->rwlock);
    n = nr >> bitmap->log_nr_bits_per_block;
    offset = n % bitmap->nr_bits_per_block;
    data = bitmap->bhs[n]->b_data;
    ret = test_and_set_bit(offset, data);
    if (!ret)
        mark_buffer_dirty(bitmap->bhs[n]);
    write_unlock(&bitmap->rwlock);
    return ret;
}

void bitmap_clear_bit(struct lab4fs_bitmap *bitmap, int nr)
{
    __u32 n, offset;
    void *data;
    if (unlikely(nr < 0 || nr >= bitmap->nr_valid_bits))
        return;
    write_lock(&bitmap->rwlock);
    n = nr >> bitmap->log_nr_bits_per_block;
    offset = n % bitmap->nr_bits_per_block;
    data = bitmap->bhs[n]->b_data;
    clear_bit(offset, data);
    mark_buffer_dirty(bitmap->bhs[n]);
    write_unlock(&bitmap->rwlock);
}

int bitmap_test_and_clear_bit(struct lab4fs_bitmap *bitmap, int nr)
{
    __u32 n, offset;
    void *data;
    int ret;
    if (unlikely(nr < 0 || nr >= bitmap->nr_valid_bits))
        return -1;
    write_lock(&bitmap->rwlock);
    n = nr >> bitmap->log_nr_bits_per_block;
    offset = n % bitmap->nr_bits_per_block;
    data = bitmap->bhs[n]->b_data;
    ret = test_and_clear_bit(offset, data);
    if (ret)
        mark_buffer_dirty(bitmap->bhs[n]);
    write_unlock(&bitmap->rwlock);
    return ret;
}

int bitmap_test_bit(struct lab4fs_bitmap *bitmap, int nr)
{
    __u32 n, offset;
    void *data;
    int ret;
    if (unlikely(nr < 0 || nr >= bitmap->nr_valid_bits))
        return -1;
    read_lock(&bitmap->rwlock);
    n = nr >> bitmap->log_nr_bits_per_block;
    offset = n % bitmap->nr_bits_per_block;
    data = bitmap->bhs[n]->b_data;
    ret = test_bit(offset, data);
    read_unlock(&bitmap->rwlock);
    return ret;
}

