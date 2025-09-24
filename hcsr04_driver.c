#include <linux/init.h>
#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/ktime.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/math64.h>

#define TRIGGER_PIN 4 
#define ECHO_PIN 3
#define OFFSET_PIN 512
#define TIMEOUT 50

static int echo_irq, ret;
static ktime_t start_time, end_time, duration_ns;
static s64 distance_cm;

static uint16_t major_number;
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
	.read = get_distance
};

static irqreturn_t echo_isr(int irq, void *dev_id) {

	uint8_t value = gpiod_get_value(echo);

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

	major_number = register_chrdev(0, "hcsr04_driver", &fops);

	if (major_number < 0) {
		pr_err("hcsr04_driver - Error getting a major number for the driver\n");
		ret = major_number;
		goto err_free_irq;
	}

	pr_info("hcsr04_driver %d - Driver initialized succesfully\n", major_number);

	return 0;

	err_free_irq:
		free_irq(echo_irq, NULL);
		return ret;
}

static void __exit hcsr04_exit(void) {

	free_irq(echo_irq, NULL);
	unregister_chrdev(major_number, "hcsr04_driver");
	pr_info("hcsr04_driver - Driver removed\n");

	return;
}

module_init(hcsr04_init);
module_exit(hcsr04_exit);

MODULE_AUTHOR("Carlos Rivas <leorivas1805@gmail.com>");
MODULE_DESCRIPTION("HCSR04 Ultrasonic Sensor driver");
MODULE_LICENSE("GPL");
