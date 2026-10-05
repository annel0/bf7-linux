#!/bin/bash
# Поднимает набор служб Android, нужный аппаратному декодеру видео.
#
# Убиваем по цели /proc/PID/exe, а не через pkill -f: шаблон pkill совпадает
# с командной строкой ssh и убивает собственный сеанс.
#
# Подпорки, все через LD_PRELOAD и только этим процессам:
#   libselinux-stub — SELinux выключен (security=none), настоящая libselinux не
#                     открывается и hwservicemanager падает в AccessControl;
#   libbf7prop      — init не запущен, /dev/__properties__ нет, а libhidlbase
#                     крутит цикл ожидания свойства hwservicemanager.ready;
#   libbf7binder    — отклоняет BINDER_SET_CONTEXT_MGR_EXT, иначе ядро требует
#                     контекст SELinux на каждую транзакцию и все они падают с
#                     BR_FAILED_REPLY/-EOPNOTSUPP (binder.c:3280);
#   libbf7log       — logd без init не получает сокеты, журнал уходит в stderr.
#
# BF7_PROP_MINIMAL прячет настоящие build.prop: служба выделения буферов от них
# уходит на ветку, которой у нас нет.
#
# Без /vendor/etc/ld.config.txt у линковщика нет пространств имён, и библиотеки
# VNDK в apex он не находит — пути указываем явно.
L=/vendor.local/lib64
SEL=$L/libselinux-stub.so
PROP=$L/libbf7prop.so
LOG=$L/libbf7log.so
BND=$L/libbf7binder.so

VNDK64=/apex/com.android.vndk.v31/lib64
LDPATH64="$VNDK64:/vendor/lib64:/vendor/lib64/hw:/system/lib64:/system/lib64/hw"

HWSM=/system/bin/hwservicemanager

# имя|путь|доп-окружение|аргументы  (разделитель не двоеточие: оно внутри
# LD_LIBRARY_PATH). vndservicemanager без аргумента садится на /dev/binder.
#
# Службы питания (vendor.mediatek.hardware.mtkpower@1.0-service) тут нет
# намеренно. Она поднимается и регистрирует IMtkPerf, но её поток
# mtkScnHanler жжёт целое ядро без остановки: checkFPSupdate() ->
# getFPSvalue() в /vendor/lib64/libpowerhal.so опрашивает частоту кадров
# в пустом цикле, вообще не заходя в ядро (проверено strace: ноль системных
# вызовов за три секунды). Декодеру вместо неё подставлена наша заглушка
# клиента /vendor.local/lib/libmtkperf_client_vendor.so — теряется только
# подъём частот на время декодирования.
read -r -d '' SERVICES <<'LIST'
vndservicemgr|/vendor/bin/vndservicemanager|BF7_PROP_MINIMAL=1 BF7_CTXMGR=1|/dev/vndbinder
gralloc4|/vendor/bin/hw/android.hardware.graphics.allocator@4.0-service-mediatek|BF7_PROP_MINIMAL=1
LIST
SERVICES=${SERVICES//__LDPATH64__/$LDPATH64}

# Сравниваем [[ /proc/PID/exe -ef путь ]] — проверка встроена в bash (тот же
# inode по ссылке), процесс не порождается. Раньше здесь был $(readlink …) на
# каждый процесс в /proc: при загрузке их сотни, обход шёл четыре раза и ещё в
# каждом шаге ожидания — около 2800 порождений, и hwservicemanager поднимался
# через 12,5 с после старта скрипта. Вся цепочка модема ждала этого.
kill_by_exe() {
	local want="$1" p killed=1
	for p in /proc/[0-9]*; do
		[[ $p/exe -ef $want ]] || continue
		kill -KILL "${p#/proc/}" 2>/dev/null && killed=0
	done
	return $killed
}

alive_by_exe() {
	local want="$1" p
	for p in /proc/[0-9]*; do
		[[ $p/exe -ef $want ]] && { echo "${p#/proc/}"; return 0; }
	done
	return 1
}

# Дождаться, пока процесс поднимется. Раньше здесь стояли неподвижные sleep 3 и
# sleep 5 на каждую службу — при загрузке это набегало в добрый десяток секунд,
# и всё это время multi-user.target, а с ней и графика, ждали. Ждём по факту:
# опрашиваем раз в пятую долю секунды и уходим, как только процесс появился.
# Короткая выдержка после — чтобы служба успела зарегистрироваться.
wait_alive() {
	path_=$1
	limit=${2:-10}
	steps=$((limit * 5))
	i=0

	while [ "$i" -lt "$steps" ]; do
		if pid=$(alive_by_exe "$path_"); then
			sleep 0.4
			echo "$pid"
			return 0
		fi
		sleep 0.2
		i=$((i + 1))
	done
	return 1
}

# Прежние экземпляры (перезапуск вручную) — убить и дать им уйти. При
# загрузке убивать некого, и секунду ждать незачем.
ubili=
kill_by_exe "$HWSM" && ubili=1
while IFS='|' read -r name path envs args; do
	[ -n "$path" ] && kill_by_exe "$path" && ubili=1
done <<< "$SERVICES"
[ -n "$ubili" ] && sleep 1

setsid env LD_PRELOAD="$PROP:$LOG:$SEL:$BND" "$HWSM" >/tmp/hwsm.log 2>&1 </dev/null &
if pid=$(wait_alive "$HWSM" 10); then
	echo "  hwservicemanager    жив, pid $pid"
else
	echo "  hwservicemanager    УМЕР:"; sed 's/^/    /' /tmp/hwsm.log; exit 1
fi

while IFS='|' read -r name path envs args; do
	[ -n "$path" ] || continue
	# Диспетчерам нужен ещё и binder-шим: они регистрируются как context
	# manager и иначе требуют от ядра контекст SELinux.
	pre="$PROP:$LOG:$SEL"
	case "$envs" in *BF7_CTXMGR=1*) pre="$pre:$BND" ;; esac
	setsid env $envs LD_PRELOAD="$pre" "$path" $args >"/tmp/hal-$name.log" 2>&1 </dev/null &
	if pid=$(wait_alive "$path" 12); then
		printf "  %-19s жив, pid %s, CPU=%s%%\n" "$name" "$pid" "$(ps -o pcpu= -p $pid | tr -d ' ')"
	else
		printf "  %-19s УМЕР:\n" "$name"; tail -8 "/tmp/hal-$name.log" | sed 's/^/    /'
	fi
done <<< "$SERVICES"

# Перечень регистраций стоит до 25 секунд — при загрузке это задержало бы
# рабочий стол. Спрашиваем его только руками или по BF7_HAL_VERBOSE=1.
if [ -t 1 ] || [ -n "$BF7_HAL_VERBOSE" ]; then
	echo "=== зарегистрированные службы ==="
	timeout 25 env LD_PRELOAD="$PROP:$SEL" /system/bin/lshal --types=b --neat 2>/dev/null | sed 's/^/  /'
fi

echo "  hal-start: скрипт закончил работу"
