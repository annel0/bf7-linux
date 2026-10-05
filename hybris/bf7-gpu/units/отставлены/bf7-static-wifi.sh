#!/bin/sh
# Fixed LAN address for the BF7 native server. Keep this in a separate
# service because the OEM kernel's systemd-networkd DHCP path is unreliable.
set -eu

ip addr replace 192.168.27.140/24 dev wlan0
ip route replace default via 192.168.27.1 dev wlan0
printf 'nameserver 192.168.27.1\n' > /etc/resolv.conf
