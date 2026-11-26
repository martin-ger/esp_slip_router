#!/bin/bash

set -e

SERIAL="${SERIAL:-/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0}"
BAUD="${BAUD:-115200}"
WAN="${WAN:-enp0s3}"
SLIP="sl0"
LOCAL_IP="192.168.240.2"
ESP_IP="192.168.240.1"
WIFI_NET="192.168.4.0/24"

echo "Waiting for ESP8266..."

while [ ! -c "$SERIAL" ]; do
    sleep 2
done

echo "Configuring serial port..."

stty -F "$SERIAL" "$BAUD" cs8 \
    -parenb -cstopb -crtscts \
    raw -echo

sleep 3

echo "Starting SLIP..."

slattach -L -p slip -s "$BAUD" "$SERIAL" &
SLIP_PID=$!

trap 'kill "$SLIP_PID" 2>/dev/null || true' EXIT
trap 'exit 0' TERM INT

for i in {1..30}; do
    ip link show "$SLIP" >/dev/null 2>&1 && break
    kill -0 "$SLIP_PID"
    sleep 1
done

ip addr replace "$LOCAL_IP" peer "$ESP_IP" dev "$SLIP"
ip link set "$SLIP" mtu 1500 up

ip route replace "$WIFI_NET" via "$ESP_IP" dev "$SLIP"

sysctl -w net.ipv4.ip_forward=1

iptables -C FORWARD -i "$SLIP" -o "$WAN" -j ACCEPT 2>/dev/null ||
iptables -A FORWARD -i "$SLIP" -o "$WAN" -j ACCEPT

iptables -C FORWARD -i "$WAN" -o "$SLIP" \
    -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT 2>/dev/null ||
iptables -A FORWARD -i "$WAN" -o "$SLIP" \
    -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT

iptables -t nat -C POSTROUTING -s "$WIFI_NET" -o "$WAN" -j MASQUERADE 2>/dev/null ||
iptables -t nat -A POSTROUTING -s "$WIFI_NET" -o "$WAN" -j MASQUERADE

echo "ESP8266 SLIP Router started!"

wait "$SLIP_PID"
