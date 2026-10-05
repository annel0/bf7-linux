#!/bin/sh
# Межсетевой экран BF7.
#
# Доверенная зона — USB-сеть 192.168.7.0/24: это прямой кабель к ПК, через
# неё идут ssh, VNC и аварийные консоли. Из Wi-Fi пускаем только ssh и эхо.
# Ядро собрано без nftables, поэтому используется iptables-legacy.
set -e

USB_NET=192.168.7.0/24

ipt() { iptables "$@"; }

ipt -P INPUT ACCEPT          # на время перестройки, чтобы не отрезать себя
ipt -F INPUT
ipt -F FORWARD
ipt -X 2>/dev/null || true

ipt -A INPUT -i lo -j ACCEPT
ipt -A INPUT -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT
ipt -A INPUT -s "$USB_NET" -j ACCEPT
ipt -A INPUT -p icmp --icmp-type echo-request -m limit --limit 10/s -j ACCEPT
ipt -A INPUT -p tcp --dport 22 -m conntrack --ctstate NEW -j ACCEPT
# Широковещательное и групповое от соседей по Wi-Fi (умные розетки на UDP
# 6667 и т. п.) отбрасываем молча: иначе ограничитель журнала (2 в минуту)
# целиком уходил на этот мусор, и настоящие попытки достучаться в журнал не
# попадали. Работе ничего не меняет — DHCP ходит пакетными сокетами мимо
# netfilter, а прочее входящее и так отбрасывается.
ipt -A INPUT -m pkttype --pkt-type broadcast -j DROP
ipt -A INPUT -m pkttype --pkt-type multicast -j DROP
# Цель LOG в этом ядре не собрана (CONFIG_NETFILTER_XT_TARGET_LOG),
# поэтому добавляем её только если она есть.
iptables -A INPUT -m limit --limit 2/min -j LOG \
	--log-prefix "bf7-fw drop: " --log-level 7 2>/dev/null \
	|| echo "bf7-firewall: цель LOG недоступна, отбрасывание не журналируется" >&2
ipt -P INPUT DROP
ipt -P FORWARD DROP
ipt -P OUTPUT ACCEPT

# IPv6: адреса нет, но правила ставим, чтобы не было открытой двери.
ip6tables -P INPUT ACCEPT
ip6tables -F INPUT
ip6tables -A INPUT -i lo -j ACCEPT
ip6tables -A INPUT -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT
ip6tables -A INPUT -p ipv6-icmp -j ACCEPT
ip6tables -A INPUT -p tcp --dport 22 -m conntrack --ctstate NEW -j ACCEPT
ip6tables -P INPUT DROP
ip6tables -P FORWARD DROP

exit 0
