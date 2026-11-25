#!/bin/sh
set -eu
task_base="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
if ! command -v python3 >/dev/null 2>&1; then
    echo 'Install Python 3 for macOS first, then run this checker again.'
    exit 1
fi
echo 'Health project checker'
echo '1: UNO connected directly to this Mac by USB'
echo '2: UNO connected to the Yun; check it over Wi-Fi'
echo '3: Interface demonstration with clearly simulated data'
printf 'Choose 1, 2 or 3: '
read -r task_mode
case "$task_mode" in
    1) exec python3 "$task_base/check_health.py" --usb ;;
    2) printf 'Yun IP address or hostname: '; read -r task_host
       case "$task_host" in ''|-*|*[!A-Za-z0-9.:-]*) echo 'Enter an IP address or hostname.'; exit 1 ;; esac
       exec python3 "$task_base/check_health.py" --wifi "http://$task_host/workout/latest.json" ;;
    3) exec python3 "$task_base/check_health.py" --demo ;;
    *) echo 'Run again and choose 1, 2 or 3.'; exit 1 ;;
esac
