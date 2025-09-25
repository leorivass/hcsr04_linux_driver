# HC-SR04 Linux Kernel Driver

A Linux kernel driver for the HC-SR04 ultrasonic distance sensor using GPIO interrupts and character device interface.

## Features

- **Interrupt-driven measurements** using GPIO subsystem
- **Character device interface** for user-space communication
- **Timeout handling** to prevent hanging on failed measurements
- **Precise timing** using kernel high-resolution timers
- **Simple user-space API** through standard file operations

## Hardware Requirements

- HC-SR04 ultrasonic sensor
- Linux-compatible board (tested on a Raspberry PI 3B+)
- GPIO pins for trigger and echo connections

## Hardware Connections

| HC-SR04 Pin | GPIO Pin | Description |
|-------------|----------|-------------|
| VCC         | 5V/3.3V  | Power supply |
| GND         | GND      | Ground |
| Trig        | GPIO 4   | Trigger pin (output) |
| Echo        | GPIO 3   | Echo pin (input) |

> **Note:** The driver uses GPIO pins 4 and 3 with a 512 offset. Modify `TRIGGER_PIN`, `ECHO_PIN`, and `OFFSET_PIN` in the source code for your specific hardware configuration.

## Installation

### 1. Compile the driver
```bash
make
```

### 2. Load the kernel module
```bash
sudo insmod hcsr04_driver.ko
```

### 3. Compile test.c
```bash
gcc test.c
```

## Usage

### Reading distance measurements
```bash
# Method 1: Using the included user program example
./a.out

# Method 2: Direct reading from device file
cat /dev/hcsr04_1
```

### Expected output
```
Distance: 25cm
Distance: 30cm
Distance: 15cm
```

## How it Works

1. **Trigger pulse**: Driver sends a 10μs pulse to the trigger pin
2. **Echo measurement**: Interrupt handler measures the echo pulse duration
3. **Distance calculation**: Converts time to distance using speed of sound formula
4. **User interface**: Provides result through character device read operation

## Uninstalling

```bash
# Remove the kernel module
sudo rmmod hcsr04_driver
```

## Limitations

- **Single sensor support**: Driver is configured for one HC-SR04 sensor
- **Fixed GPIO pins**: GPIO pins are hardcoded in the source
- **No concurrent access protection**: Multiple simultaneous reads may interfere

## Future Improvements

- [ ] Device tree support for GPIO configuration
- [ ] Multiple sensor support
- [ ] Concurrent access protection with mutex
- [ ] Sysfs interface for configuration
- [ ] Error reporting improvements

## Contributing

Feel free to submit issues, fork the repository, and create pull requests for any improvements.

## License

This project is licensed under the GPL v2 - see the `MODULE_LICENSE("GPL")` declaration in the source code.

## Author

**Carlos Rivas** - [leorivas1805@gmail.com](mailto:leorivas1805@gmail.com)

---

## Troubleshooting

### Driver fails to load
- Check kernel logs: `dmesg | tail`
- Verify GPIO pins are available and not in use
- Ensure you have root privileges

### No distance readings
- Verify hardware connections
- Check GPIO pin configuration
- Ensure HC-SR04 has proper power supply (5V recommended)

### Permission denied on device file
```bash
sudo chmod 666 /dev/hcsr04_1
```
