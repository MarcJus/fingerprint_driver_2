#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/fd.h>
#include <linux/wait.h>
#include <linux/mutex.h>

#define MODULE_NAME KBUILD_MODNAME

#define USB_FINGERPRINT_VENDOR_ID		0x04f3
#define USB_FINGERPRINT_PRODUCT_ID		0X0c00

struct fingerprint_skel {
	struct usb_device 		*udev;
	struct usb_interface 	*interface;
	struct urb				*in_urb;
	struct urb				*out_urb;
	struct semaphore		limit_sem;
	struct usb_anchor		submitted;
	struct kref				refcount;
	u8						*bulk_in_buffer;
	u8						*bulk_out_buffer;
	u8						last_bulk_in_endpoint;
	u8						last_bulk_out_endpoint;
	size_t					bulk_size;
	size_t					bulk_filled;
	size_t					bulk_copied;
	bool					ongoing_read;
	bool					open;
	struct mutex			io_mutex;
	wait_queue_head_t		bulk_wait;
	int 					disconnected:1;
};

#define to_fingerprint_dev(d) container_of(d, struct fingerprint_skel, refcount)

static struct usb_device_id fingerprint_usb_table[] = {
	{ USB_DEVICE(USB_FINGERPRINT_VENDOR_ID, USB_FINGERPRINT_PRODUCT_ID) },
	{}
};

static struct usb_driver fingerprint_usb_driver;

static void fingerprint_delete(struct kref *refcount){
	struct fingerprint_skel *dev;

	dev = to_fingerprint_dev(refcount);
	usb_free_urb(dev->in_urb);
	usb_free_urb(dev->out_urb);
	usb_put_intf(dev->interface);
	usb_put_dev(dev->udev);
	kfree(dev->bulk_in_buffer);
	kfree(dev->bulk_out_buffer);
	kfree(dev);
}

static void fingerprint_write_callback(struct urb *urb){
	struct fingerprint_skel *dev;

	dev = urb->context;

	if(urb->status){
		dev_err(&dev->interface->dev, "%s - nonzero write bulk status received: %d\n", 
		__func__, urb->status);
	}

	mutex_unlock(&dev->io_mutex);
	up(&dev->limit_sem);
}

static int fingerprint_open(struct inode *inode, struct file *file){

	struct fingerprint_skel *dev;
	struct usb_interface *interface;
	int ret = 0;
	int subminor;
	int current_ref;

	subminor = iminor(inode);

	interface = usb_find_interface(&fingerprint_usb_driver, subminor);
	if(!interface){
		pr_err(MODULE_NAME ": error : can't find device for minor %d\n", subminor);
		ret = -ENODEV;
		goto exit;
	}

	dev = usb_get_intfdata(interface);
	if(!dev){
		pr_err(MODULE_NAME ": error : can't get interface data\n");
		ret = -ENODEV;
		goto exit;
	}

	current_ref = kref_read(&dev->refcount);
	if(current_ref > 1)
		return -EBUSY;
	kref_get(&dev->refcount);

	file->private_data = dev;

	/*blocking file*/
	if(!(file->f_flags & O_NONBLOCK)){
		if(down_interruptible(&dev->limit_sem)){
			return -ERESTARTSYS;
			goto exit;
		}
	/*non-blocking file*/
	} else {
		/**/
		if(down_trylock(&dev->limit_sem)){
			return -EAGAIN;
			goto exit;
		}
	}

	dev->out_urb = usb_alloc_urb(0, GFP_KERNEL);
	if(!dev->out_urb){
		ret = -ENOMEM;
		goto error;
	}

	mutex_lock(&dev->io_mutex);
	if(dev->disconnected){
		/*device disconnected for unknown reason*/
		mutex_unlock(&dev->io_mutex);
		ret = -ENODEV;
		goto error;
	}

	dev->last_bulk_out_endpoint = 0x1;

	dev->bulk_out_buffer[0] = 0x40;
	dev->bulk_out_buffer[1] = 0xff;
	dev->bulk_out_buffer[2] = 0x03;

	usb_fill_bulk_urb(dev->out_urb, dev->udev, usb_sndbulkpipe(dev->udev, dev->last_bulk_out_endpoint),
		dev->bulk_out_buffer, 3, fingerprint_write_callback, dev);
	usb_anchor_urb(dev->out_urb, &dev->submitted);

	ret = usb_submit_urb(dev->out_urb, GFP_KERNEL);
	if(ret){
		dev_err(&dev->interface->dev, "%s - failed submitting urb, error %d\n",
			__func__, ret);
		goto error_unanchor;
	}

	usb_free_urb(dev->out_urb);

	goto exit;

error_unanchor:
	usb_unanchor_urb(dev->out_urb);
error:
	up(&dev->limit_sem);
exit:
	return ret;
}

