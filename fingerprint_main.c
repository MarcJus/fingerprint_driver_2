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
	struct semaphore		limit_sem;
	struct usb_anchor		submitted;
	__u8					*bulk_buffer;
	size_t					bulk_size;
	size_t					bulk_filled;
	size_t					bulk_copied;
	bool					ongoing_read;
	bool					open;
	struct mutex			io_mutex;
	wait_queue_head_t		bulk_wait;
	int 					disconnected:1;
};

static struct usb_device_id fingerprint_usb_table[] = {
	{ USB_DEVICE(USB_FINGERPRINT_VENDOR_ID, USB_FINGERPRINT_PRODUCT_ID) },
	{}
};

struct file_operations fops = {
	.owner = THIS_MODULE,
	.open = fingerprint_open,
	.release = fingerprint_release,
	.read = fingerprint_read,
	.flush = fingerprint_flush
};

static struct usb_class_driver fingerprint_class_driver = {
	.name = MODULE_NAME,
	.fops = &fops,
	.devnode = fingerprint_devnode
};

static int fingerprint_usb_probe(struct usb_interface *interface, const struct usb_device_id *id){
	int ret;
	struct fingerprint_skel *dev;
	
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if(!dev)
		return -ENOMEM;

	mutex_init(&dev->io_mutex);
	init_waitqueue_head(&dev->bulk_wait);
	sema_init(&dev->limit_sem, 1);
	init_usb_anchor(&dev->submitted);

	dev->udev = usb_get_dev(interface_to_usbdev(interface));
	dev->interface = usb_get_intf(interface);

	usb_set_intfdata(interface, dev);

	if((ret = usb_register_dev(interface, &fingerprint_class_driver)) < 0){
		printk(KERN_ERR MODULE_NAME "Cannot get a minor for this device : %d", ret);
		usb_set_intfdata(interface, NULL);
		goto error;
	} else {
		printk(KERN_DEBUG MODULE_NAME " - Minor : %d", ret);
	}

	mutex_lock(&dev->io_mutex);
	dev->disconnected = 0;
	mutex_unlock(&dev->io_mutex);

	error:
		return ret;
}

static void fingerprint_usb_disconnect(struct usb_interface *interface){
	struct fingerprint_skel *dev;

	dev = usb_get_intfdata(interface);

	usb_deregister_dev(interface, &fingerprint_class_driver);

	mutex_lock(&dev->io_mutex);
	dev->disconnected = 1;
	mutex_unlock(&dev->io_mutex);

	usb_kill_urb(dev->bulk_urb);
	usb_kill_anchored_urbs(&dev->submitted);

	usb_free_urb(dev->bulk_urb);
	usb_put_intf(dev->interface);
	usb_put_dev(dev->udev);
	kfree(dev->bulk_buffer);
	kfree(dev);

	pr_info(MODULE_NAME " - device disconnected");
}

static struct usb_driver fingerprint_usb_driver = {
	.name = MODULE_NAME,
	.id_table = fingerprint_usb_table,
	.probe = fingerprint_usb_probe,
	.disconnect = fingerprint_usb_disconnect
};

static int __init module_fingerprint_init(void){
	int ret = 0;
	printk(KERN_INFO MODULE_NAME " - initiating");
	ret = usb_register(&fingerprint_usb_driver);
	if(ret){
		printk(KERN_INFO MODULE_NAME " - Error registering usb driver");
		return -ret;
	}
	printk(KERN_INFO MODULE_NAME " - Usb driver registered successfully");
	return 0;
}

static void __exit module_fingerprint_exit(void){
	printk(KERN_INFO MODULE_NAME " - exit");
	usb_deregister(&fingerprint_usb_driver);
}

module_usb_driver(fingerprint_usb_driver);

module_init(module_fingerprint_init);
module_exit(module_fingerprint_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MarcJus");
MODULE_DESCRIPTION("Driver for Elan fingerprint product 0c00");