#include <linux/config.h>
#include <linux/module.h>
#include <asm/bitops.h>
#include "lab4fs.h"

int bitmap_setup(struct lab4fs_bitmap *bitmap, struct super_block *sb,
        __u32 start_block)
{
    int bits_per_block;
    int i;
    __u32 current_block = start_block;
    int nr_valid_bits = bitmap->nr_valid_bits;
    bits_per_block = sb->s_block_size << 3;

    for(i = 0; nr_valid_bits > 0; nr_valid_bits -= bits_per_block, i++) {
        struct buffer_head *bh;
		bh = sb_bread(sb, current_block);
        if (!bh) {
            LAB4ERROR("Cannot load bitmap at block %lu\n", current_block);
            return -1;
        }
        bitmap->bhs[i] = bh;
    }
    bitmap->nr_bhs = i + 1;
    return 0;
}