static int fingerprint_release(struct inode *inode, struct file *file){

	struct fingerprint_skel *dev;
	int ret = 0;

	dev = file->private_data;
	if(!dev){
		return -ENODEV;
	}

	/*blocking file*/
	if(!(file->f_flags & O_NONBLOCK)){
		if(down_interruptible(&dev->limit_sem)){
			return -ERESTARTSYS;
			goto exit;
		}
	/*non-blocking file*/
	} else {
		/**/
		if(down_trylock(&dev->limit_sem)){
			return -EAGAIN;
			goto exit;
		}
	}

	dev->out_urb = usb_alloc_urb(0, GFP_KERNEL);
	if(!dev->out_urb){
		ret = -ENOMEM;
		goto error;
	}

	mutex_lock(&dev->io_mutex);
	if(dev->disconnected){
		/*device disconnected for unknown reason*/
		mutex_unlock(&dev->io_mutex);
		ret = -ENODEV;
		goto error;
	}

	dev->last_bulk_out_endpoint = 0x1;
	dev->bulk_out_buffer[0] = 0x40;
	dev->bulk_out_buffer[1] = 0xff;
	dev->bulk_out_buffer[2] = 0x02;

	usb_fill_bulk_urb(dev->out_urb, dev->udev, usb_sndbulkpipe(dev->udev, dev->last_bulk_out_endpoint),
		dev->bulk_out_buffer, 3, fingerprint_write_callback, dev);

	ret = usb_submit_urb(dev->out_urb, GFP_KERNEL);
	mutex_unlock(&dev->io_mutex);
	if(ret){
		dev_err(&dev->interface->dev, "%s - failed submitting urb, error %d\n",
			__func__, ret);
		goto error_unanchor;
	}

	usb_free_urb(dev->out_urb);

	goto exit;

error_unanchor:
	usb_unanchor_urb(dev->out_urb);
error:
	up(&dev->limit_sem);
exit:
	kref_put(&dev->refcount, fingerprint_delete);
	return ret;
}

static void fingerprint_read_callback(struct urb *urb){
	struct fingerprint_skel *dev;

	dev = urb->context;

	if(urb->status){
		dev_err(&dev->interface->dev, "%s - nonzero read bulk status reveived: %d\n",
			__func__, urb->status);
	} else {
		dev->bulk_filled = urb->actual_length;
	}

	dev->ongoing_read = 0;

	wake_up_interruptible(&dev->bulk_wait);
}

static int fingerprint_do_read_usb_request(struct fingerprint_skel *dev){

	int ret;

	usb_fill_bulk_urb(dev->in_urb, dev->udev,
			usb_rcvbulkpipe(dev->udev, 0x4),
			dev->bulk_in_buffer,
			2,
			fingerprint_read_callback,
			dev);

	dev->ongoing_read = 1;

	dev->bulk_copied = 0;
	dev->bulk_filled = 0;

	ret = usb_submit_urb(dev->in_urb, GFP_KERNEL);
	if(ret < 0){
		dev_err(&dev->interface->dev, 
			"%s - failed submitting read urb, error %d\n",
			__func__, ret);
		// ret = (ret == -ENOMEM) ? ret : -EIO;
		dev->ongoing_read = 0;
	}

	return ret;
}

