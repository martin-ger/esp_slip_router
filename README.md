# esp_slip_router
A SLIP to WiFi router

This is a proof of concept implementation of a SLIP (RFC1055) router on the esp8266. It can be used as simple (and slow) network interface to get WiFi connectivity. The esp also acts as STA and transparently forwards any IP traffic through it. As it uses NAT no routing entries are required on the network side. 

# Usage
The Firmware starts with the following default configuration:
- ssid: ssid, pasword: password
- slip interface address: 192.168.240.1

This means it connects to the internet via AP ssid,password and offer at the UART0 a SLIP interface with IP address 192.168.240.1. This default can be changed in the file user_config.h. 

To connect a linux-based host, start the firmware on the esp, connect it via serial to USB, and use the following commands on the host:
```
sudo slattach -p slip -s 115200 /dev/ttyUSB0&
sudo ifconfig sl0 192.168.240.2 pointopoint 192.168.240.1 up mtu 1500
```
now 
```
telnet 192.168.240.1 7777
```
should give you terminal access to the esp as router.

To get full internet access you will nees aditionally a route:
```
sudo sudo route add default gw 192.168.240.1
```
and a DNS server - add an appropriate entry (e.g. the one that "show" tells you on the router console) in /etc/resolv.conf, eg. by:
```
sudo echo "nameserver 192.168.178.1" > /etc/resolv.conv
```
A script may help to automize this process.

The default config of the router can be overwritten and persistenly saved to flash by using a console interface. This console is available via tcp port 7777 (e.g. from the host attached to the serial line - see above). 

The console understands the following command:
- help: prints a short help message
- show: prints the current config and status
- set ssid|pasword [value]: changes the named config parameter
- set addr [ip-addr]: sets the IP address of the SLIP interface
- set speed [80|160]: sets the CPU clock frequency
- save: saves the current parameters to flash
- quit: terminates a remote session
- reset [factory]: resets the esp and applies the config, optionally resets WiFi params to default values
- lock: locks the current config, changes are not allowed
- unlock [password]: unlocks the config, requires password of the network AP
- scan: does a scan for APs

# Building and Flashing
To build this binary you download and install the esp-open-sdk (https://github.com/pfalcon/esp-open-sdk) and my version of the esp-open-lwip library (https://github.com/martin-ger/esp-open-lwip). Replace that in the esp-open-sdk tree. "make clean" in the esp_open_lwip dir and once again a "make" in the upper esp_open_sdk will do the job. This installs a new version of the liblwip_open.a that contains the SLIP interface and the NAT features.

Then adjust the BUILD_AREA variable in the Makefile and any desired options in user/user_config.h.

Build the esp_wifi_repeater firmware with "make". "make flash" flashes it onto an esp8266.

If you want to use the precompiled binaries you can flash them with "esptool.py --port /dev/ttyUSB0 write_flash -fs 32m 0x00000 firmware/0x00000.bin 0x10000 firmware/0x10000.bin" (use -fs 8m for an ESP-01)

# Softuart UART
As UART0, the HW UART of the esp8266 is busy with the SLIP protocoll, it cannot be used simultaniuosly as debugging output. This is highly uncomfortable especially during development. If you define DEBUG_SOFTUART in user_config.h, a second UART will be simulated in software (Rx GPIO 14, Tx GPIO 12, 19200 baud). All debug output (os_printf) will then be redirectd to this port.

# Known Issues
- Speed: 115200 is the max baudrate on may USB ports and the current standard speed. This is SLOW compared to the typical WiFi speeds. This means connectivity via the serial line works, even basic web browsing, but the speed is what you can expect from about 100kB/s... But IoT applications typically use much less bandwidth, also terminal access is fine.
- If you are just interested in the SLIP interface as a basis for you own projects, you might have a look into the user_simple directory. It contains a minimal version of the router with no config console.
