#include <linux/init.h>
#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/ktime.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/math64.h>

#define DEVICE_NAME "hcsr04_1"
#define CLASS_NAME  "hcsr04"

#define TRIGGER_PIN 4 
#define ECHO_PIN 3
#define OFFSET_PIN 512
#define TIMEOUT 50

static dev_t devt;
static struct cdev hcsr04_cdev;
static struct class *hcsr04_class;
static struct device *hcsr04_device;

static int echo_irq, ret;
static ktime_t start_time, end_time, duration_ns;
static s64 distance_cm;

static struct gpio_desc *trigger, *echo;

static DECLARE_WAIT_QUEUE_HEAD(echo_wq);
static bool pulse_ready;

static ssize_t get_distance(struct file *filp, char __user *user_buffer, size_t len, loff_t *off) {
    char buffer[64];
    int buffer_len, not_copied, to_copy;

    if (*off > 0) {
        *off = 0;
        return 0;
    }

    gpiod_set_value(trigger, 1);
    udelay(10);
    gpiod_set_value(trigger, 0);

	/* 
 	 *	wait_event_interruptible_timeout() blocks the process until the interrupt handler sets pulse_ready to true or timeout expires.
	 * 	This avoids busy-waiting and allows other processes to run while we wait for the echo pulse measurement to complete.
	 */
	
    pulse_ready = false;
    wait_event_interruptible_timeout(echo_wq, pulse_ready, msecs_to_jiffies(TIMEOUT));

    if (!pulse_ready) 
        return -ETIMEDOUT;

    distance_cm = div64_s64(duration_ns, 58000ULL);

    if (distance_cm < 0 || distance_cm > 400) {
        pr_err("hcsr04_driver - distance out of range! value = %lld\n", distance_cm);
        return -ERANGE;
    }

    buffer_len = snprintf(buffer, sizeof(buffer), "%lldcm\n", distance_cm);

    to_copy = min(len, (size_t)(buffer_len + 1));

    not_copied = copy_to_user(user_buffer, buffer, to_copy);
    
    if (not_copied > 0) 
        return -EFAULT;

    *off += to_copy;

    return to_copy;
}

static struct file_operations fops = {
	.owner = THIS_MODULE,
	.read = get_distance
};

static irqreturn_t echo_isr(int irq, void *dev_id) {

	uint8_t value = gpiod_get_value(echo);

	/*
	 * 	After trigger pulse, echo pin goes HIGH when ultrasonic burst starts. Echo pin goes low when reflected signal returns.
	 *	The pulse duration is equal to the difference between the time at the end of the pulse and the time at the start of it.
	 *	ktime_get() function gets the exact time in nanoseconds.
	 */

	if (value) {
		start_time = ktime_get();
	}
	else {
		end_time = ktime_get();

		duration_ns = ktime_to_ns(ktime_sub(end_time, start_time));

		pulse_ready = true;
		wake_up_interruptible(&echo_wq);
	}

	return IRQ_HANDLED;
}