static ssize_t fingerprint_read(struct file *file, char __user *buffer, size_t count, loff_t *off){

	struct fingerprint_skel *dev;
	int ret;
	bool ongoing_io;

	dev = file->private_data;

	if(!count)
		return 0;

	ret = mutex_lock_interruptible(&dev->io_mutex);
	if(ret < 0)
		return ret;

	if(dev->disconnected){
		ret = -ENODEV;
		goto exit;
	}

retry:
	ongoing_io = dev->ongoing_read;
	if(ongoing_io){
		if(file->f_flags & O_NONBLOCK){
			ret = -EAGAIN;
			goto exit;
		}
		ret = wait_event_interruptible(dev->bulk_wait, (!dev->ongoing_read));
		if(ret < 0)
			goto exit;
	}

	if(dev->bulk_filled){
		/*data left to copy*/
		size_t available = dev->bulk_filled - dev->bulk_copied;
		/*should not copy more than requested*/
		size_t chunk = min(available, count);

		/*no data left to copy*/
		if(!available){
			ret = fingerprint_do_read_usb_request(dev);
			if(ret < 0)
				/*error*/
				goto exit;
			else
				/*success*/ 
				goto retry;
		}

		if(copy_to_user(buffer, dev->bulk_in_buffer + dev->bulk_copied, chunk))
			ret = -EFAULT;
		else
			ret = chunk;

		dev->bulk_copied += chunk;
	} else {
		ret = fingerprint_do_read_usb_request(dev);
		if(ret < 0)
			goto exit;
		else
			goto retry;
	}

exit:
	mutex_unlock(&dev->io_mutex);
	return ret;
}

static int fingerprint_flush(struct file *file, fl_owner_t id){
	return 0;
}

char *fingerprint_devnode(struct device *dev, umode_t *mode){
	if(mode)
		*mode = 0644;
	return NULL;
}

struct file_operations fops = {
	.owner = THIS_MODULE,
	.open = fingerprint_open,
	.release = fingerprint_release,
	.read = fingerprint_read,
	.flush = fingerprint_flush,
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
	kref_init(&dev->refcount);

	dev->udev = usb_get_dev(interface_to_usbdev(interface));
	dev->interface = usb_get_intf(interface);

	/*64 = length of fingerprint*/
	dev->bulk_size = 64;
	dev->bulk_in_buffer = kmalloc(dev->bulk_size, GFP_KERNEL);
	if(!dev->bulk_in_buffer){
		ret = -ENOMEM;
		goto error;
	}

	/*3 = length of data sent to fingerprint reader*/
	dev->bulk_out_buffer = kmalloc(3, GFP_KERNEL);
	if(!dev->bulk_out_buffer){
		ret = -ENOMEM;
		goto error;
	}

	dev->in_urb = usb_alloc_urb(0, GFP_KERNEL);
	if(!dev->in_urb){
		ret = -ENOMEM;
		goto error;
	}

	usb_set_intfdata(interface, dev);

	if((ret = usb_register_dev(interface, &fingerprint_class_driver)) < 0){
		printk(KERN_ERR MODULE_NAME "Cannot get a minor for this device : %d", ret);
		usb_set_intfdata(interface, NULL);
		goto error;
	} else {
		printk(KERN_DEBUG MODULE_NAME ": Minor : %d", ret);
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

	usb_kill_urb(dev->in_urb);
	usb_kill_anchored_urbs(&dev->submitted);

	kref_put(&dev->refcount, fingerprint_delete);
}

static struct usb_driver fingerprint_usb_driver = {
	.name = MODULE_NAME,
	.id_table = fingerprint_usb_table,
	.probe = fingerprint_usb_probe,
	.disconnect = fingerprint_usb_disconnect,
	.supports_autosuspend = 0
};

static int __init module_fingerprint_init(void){
	int ret = 0;
	ret = usb_register(&fingerprint_usb_driver);
	if(ret){
		printk(KERN_INFO MODULE_NAME ": Error registering usb driver");
		return -ret;
	}
	return 0;
}

static void __exit module_fingerprint_exit(void){
	usb_deregister(&fingerprint_usb_driver);
}

module_init(module_fingerprint_init);
module_exit(module_fingerprint_exit);

// module_usb_driver(fingerprint_usb_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MarcJus");
MODULE_DESCRIPTION("Driver for Elan fingerprint product 0c00");
MODULE_VERSION("1.0.0");