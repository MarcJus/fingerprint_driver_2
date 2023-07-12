#ifndef _FINGERPRINT_FILE_
#define _FINGERPRINT_FILE_

#include <linux/fs.h>

int fingerprint_open(struct inode * inode, struct file * file);

int fingerprint_release(struct inode *inode, struct file *file);

ssize_t fingerprint_read(struct file *file, char __user *buffer, size_t count, loff_t *off);

#endif //_FINGERPRINT_FILE_