static int __init hcsr04_init(void) {

	trigger = gpio_to_desc(TRIGGER_PIN + OFFSET_PIN);

	if (!trigger) {
		pr_err("hcsr04_driver - Error getting pin 4\n");
		return -ENODEV;
	}

	echo = gpio_to_desc(ECHO_PIN + OFFSET_PIN);

	if (!echo) {
		pr_err("hcsr04_driver - Error getting pin 3\n");
		return -ENODEV;
	}

	ret = gpiod_direction_output(trigger, 0);

	if (ret) {
		pr_err("hcsr04_driver - Error setting pin 4 to output\n");
		return ret;
	}

	ret = gpiod_direction_input(echo);

	if (ret) {
		pr_err("hcsr04_driver - Error setting pin 3 to input\n");
		return ret;
	}

	echo_irq = gpiod_to_irq(echo);

	if (echo_irq < 0) {
		pr_err("hcsr04_driver - Error getting an IRQ number for the ECHO pin\n");
		return echo_irq;
	}

	ret = request_irq(echo_irq, echo_isr, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "echo_irq_handler", NULL);

	if (ret) {
		pr_err("hcsr04_driver - Error registering an IRQ for the ECHO pin\n");
		goto err_free_irq;
	}

	init_waitqueue_head(&echo_wq);

	/*
	 *	The next function allocates the major and minor number for the new device. It takes four parameters: (dev_t *dev, unsigned baseminor, unsigned count, const char *name))
	 *		- *dev: the kernel uses this pointer to return the major and minor number reserved
	 *		-  baseminor: specifies the first minor number in the range of devices to be reserved
	 *		-  count: is for the amount of devices that we want to add
	 *		- *name: takes the macro we declared at the beginning of the code
	 */

	ret = alloc_chrdev_region(&devt, 0, 1, DEVICE_NAME);
	
	if (ret < 0) {
		pr_err("hcsr04_driver - Error reserving major and minor numbers for the character device\n");
		goto err_free_irq;
	}

	/*
	 * 	Initialize the character device. These functions do not create the /dev file, but register the device with the kernel.
	 *  	- cdev_init(): links the cdev instance with our file_operations, so the kernel knows which operations are available.
	 *   	- cdev_add(): registers the character device with the kernel, but does not create the device node in /dev.
	 */

	cdev_init(&hcsr04_cdev, &fops);
	hcsr04_cdev.owner = THIS_MODULE;
	ret = cdev_add(&hcsr04_cdev, devt, 1);

	if (ret < 0) {
		pr_err("hcsr04_driver - The character device file could not be registered\n");
		goto err_unregister_chrdev_region;
	}

	/*
	 *	This class_create() function creates a class to organize devices and provide a framework for device management in /sys/class
	 */
	 
	hcsr04_class = class_create(CLASS_NAME);

	if (IS_ERR(hcsr04_class)) {
		pr_err("hcsr04_driver - Error creating a class for the device\n");
		ret = PTR_ERR(hcsr04_class);
		goto err_cdev_del;
	}

	/*
	 *	The next device_create() function creates the node in /dev taking five parameters: (struct class *cls, struct device *parent, dev_t devt, void *drvdata, const char *fmt, ...);
	 *		*cls: this pointer stores our previous created class, grouping multiple devices in sysfs (/sys/class/<classname>)
	 *		*parent: pointer to parent device (NULL in this case)
	 *		 devt: major and minor numbers reserved
	 *		*drvdata: private data associated to the device (NULL in this case)
	 *		*fmt, ...: name of the character device file that will be shown in /dev 
	 */

	hcsr04_device = device_create(hcsr04_class, NULL, devt, NULL, DEVICE_NAME);

	if(IS_ERR(hcsr04_device)) {
		pr_err("hcsr04_driver - Error creating the character device file\n");
		ret = PTR_ERR(hcsr04_device);
		goto err_class_destroy;
	}

	pr_info("hcsr04_driver %d - Driver initialized succesfully\n", MAJOR(devt));

	return 0;

	/* ~ Tags for handling errors ~ */
	
	err_free_irq:
		free_irq(echo_irq, NULL);
		return ret;
	err_unregister_chrdev_region:
		unregister_chrdev_region(devt, 1);
		free_irq(echo_irq, NULL);
		return ret;
	err_cdev_del:
		cdev_del(&hcsr04_cdev);
		unregister_chrdev_region(devt, 1);
		free_irq(echo_irq, NULL);
		return ret;
	err_class_destroy:
		class_destroy(hcsr04_class);
		cdev_del(&hcsr04_cdev);
		unregister_chrdev_region(devt, 1);
		free_irq(echo_irq, NULL);
		return ret;	
}

static void __exit hcsr04_exit(void) {

	device_destroy(hcsr04_class, devt);
	class_destroy(hcsr04_class);
	cdev_del(&hcsr04_cdev);
	unregister_chrdev_region(devt, 1);
	free_irq(echo_irq, NULL);
	
	pr_info("hcsr04_driver - Driver removed\n");

	return;
}

module_init(hcsr04_init);
module_exit(hcsr04_exit);

MODULE_AUTHOR("Carlos Rivas <leorivas1805@gmail.com>");
MODULE_DESCRIPTION("HCSR04 Ultrasonic Sensor driver");
MODULE_LICENSE("GPL");
