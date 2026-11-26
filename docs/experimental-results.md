# Experimental Results

## 1. Test Environment

- ESP8266MOD ESP-12F
- CH340 USB-to-serial converter
- Windows 10 Pro
- Ubuntu running in VirtualBox
- Android smartphone
- UART: 115200 baud, 8N1
- SLIP MTU: 1500 bytes

## 2. Serial Communication Performance

### Final ICMP test

```bash
ping -c 10 192.168.240.1
```

| Parameter | Result |
|---|---:|
| Packets transmitted | 10 |
| Packets received | 10 |
| Packet loss | 0% |
| Minimum RTT | 18.225 ms |
| Average RTT | 20.174 ms |
| Maximum RTT | 29.745 ms |
| RTT deviation | 3.785 ms |

### Comparison during troubleshooting

| Configuration | Packet loss | Average RTT |
|---|---:|---:|
| Initial reboot failure | 100% | N/A |
| Manual UART correction | 25% | 439.941 ms |
| Revised automation | 0% | 20.174 ms |

## 3. Memory Optimization

| Configuration | Available heap |
|---|---:|
| Duplicate NAT initialization | 16,304 bytes |
| After removing duplicate initialization | 28,112 bytes |
| Final routing diagnostic | 28,048 bytes |

Observed memory recovery: 11,808 bytes.

Heap measurements may vary with runtime conditions.

## 4. Routing Validation

| Interface | Before correction | After correction |
|---|---|---|
| Wi-Fi (ew0) | Default | Non-default |
| SLIP (sl0) | Non-default | Default |

## 5. Functional Validation

| Test | Result |
|---|---|
| USB serial communication | PASS |
| SLIP encoding and decoding | PASS |
| IPv4 forwarding | PASS |
| 1500-byte IPv4 packet transport | PASS |
| Wi-Fi client connection | PASS |
| Local HTTP communication | PASS |
| External internet access | PASS |
| DNS resolution | PASS |
| HTTPS navigation | PASS |
| Automatic systemd startup | PASS |
| Internet access after Ubuntu reboot | PASS |

## 6. Theoretical Bandwidth

With 8N1 UART framing:

| UART baud rate | Theoretical maximum |
|---:|---:|
| 115200 | 92.16 kbps |
| 230400 | 184.32 kbps |
| 460800 | 368.64 kbps |
| 921600 | 737.28 kbps |
| 2000000 | 1600 kbps |

Only 115200 baud was validated in the final prototype.

These bandwidth values are theoretical and do not
represent measured internet throughput.

## 7. Pending Experiments

- Actual internet throughput
- Long-term connection stability
- Multiple simultaneous clients
- Higher UART speeds
- USB disconnection and reconnection
- Energy consumption
- Hardware validation of the cleaned firmware
- Functional validation of the generalized installer

## 8. Experimental Notes

The reported results were obtained during prototype
development in November 2026.

The ICMP latency measurements represent the SLIP
connection between Ubuntu and the ESP8266, rather
than end-to-end internet latency.

Internet access and reboot recovery were confirmed
through manual smartphone tests.

Author: Eliton Machado
