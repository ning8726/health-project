#!/bin/sh
# Run ON the Yun, from the directory containing this script and its companion files.
set -eu
if [ "$(id -u)" != 0 ]; then echo 'Run this installer as root on the Yun.'; exit 1; fi
if [ ! -f /etc/openwrt_release ]; then echo 'This installer is for the Yun OpenWrt system.'; exit 1; fi
for file in collector.py index.html workout-monitor.init; do
    if [ ! -f "$file" ]; then echo "Missing companion file: $file"; exit 1; fi
done
if ! command -v python >/dev/null 2>&1; then echo 'The Yun Python interpreter is missing. Stop and check its firmware.'; exit 1; fi
python -c 'import os,json,termios,fcntl,select,signal,struct' || exit 1
# Use an installed kernel driver; do not fetch a mismatched kernel package.
modprobe cdc_acm 2>/dev/null || true
if ! grep -q 'cdc_acm' /proc/modules 2>/dev/null && [ ! -d /sys/module/cdc_acm ]; then
    echo 'USB CDC ACM driver was not detected.'
    echo 'If no /dev/ttyACM* port appears, obtain kmod-usb-acm matching THIS firmware/kernel.'
    echo 'Do not force-install a driver from a different OpenWrt release.'
fi
mkdir -p /root/workout-monitor /www/workout /tmp/workout
cp collector.py /root/workout-monitor/collector.py
cp index.html /www/workout/index.html
chmod 644 /root/workout-monitor/collector.py /www/workout/index.html
if [ -e /www/workout/latest.json ] && [ ! -L /www/workout/latest.json ]; then
    cp /www/workout/latest.json /www/workout/latest.json.backup
fi
ln -sf /tmp/workout/latest.json /www/workout/latest.json
if [ -f /etc/init.d/workout-monitor ]; then
    /etc/init.d/workout-monitor stop || true
    cp /etc/init.d/workout-monitor /etc/init.d/workout-monitor.backup
fi
cp workout-monitor.init /etc/init.d/workout-monitor
chmod 755 /etc/init.d/workout-monitor
/etc/init.d/workout-monitor enable
/etc/init.d/workout-monitor start
sleep 1
python - <<'PY'
import json
with open('/tmp/workout/latest.json') as f:
    status = json.load(f)
print('Reader status: ' + status.get('collector_status', 'unknown'))
print('USB device: ' + (status.get('usb_port') or 'not detected yet'))
PY
echo 'Installed. Open http://YOUR_YUN_IP/workout/ on the phone.'
echo 'Arduino readings are expected through the Yun USB host port (/dev/ttyACM*).'
echo 'No UART, Bridge, console, Wi-Fi, firewall, or firmware configuration was changed.'
