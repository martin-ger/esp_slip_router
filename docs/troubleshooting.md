# Troubleshooting Guide

This guide documents problems encountered during
the development of the ESP8266 Reverse SLIP Router.

## 1. Serial device not found

List available persistent USB serial identifiers:

```bash
ls -l /dev/serial/by-id/
```

Check whether the CH340 was detected:

```bash
lsusb
```

When using VirtualBox, ensure USB passthrough is enabled.

## 2. Serial port already in use

Check which process is using the serial device:

```bash
sudo lsof /dev/ttyUSB0
```

Only the intended slattach process should control
the serial connection.

Do not run manual and automated SLIP connections
simultaneously.

## 3. Systemd service not working

Check the service status:

```bash
sudo systemctl status esp-slip-router --no-pager -l
```

Inspect the service logs:

```bash
sudo journalctl -u esp-slip-router -b --no-pager
```

## 4. Service running but no internet after reboot

Check the UART configuration:

```bash
sudo stty -F /dev/ttyUSB0 -a
```

Expected configuration:

- 115200 baud
- 8 data bits
- No parity
- One stop bit
- Hardware flow control disabled

During prototype development, the serial interface
reported 0 baud after reboot.

Explicit UART configuration before starting slattach
resolved the observed problem.

## 5. ESP8266 does not respond to ping

Test the SLIP connection:

```bash
ping -c 10 192.168.240.1
```

Check interface addressing and statistics:

```bash
ip -4 addr show dev sl0
ip -s link show sl0
```

Expected configuration:

- Local IP: 192.168.240.2
- Peer IP: 192.168.240.1
- MTU: 1500
- Interface status: UP

## 6. Invalid IP packets

Capture traffic on the SLIP interface:

```bash
sudo timeout 20 tcpdump -ni sl0 -vv -X
```

Messages such as "unknown ip 0" or "unknown ip 5"
may indicate corrupted serial data or incorrect
UART configuration.

Verify the baud rate and serial parameters before
rebuilding or flashing the firmware.

## 7. Local connectivity works but internet fails

Connect to the ESP8266 console:

```bash
nc 192.168.240.1 7777
```

Enter:

```text
show stats
```

Expected AP-mode interface configuration:

```text
IF ew0 IP: 192.168.4.1
GW: 192.168.4.1 DEFAULT: 0 NAT: 0

IF sl0 IP: 192.168.240.1
GW: 192.168.240.2 DEFAULT: 1 NAT: 0
```

If ew0 is the default interface instead of sl0,
verify that the corrected firmware is installed.

## 8. Missing Linux return route

Inspect the routing table:

```bash
ip route
```

The following route should be present:

```text
192.168.4.0/24 via 192.168.240.1 dev sl0
```

## 9. IPv4 forwarding or NAT failure

Check IPv4 forwarding:

```bash
sysctl net.ipv4.ip_forward
```

Expected result:

```text
net.ipv4.ip_forward = 1
```

Inspect the NAT and forwarding rules:

```bash
sudo iptables -t nat -S
sudo iptables -S FORWARD
```

Verify that the configured WAN interface is correct.

Existing firewall policies may require additional
configuration. Do not flush the existing firewall.

## 10. DNS problems

Test internet access using an external IP address:

```text
http://1.1.1.1
```

Then test a domain name:

```text
https://www.google.com
```

If IP-based access works but domain names fail,
investigate DNS configuration.

The prototype used 1.1.1.1 as the DNS server
distributed to wireless clients.

## 11. Low available ESP8266 memory

Use the firmware console:

```text
show stats
```

Avoid redundant calls to ip_napt_init().

The bundled lwIP library already initializes
the NAT data structures.

Removing a duplicate initialization recovered
approximately 11,808 bytes during development.

## 12. Slow internet connection

At 115200 baud with 8N1 framing, the theoretical
serial throughput limit is 11.52 kB/s before
protocol overhead.

Slow navigation is therefore expected.

Higher baud rates require separate hardware
and firmware validation.

## 13. Security considerations

- Use a strong WPA2 Wi-Fi password.
- Restrict access to TCP configuration port 7777.
- Do not expose the configuration console publicly.
- Review firewall rules before installation.
- Do not publish passwords or private configuration.
- Avoid running multiple SLIP services simultaneously.

## 14. Known limitations

Automatic startup after an Ubuntu reboot was
successfully validated with the working prototype.

Physical USB reconnection, prolonged stability,
multiple clients and higher baud rates remain
pending validation.

The cleaned firmware and generalized installer
also require separate end-to-end testing.

---

Author: Eliton Machado

Based on the ESP SLIP Router by Martin Ger.
