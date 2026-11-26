#!/bin/bash

set -e

BASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "========================================"
echo " ESP8266 Reverse SLIP Router Installer"
echo "========================================"

if [ "$EUID" -ne 0 ]; then
    echo "Please run this installer with sudo."
    exit 1
fi

echo "Checking dependencies..."

for cmd in slattach ip iptables stty systemctl sysctl; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "Missing dependency: $cmd"
        echo "Install net-tools, iproute2 and iptables."
        exit 1
    fi
done

echo "Available serial devices:"
ls /dev/serial/by-id/ 2>/dev/null || true

DEFAULT_SERIAL="/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0"

read -r -p "Serial device [$DEFAULT_SERIAL]: " SERIAL
SERIAL="${SERIAL:-$DEFAULT_SERIAL}"

echo "Available network interfaces:"
ip -br link

read -r -p "WAN interface [enp0s3]: " WAN
WAN="${WAN:-enp0s3}"

read -r -p "Serial baud rate [115200]: " BAUD
BAUD="${BAUD:-115200}"

if [ ! -c "$SERIAL" ]; then
    echo "Serial device not found: $SERIAL"
    exit 1
fi

if ! ip link show "$WAN" >/dev/null 2>&1; then
    echo "Network interface not found: $WAN"
    exit 1
fi

if ! [[ "$BAUD" =~ ^[0-9]+$ ]]; then
    echo "Invalid baud rate."
    exit 1
fi

echo "Installing router files..."

install -m 755 \
    "$BASE_DIR/esp-slip-router.sh" \
    /usr/local/bin/esp-slip-router.sh

install -m 644 \
    "$BASE_DIR/esp-slip-router.service" \
    /etc/systemd/system/esp-slip-router.service

mkdir -p /etc/systemd/system/esp-slip-router.service.d

cat > /etc/systemd/system/esp-slip-router.service.d/config.conf <<EOF
[Service]
Environment="SERIAL=$SERIAL"
Environment="WAN=$WAN"
Environment="BAUD=$BAUD"
EOF

systemctl daemon-reload
systemctl enable esp-slip-router.service

echo "Installation completed successfully."
echo "Automatic startup enabled."

read -r -p "Start the router now? [y/N]: " ANSWER

case "$ANSWER" in
    y|Y|yes|YES)
        systemctl start esp-slip-router.service
        systemctl status esp-slip-router.service --no-pager
        ;;
    *)
        echo "To start the router manually:"
        echo "sudo systemctl start esp-slip-router.service"
        ;;
esac
