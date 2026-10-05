#!/bin/sh
# BF7: песочница для первых запусков демонов модема.
#
# Настоящие разделы NVRAM (IMEI, калибровка радио) портить нельзя, поэтому
# демоны впервые запускаются на КОПИЯХ: копии монтируются в частном
# пространстве монтирования на те же пути, что у Android (/mnt/vendor/*),
# свойства — через заглушку libbf7prop, всё под strace. Что демон пишет —
# видно до того, как он коснётся настоящих разделов.
#
# Вызов: pesochnica.sh <этап> [секунд]
#   nvram  — nvram_daemon (готовит NVRAM, на Android идёт первым)
#   mdinit — ccci_mdinit 0 (поднимает модем и отвечает ему на запросы файлов)
#   modem  — то же, но сперва HAL NVRAM (vendor.mediatek.hardware.nvram@1.1):
#            fsd в ccci_mdinit читает NVRAM модема через него, по hwbinder,
#            и без него на первом же запросе модема ждёт вечно
set -u
etap=${1:?этап: nvram или mdinit}
sekund=${2:-60}
D=/root/modem-pesochnica
mkdir -p "$D/img" "$D/log"

for p in nvdata nvcfg protect1 protect2 persist; do
	[ -f "$D/img/$p.img" ] ||
		dd if="/dev/disk/by-partlabel/$p" of="$D/img/$p.img" bs=1M status=none
done

# Свойства: наши обычные плюс то, что на Android выставили бы другие
# процессы (заглушка у каждого процесса своя и чужих записей не видит).
{
	cat /vendor.local/etc/bf7.prop
	echo "vendor.connsys.driver.ready=yes"
	# ccci_mdinit в цикле ждёт vendor.service.nvram_restore = «Ready» (или
	# «Pre_Ready») — сравнение найдено дизассемблером; выставляет его
	# nvram_daemon, но в другом процессе.
	if [ "$etap" = mdinit ] || [ "$etap" = modem ]; then
		echo "vendor.service.nvram_init=Ready"
		echo "vendor.service.nvram_restore=Ready"
	fi
} > "$D/bf7.prop"

case "$etap" in
	nvram)  cmd="/vendor/bin/nvram_daemon" ;;
	mdinit|modem) cmd="/vendor/bin/ccci_mdinit 0" ;;
	*) echo "неизвестный этап $etap" >&2; exit 2 ;;
esac

log=$D/log/$etap-$(date +%H%M%S)
echo "=== $(date '+%F %T') этап $etap, до $sekund с, журнал $log.*"
echo "ccci до: $(cat /sys/kernel/ccci/boot | head -1)"
unshare -m sh -c '
	set -e
	mount --make-rprivate /
	for m in nvdata:nvdata nvcfg:nvcfg protect_f:protect1 protect_s:protect2 persist:persist; do
		mkdir -p /mnt/vendor/${m%%:*}
		mount -o loop,noatime,nosuid,nodev "'"$D"'/img/${m#*:}.img" /mnt/vendor/${m%%:*}
	done
	mount --bind "'"$D"'/bf7.prop" /vendor.local/etc/bf7.prop
	L=/vendor.local/lib64
	if [ "'"$etap"'" = modem ]; then
		# HAL NVRAM — так же, как gralloc в hal-start.sh: заглушки свойств,
		# лога и SELinux. Работает в этом же пространстве, то есть на копиях.
		setsid env LD_PRELOAD=$L/libbf7prop.so:$L/libbf7log.so:$L/libselinux-stub.so \
			/vendor/bin/hw/vendor.mediatek.hardware.nvram@1.1-service \
			> "'"$log"'.nvram-hal" 2>&1 < /dev/null &
		# ccci_rpcd — пользовательская половина RPC: модем на старте спрашивает
		# у процессора приложений то, чего ядро не знает. На Android он в
		# class core, то есть работает всегда.
		setsid env LD_PRELOAD=$L/libbf7prop.so:$L/libbf7log.so:$L/libselinux-stub.so:$L/libbf7wakelock.so \
			/vendor/bin/ccci_rpcd 0 > "'"$log"'.rpcd" 2>&1 < /dev/null &
		sleep 3
	fi
	# LD_PRELOAD — только демону: заглушка собрана под bionic, glibc-овские
	# timeout и strace её не загрузят.
	timeout -s INT '"$sekund"' strace -f -tt -o "'"$log"'.strace" \
		-E LD_PRELOAD=/vendor.local/lib64/libbf7prop.so:/vendor.local/lib64/libselinux-stub.so:/vendor.local/lib64/libbf7wakelock.so \
		-e trace=openat,unlinkat,renameat,mkdirat,write,pwrite64,ioctl,execve \
		'"$cmd"' > "'"$log"'.out" 2>&1 || true
'
echo "ccci после: $(cat /sys/kernel/ccci/boot | head -1)"
# HAL NVRAM живёт отдельно от timeout — гасим по цели /proc/PID/exe.
for p in /proc/[0-9]*; do
	case "$(readlink "$p/exe" 2>/dev/null)" in
		/vendor/bin/hw/vendor.mediatek.hardware.nvram@1.1-service|/vendor/bin/ccci_rpcd)
			kill "${p#/proc/}" 2>/dev/null ;;
	esac
done
echo "--- записи в файлы (кроме журналов):"
grep -oE 'openat\([^"]*"[^"]+", [^)]*O_(WRONLY|RDWR|CREAT)[^)]*' "$log.strace" |
	grep -oE '"[^"]+"' | sort | uniq -c | sort -rn | head -30
echo "--- последние строки вывода:"
tail -15 "$log.out"
