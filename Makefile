#
# Makefile for lab4fs
#

obj-$(CONFIG_LAB4_FS) += lab4fs.o

lab4fs-objs := super.o	\
	inode.o		\
	dir.o		\
	bitmap.o
