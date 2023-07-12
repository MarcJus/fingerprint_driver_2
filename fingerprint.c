#include <linux/module.h>
#include <linux/init.h>

#define MODULE_NAME "fingerprint"

static int __init module_fingerprint_init(void){
	printk(MODULE_NAME " - init");
	return 0;
}

static void __exit module_fingerprint_exit(void){
	printk(MODULE_NAME " - exit");
}

module_init(module_fingerprint_init);
module_exit(module_fingerprint_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MarcJus");
MODULE_DESCRIPTION("Driver for Elan fingerprint product 0c00");