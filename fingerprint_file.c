#include <linux/fs.h>
#include "fingerprint_file.h"

int fingerprint_open(struct inode *inode, struct file *file){
	return 0;
}

int fingerprint_release(struct inode *inode, struct file *file){
	return 0;
}

ssize_t fingerprint_read(struct file *file, char __user *buffer, size_t cout, loff_t *off){
	return 0;
}