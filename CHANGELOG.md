# Changelog

All notable changes introduced by this fork are documented
in this file.

## [Unreleased] - 2026-11-26

### Added

- Post-SDK initialization callback for AP-mode routing.
- Extended network interface diagnostics in `show stats`.
- Linux gateway automation script.
- Interactive Linux installation script.
- Systemd service for automatic startup.
- Configurable serial device, WAN interface and baud rate.
- Automatic IPv4 forwarding configuration.
- Network architecture diagram.
- Experimental results and troubleshooting documentation.

### Fixed

- Incorrect default network interface in AP mode.
- External IPv4 traffic forwarding through SLIP.
- UART initialization issues observed after Linux reboot.
- Duplicate network interface diagnostic output.
- Incorrect theoretical serial bandwidth estimate
  in the upstream documentation.

### Changed

- NAT is performed on the Linux gateway in reverse mode.
- SLIP MTU is configured to 1500 bytes.
- UART parameters are explicitly configured using stty.
- NAT rules are restricted to the ESP8266 wireless subnet.
- Firewall rules are checked before insertion.
- Linux automation messages are written in English.

### Experimental validation

The working prototype successfully demonstrated:

- Bidirectional IPv4 communication over SLIP.
- Internet access from an Android smartphone.
- DNS resolution and HTTPS navigation.
- Automatic startup after Ubuntu reboot.
- 10 successful ICMP responses out of 10 requests.
- 0% packet loss.
- Average round-trip latency of 20.174 ms.

### Pending validation

- Hardware testing of the cleaned firmware.
- Functional testing of the generalized installer.
- Internet throughput measurements.
- Long-term stability testing.
- Multiple simultaneous Wi-Fi clients.
- Automatic recovery after physical USB disconnection.
- Higher UART baud rates.

### Credits

- Original ESP SLIP Router: Martin Ger.
- Reverse-routing corrections and Linux automation:
  Eliton Machado, Tomsk Polytechnic University.

The original upstream license and copyright are preserved.
