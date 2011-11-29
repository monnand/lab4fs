#include "lab4fs.h"

struct inode_operations lab4fs_file_inode_operations = {
    .setattr    = lab4fs_setattr,
	.permission	= lab4fs_permission,
};

struct file_operations lab4fs_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_file_read,
	.write		= generic_file_write,
	.aio_read	= generic_file_aio_read,
	.aio_write	= generic_file_aio_write,
	.mmap		= generic_file_mmap,
	.open		= generic_file_open,
	.readv		= generic_file_readv,
	.writev		= generic_file_writev,
	.sendfile	= generic_file_sendfile,
};

