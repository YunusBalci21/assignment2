#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/cdev.h>
#include "ioctl_commands.h" 

#define MIN_MINOR_NUMBER 0
#define MAX_MINOR_NUMBER 1
#define DEVICE_NAME "dm510"
#define MAX_BUFFER_SIZE (1024 * 1024) // 1MB max buffer size

static int dm510_major;

struct dm510_buffer {
    char *data;
    size_t size;
    int begin, end, used;
};

struct dm510_device {
    struct dm510_buffer buffer;
    struct mutex mutex;
    wait_queue_head_t read_queue, write_queue;
    struct cdev cdev;
};

static struct dm510_device devices[DEVICE_COUNT];

static ssize_t dm510_read(struct file *filep, char __user *buf, size_t count, loff_t *f_pos) {
    struct dm510_device *device = filep->private_data;
    ssize_t result = 0;

    if (mutex_lock_interruptible(&device->mutex))
        return -ERESTARTSYS;

    // Wait for data to be available
    while (device->buffer.used == 0) {
        mutex_unlock(&device->mutex); // Release lock while waiting
        if (filep->f_flags & O_NONBLOCK)
            return -EAGAIN; // Non-blocking read
        if (wait_event_interruptible(device->read_queue, device->buffer.used > 0))
            return -ERESTARTSYS; // Interrupted while waiting
        if (mutex_lock_interruptible(&device->mutex))
            return -ERESTARTSYS;
    }

    // Handle reading with potential wrap-around
    while (count > 0 && device->buffer.used > 0) {
        size_t read_chunk = min((size_t)count, (size_t)device->buffer.used);
        size_t to_end = device->buffer.size - device->buffer.begin; // Distance to the end of the buffer
        read_chunk = min(read_chunk, to_end);

        if (copy_to_user(buf, device->buffer.data + device->buffer.begin, read_chunk)) {
            result = -EFAULT;
            break;
        }

        device->buffer.begin = (device->buffer.begin + read_chunk) % device->buffer.size;
        device->buffer.used -= read_chunk;
        result += read_chunk;
        buf += read_chunk;
        count -= read_chunk;
    }

    mutex_unlock(&device->mutex);
    wake_up_interruptible(&device->write_queue); // Wake up any waiting writers
    return result;
}

static ssize_t dm510_write(struct file *filep, const char __user *buf, size_t count, loff_t *f_pos) {
    struct dm510_device *device = filep->private_data;
    ssize_t result = 0;

    if (mutex_lock_interruptible(&device->mutex))
        return -ERESTARTSYS;

    while (count > 0) {
        if (device->buffer.used == device->buffer.size) {
            mutex_unlock(&device->mutex);
            if (filep->f_flags & O_NONBLOCK)
                return -EAGAIN;
            if (wait_event_interruptible(device->write_queue, device->buffer.used < device->buffer.size))
                return -ERESTARTSYS;
            if (mutex_lock_interruptible(&device->mutex))
                return -ERESTARTSYS;
        }

        size_t space_left = device->buffer.size - device->buffer.used;
        size_t to_end = device->buffer.size - device->buffer.end;
        size_t write_chunk = min(min(count, space_left), to_end);

        if (copy_from_user(device->buffer.data + device->buffer.end, buf + result, write_chunk)) {
            if (result == 0) // If no bytes were written yet
                result = -EFAULT; // Only return an error if nothing was written
            break;
        }

        device->buffer.end = (device->buffer.end + write_chunk) % device->buffer.size;
        device->buffer.used += write_chunk;
        result += write_chunk;
        count -= write_chunk;
    }

    mutex_unlock(&device->mutex);
    if (device->buffer.used < device->buffer.size)
        wake_up_interruptible(&device->write_queue); // Wake up any waiting writers

    return result; // Return the number of bytes written
}

static int dm510_open(struct inode *inode, struct file *filep) {
    int minor = iminor(inode);
    struct dm510_device *device = &devices[minor];

    filep->private_data = device;
    return nonseekable_open(inode, filep);
}

static int dm510_release(struct inode *inode, struct file *filep) {
    return 0;
}

