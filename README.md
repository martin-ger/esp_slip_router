# esp_slip_router
A SLIP to WiFi router

This is a proof of concept implementation of a SLIP (Serial Line IP - RFC1055) router on the esp8266. It can be used as simple (and slow) network interface to get WiFi connectivity. The esp also acts as STA and transparently forwards any IP traffic through it. As it uses NAT no routing entries are required on the network side. 

# Usage
The Firmware starts with the following default configuration:
- ssid: ssid, pasword: password
- slip interface address: 192.168.240.1

This means it connects to the internet via AP ssid,password and offers at UART0 a SLIP interface with IP address 192.168.240.1. This default can be changed in the file user_config.h. 

To connect a Linux-based host, start the firmware on the esp, connect it via serial to USB, and use the following commands on the host:
```
sudo slattach -p slip -s 115200 /dev/ttyUSB0&
sudo ifconfig sl0 192.168.240.2 pointopoint 192.168.240.1 up mtu 1500
```
now 
```
telnet 192.168.240.1 7777
```
should give you terminal access to the esp as router.

To get full internet access you will need aditionally a route:
```
sudo route add default gw 192.168.240.1
```
and a DNS server - add an appropriate entry (e.g. the one that "show" tells you on the router console) in /etc/resolv.conf, eg. by (as root):
```
echo "nameserver 192.168.178.1" > /etc/resolv.conf
```
A script may help to automize this process.

The default config of the router can be overwritten and persistenly saved to flash by using a console interface. This console is available via tcp port 7777 (e.g. from the host attached to the serial line - see above). 

The console understands the following command:
- help: prints a short help message
- show: prints the current config and status
- set ssid|pasword [value]: changes the named config parameter
- set addr [ip-addr]: sets the IP address of the SLIP interface
- set speed [80|160]: sets the CPU clock frequency
- set bitrate [bitrate]: sets the serial bitrate to a new value
- save: saves the current parameters to flash
- quit: terminates a remote session
- reset [factory]: resets the esp and applies the config, optionally resets WiFi params to default values
- lock: locks the current config, changes are not allowed
- unlock [password]: unlocks the config, requires password of the network AP
- scan: does a scan for APs

# Usage as AP
You can also turn the sides and make the ESP to work as AP - useful e.g. if you want to connect other devices to a RasPi that has no WiFi interface:

With a Linux-based host start the firmware on the esp, connect it via serial to USB, and use the following commands on the host:
```
sudo slattach -p slip -s 115200 /dev/ttyUSB0&
sudo ifconfig sl0 192.168.240.2 pointopoint 192.168.240.1 up mtu 1500
sudo route add -net 192.168.4.0/24 gw 192.168.240.1
```
again 
```
telnet 192.168.240.1 7777
```
should give you terminal access to the ESP as router. On the ESP you then enter:

```
CMD>set ap_ssid <your_ssid> 
CMD>set ap_password <your_pw> 
CMD>set use_ap 1
CMD>save
CMD>reset
```

Now STAs of this AP get IP adresses in the 192.168.4.0/24 network and can reach the Linux machine as "192.168.240.2". The AP interface is NATed, i.e. you cannot set up connections from the Linux machine to the STAs, but from STAs to Linux works. Useful e.g. if you want to reach an MQTT server on the Linux. DNS currently doesn't work here.

The console understands the following command for the AP mode:
- set use_ap [0|1]: selects, whether the esp_slip_router uses an STA interface (use_ap = 0, default) or an AP interface (use_ap = 1)
- set [ap_ssid|ap_password] _value_: changes the settings for the soft-AP of the ESP (for your stations)
- set ap_channel [1-13]: sets the channel of the SoftAP (default 1)
- set ap_open [0|1]: selects, whether the soft-AP uses WPA2-PSK security (ap_open=0,  automatic, if an ap_password is set) or open (ap_open=1)
- set ssid_hidden [0|1]: selects, whether the SSID of the soft-AP is hidden (ssid_hidden=1) or visible (ssid_hidden=0, default)
- set max_clients [1-8]: sets the number of STAs that can connct to the SoftAP (limit of the ESP's SoftAP implementation is 8, default)


# Building and Flashing
To build this binary you download and install the esp-open-sdk (https://github.com/pfalcon/esp-open-sdk). Make sure, you can compile and download the included "blinky" example.

Then download this source tree in a separate directory and adjust the BUILD_AREA variable in the Makefile and any desired options in user/user_config.h. Build the esp_wifi_repeater firmware with "make". "make flash" flashes it onto an esp8266.

The source tree includes a binary version of the liblwip_open plus the required additional includes from my fork of esp-open-lwip. *No additional install action is required for that.* Only if you don't want to use the precompiled library, checkout the sources from https://github.com/martin-ger/esp-open-lwip . Use it to replace the directory "esp-open-lwip" in the esp-open-sdk tree. "make clean" in the esp_open_lwip dir and once again a "make" in the upper esp_open_sdk directory. This will compile a liblwip_open.a that contains the NAT-features. Replace liblwip_open_napt.a with that binary.

If you want to use the precompiled binaries you can flash them with "esptool.py --port /dev/ttyUSB0 write_flash -fs 32m 0x00000 firmware/0x00000.bin 0x10000 firmware/0x10000.bin" (use -fs 8m for an ESP-01)

# Softuart UART
As UART0, the HW UART of the esp8266 is busy with the SLIP protocoll, it cannot be used simultaniuosly as debugging output. This is highly uncomfortable especially during development. If you define DEBUG_SOFTUART in user_config.h, a second UART will be simulated in software (Rx GPIO 14, Tx GPIO 12, 19200 baud). All debug output (os_printf) will then be redirectd to this port.

# Known Issues
- Speed: 115200 is the max baudrate on may USB ports and the current standard speed. This is SLOW compared to the typical WiFi speeds. This means connectivity via the serial line works, even basic web browsing, but the speed is what you can expect from about 100kB/s... But IoT applications typically use much less bandwidth, also terminal access is fine.
- If you are just interested in the SLIP interface as a basis for you own projects, you might have a look into the user_simple directory. It contains a minimal version of the router with no config console.
