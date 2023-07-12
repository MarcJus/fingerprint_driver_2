#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/fd.h>
#include <linux/wait.h>
#include <linux/mutex.h>

#include "fingerprint_file.h"

#define MODULE_NAME KBUILD_MODNAME

#define USB_FINGERPRINT_VENDOR_ID		0x04f3
#define USB_FINGERPRINT_PRODUCT_ID		0X0c00

struct fingerprint_skel {
	struct usb_device 		*udev;
	struct usb_interface 	*interface;
	struct urb				*bulk_urb;
	__u8					*bulk_buffer;
	size_t					bulk_size;
	size_t					bulk_filled;
	size_t					bulk_copied;
	bool					ongoing_read;
	bool					open;
	struct mutex			io_mutex;
	wait_queue_head_t		bulk_wait;
};

static struct usb_device_id fingerprint_usb_table[] = {
	{ USB_DEVICE(USB_FINGERPRINT_VENDOR_ID, USB_FINGERPRINT_PRODUCT_ID) },
	{}
};



static char *fingerprint_devnode(struct device *dev, umode_t *mode){
	if(mode)
		*mode = 0644;
	return NULL;
}

struct file_operations fops = {
	.owner = THIS_MODULE,
	.open = fingerprint_open,
	.release = fingerprint_release,
	.read = fingerprint_read
};

static struct usb_class_driver fingerprint_class_driver = {
	.name = MODULE_NAME,
	.fops = &fops,
	.devnode = fingerprint_devnode
};

static int fingerprint_usb_probe(struct usb_interface *interface, const struct usb_device_id *id){
	int ret;
	// struct fingerprint_skel *dev;
	
	// dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	// if(!dev)
	// 	return -ENOMEM;

	// mutex_init(&dev->io_mutex);
	// init_waitqueue_head(&dev->bulk_wait);

	// dev->udev = usb_get_dev(interface_to_usbdev(interface));
	// dev->interface = usb_get_intf(interface);

	if((ret = usb_register_dev(interface, &fingerprint_class_driver)) < 0){
		printk(KERN_ERR MODULE_NAME "Cannot get a minor for this device : %d", ret);
		return ret;
	} else {
		printk(MODULE_NAME " - Minor : %d", ret);
	}
	return 0;
}

static void fingerprint_usb_disconnect(struct usb_interface *interface){
	usb_deregister_dev(interface, &fingerprint_class_driver);
}

static struct usb_driver fingerprint_usb_driver = {
	.name = MODULE_NAME,
	.id_table = fingerprint_usb_table,
	.probe = fingerprint_usb_probe,
	.disconnect = fingerprint_usb_disconnect
};

static int __init module_fingerprint_init(void){
	int ret = 0;
	printk(MODULE_NAME " - initiating");
	ret = usb_register(&fingerprint_usb_driver);
	if(ret){
		printk(MODULE_NAME " - Error registering usb driver");
		return -ret;
	}
	printk(MODULE_NAME " - Usb driver registered successfully");
	return 0;
}

static void __exit module_fingerprint_exit(void){
	printk(MODULE_NAME " - exit");
	usb_deregister(&fingerprint_usb_driver);
}

module_init(module_fingerprint_init);
module_exit(module_fingerprint_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MarcJus");
MODULE_DESCRIPTION("Driver for Elan fingerprint product 0c00");