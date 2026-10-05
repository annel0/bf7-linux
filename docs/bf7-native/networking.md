# Сеть, SSH и время

## Wi-Fi

Native Debian использует стоковые MediaTek driver и firmware из заводской
прошивки. ConnSys запускается специальным bootstrap, а подключением управляют
три сервиса:

```text
bf7-wifi.service  — поднимает проприетарный Wi-Fi стек
bf7-wpa.service   — wpa_supplicant в foreground под systemd
bf7-dhcp.service  — BusyBox udhcpc получает DHCP-адрес
```

Проверка:

```sh
systemctl --no-pager status bf7-wifi bf7-wpa bf7-dhcp
ip -brief address show wlan0
iw dev wlan0 link
ping -c 2 1.1.1.1
```

Обычный `wpa_supplicant -B` на этом OEM-ядре не годится: daemonization получает
`ENXIO`, хотя команда способна завершиться с кодом 0. Поэтому сервис намеренно
держит `wpa_supplicant` в foreground. Generic `wpa_supplicant.service` отключён,
чтобы не конфликтовать с рабочим сервисом.

systemd-networkd DHCP на этом ядре ненадёжен, поэтому используется `udhcpc`.
Не заменять эти компоненты «обычным» сетевым стеком без отдельного теста.

### Фиксированный домашний адрес

Для текущей домашней сети адрес Wi-Fi закреплён локально:

```text
phone:   192.168.27.140/24
gateway: 192.168.27.1
DNS:     192.168.27.1
```

`bf7-dhcp.service` отключён. Адрес задают
`/etc/systemd/network/20-wlan0.network` и дополнительный
`bf7-static-wifi.service`, который применяет его после успешного WPA
подключения. Если домашняя подсеть или шлюз изменятся, нужно изменить оба
файла, затем выполнить:

```sh
systemctl daemon-reload
systemctl restart bf7-static-wifi
```

## SSH

На Wi-Fi адрес выдаётся DHCP, поэтому его нужно смотреть на самом телефоне:

```sh
ip -brief address show wlan0
```

Доступ root производится только по публичному ключу. Ключи хранятся в
`/root/.ssh/authorized_keys`; пароль Wi-Fi и приватный ключ здесь не записываются.

## USB-RNDIS: аварийный канал

При native-boot USB-gadget предоставляет RNDIS. Стороны имеют статическую сеть:

```text
host:    192.168.7.1/24
phone:   192.168.7.2/24
SSH:     tcp/22
```

На хосте интерфейс обычно называется `enp22s0f0u9`:

```sh
sudo nmcli device set enp22s0f0u9 managed no
sudo ip addr replace 192.168.7.1/24 dev enp22s0f0u9
sudo ip link set enp22s0f0u9 up
ssh -i ~/.ssh/id_rsa root@192.168.7.2
```

Если пинг не проходит, сначала назначить адрес хоста: на телефоне нет DHCP для
USB-подсети.

## Время

OEM-ядро ошибочно определял native Debian как контейнер, из-за чего
`systemd-timesyncd` не стартовал. В `/etc/systemd/system/systemd-timesyncd.service.d/native.conf`
снято ограничение `ConditionVirtualization`. Проверка:

```sh
timedatectl status
systemctl status systemd-timesyncd
```
