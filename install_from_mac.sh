#!/bin/sh
# Run on your Mac. You will be prompted for the Yun's current root password.
set -eu
host="${1:-192.168.240.1}"
case "$host" in ''|-*|*[!A-Za-z0-9.:-]*) echo 'Use the Yun IP address or hostname as the first argument.'; exit 1 ;; esac
base="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
ssh "root@$host" 'mkdir -p /tmp/workout-install'
# -O uses the older SCP protocol supported by many factory Dragino firmwares.
scp -O "$base/yun/collector.py" "$base/yun/index.html" "$base/yun/workout-monitor.init" "$base/yun/install_on_yun.sh" "root@$host:/tmp/workout-install/"
ssh "root@$host" 'cd /tmp/workout-install && sh install_on_yun.sh'
echo "Phone page: http://$host/workout/"