static long dm510_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    struct dm510_device *device = filp->private_data;
    long retval = 0;
    int tmp;
    size_t free_space, used_space;

    switch (cmd) {
        case GET_BUFFER_SIZE:
            if (copy_to_user((int __user *)arg, &device->buffer.size, sizeof(device->buffer.size)))
                return -EFAULT;
            break;

        case SET_BUFFER_SIZE:
            if (copy_from_user(&tmp, (int __user *)arg, sizeof(tmp)))
                return -EFAULT;
            if (tmp < 1 || tmp > MAX_BUFFER_SIZE)
                return -EINVAL;
            mutex_lock(&device->mutex);
            kfree(device->buffer.data);
            device->buffer.data = kmalloc(tmp, GFP_KERNEL);
            if (!device->buffer.data) {
                retval = -ENOMEM;
                break;
            }
            device->buffer.size = tmp;
            device->buffer.begin = 0;
            device->buffer.end = 0;
            device->buffer.used = 0;
            mutex_unlock(&device->mutex);
            break;

        case GET_MAX_NR_PROCESSES:
            // This example assumes a fixed limit; modify as needed
            tmp = DEVICE_COUNT;
            if (copy_to_user((int __user *)arg, &tmp, sizeof(tmp)))
                return -EFAULT;
            break;

        case SET_MAX_NR_PROCESSES:
            // Ignored or log a warning; typically, this is not dynamically adjustable but fixed
            printk(KERN_WARNING "DM510: SET_MAX_NR_PROCESSES not supported.\n");
            break;

        case GET_BUFFER_FREE_SPACE:
            mutex_lock(&device->mutex);
            free_space = device->buffer.size - device->buffer.used;
            mutex_unlock(&device->mutex);
            if (copy_to_user((size_t __user *)arg, &free_space, sizeof(free_space)))
                return -EFAULT;
            break;

        case GET_BUFFER_USED_SPACE:
            mutex_lock(&device->mutex);
            used_space = device->buffer.used;
            mutex_unlock(&device->mutex);
            if (copy_to_user((size_t __user *)arg, &used_space, sizeof(used_space)))
                return -EFAULT;
            break;

        default:
            return -ENOTTY;
    }

    return retval;
}

// File operations structure
static struct file_operations dm510_fops = {
    .owner = THIS_MODULE,
    .read = dm510_read,
    .write = dm510_write,
    .open = dm510_open,
    .release = dm510_release,
    .unlocked_ioctl = dm510_ioctl,
};

static int __init dm510_init_module(void) {
    dev_t dev_num;
    int i, ret;

    ret = alloc_chrdev_region(&dev_num, MIN_MINOR_NUMBER, DEVICE_COUNT, DEVICE_NAME);
    if (ret < 0) {
        printk(KERN_WARNING "DM510: Can't allocate device number\n");
        return ret;
    }
    dm510_major = MAJOR(dev_num);

    for (i = 0; i < DEVICE_COUNT; i++) {
        cdev_init(&devices[i].cdev, &dm510_fops);
        devices[i].cdev.owner = THIS_MODULE;
        mutex_init(&devices[i].mutex);
        init_waitqueue_head(&devices[i].read_queue);
        init_waitqueue_head(&devices[i].write_queue);
        devices[i].buffer.data = kmalloc(MAX_BUFFER_SIZE, GFP_KERNEL);
        devices[i].buffer.size = MAX_BUFFER_SIZE;
        devices[i].buffer.begin = 0;
        devices[i].buffer.end = 0;
        devices[i].buffer.used = 0;
        ret = cdev_add(&devices[i].cdev, MKDEV(dm510_major, i), 1);
        if (ret) {
            printk(KERN_NOTICE "DM510: Error %d adding dm510-%d", ret, i);
            for (--i; i >= 0; i--) {
                cdev_del(&devices[i].cdev);
            }
            unregister_chrdev_region(MKDEV(dm510_major, 0), DEVICE_COUNT);
            return ret;
        }
    }
    printk(KERN_INFO "DM510: Module loaded with device major number %d\n", dm510_major);
    return 0;
}

static void __exit dm510_cleanup_module(void) {
    int i;
    for (i = 0; i < DEVICE_COUNT; i++) {
        if (devices[i].buffer.data)
            kfree(devices[i].buffer.data);
        cdev_del(&devices[i].cdev);
    }
    unregister_chrdev_region(MKDEV(dm510_major, 0), DEVICE_COUNT);
    printk(KERN_INFO "DM510: Module unloaded\n");
}

module_init(dm510_init_module);
module_exit(dm510_cleanup_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("DM510 Character Device Module");
