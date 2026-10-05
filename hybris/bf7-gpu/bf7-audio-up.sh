#!/bin/sh
# BF7: поднять аналоговый путь звука.
#
# Динамик молчал не из-за железа, а из-за порядка. Ключи аналогового пути
# кодека MT6357 включаются только когда поток уже идёт: Speaker_Amp_Change в
# драйвере проверяет GetDLStatus(), и щелчок до открытия устройства пропадает
# впустую. Поэтому здесь сначала запускается короткий поток тишины, и уже на
# нём выставляются три ключа.
#
# Нужны две вещи: включить внешний усилитель и поднять ключи кодека при
# РАБОТАЮЩЕМ потоке. Порознь ни то, ни другое звука не даёт.
#
# Про внешний усилитель была долгая путаница. Сперва казалось, что драйвер вывод
# не трогает: его AudDrv_GPIO_EXTAMP_Select ищет состояния pinctrl с именами
# extamp_high/low, а в дереве объявлены только aud_smartpa_on/off. Поэтому линию
# 151 (mt_soc_dl1_pcm@11220000/extamp_gpio) держали снаружи через gpioset — и
# звук появлялся.
#
# Одно время казалось, что драйвер забирает линию сам и подпорка снаружи не
# нужна. ОПРОВЕРГНУТО замером 2026-09-22: ключ туда-сюда при отпущенной линии —
# энергия записи 175, линия поднята снаружи — 41733. Драйвер вести её не может:
# при загрузке не находит состояний pinctrl (aud_smartpa_on, extamp-pullhigh
# fail -19). Линию держит служба bf7-extamp (gpioset); отпущенная, она
# возвращается во вход, и усилитель молчит.
#
# Набор ключей именно такой, проверено перебором с замером через микрофон:
#   AUD_CLK_BUF_Switch   тактовый буфер
#   Audio_Amp_L_Switch   усилитель ЦАП, левый
#   Audio_Amp_R_Switch   усилитель ЦАП, правый
# Энергия тона 440 Гц в записи подскакивает с ~12 тысяч до ~14,9 миллиона,
# то есть больше чем в тысячу раз.
#
# ВНИМАНИЕ: Speaker_Amp_Switch включать НЕЛЬЗЯ — с ним звук пропадает, и
# обратное выключение его уже не возвращает. Ext_Speaker_Amp_Switch и
# Voice_Amp_Switch на слышимость не влияют.
#
# Настройка держится между потоками: второй запуск звучит, ключей не касаясь.
# Имена переменных только латиницей: /bin/sh кириллические не принимает и
# отвечает «not found», хотя строка выглядит правильной.

card=0

# Сброс. Делаем его только если поток заводим сами: на чужом потоке обнуление
# ключей выключило бы звук у того, кто уже играет.
# Ключи надо сперва обнулить: AUD_CLK_BUF_Switch у драйвера не
# переключатель, а счётчик ссылок, и повторные включения его только наращивают,
# а состояние аналогового тракта при этом не меняется. Проверено: без сброса
# та же последовательность звука не даёт.
for key in Audio_Amp_L_Switch Audio_Amp_R_Switch Speaker_Amp_Switch \
           Ext_Speaker_Amp_Switch Voice_Amp_Switch Receiver_Speaker_Switch \
           Headset_Speaker_Amp_Switch AUD_CLK_BUF_Switch; do
	amixer -c "$card" cset name="$key" 0 >/dev/null 2>&1 || true
done
sleep 1

# Короткий поток тишины, чтобы драйвер считал нисходящий тракт работающим.
#
# Через звуковой сервер, а не прямо в hw:0,0. Причина выяснилась 2026-09-22:
# служба идёт ПОСЛЕ сервера, а тот держит устройство за собой. Прямое открытие
# отвечает «Device or resource busy», нисходящий тракт не заводится, и ключи
# ложатся впустую: микшер показывает «включено», а вывод внешнего усилителя
# остаётся низким и динамик молчит. Замер микрофоном: энергия 192 вместо
# миллионов.
#
# Если сервера нет (ранняя загрузка, ремонт) — открываем напрямую, как раньше.
# Но только если служба сервера действительно не запущена: пока она поднимается,
# устройство вот-вот окажется у неё, а второй поток на том же устройстве роняет
# ядро. Поэтому сперва ждём сервер — с пределом.
PULSE=${PULSE_SERVER:-unix:/run/bf7-pulse/native}
if systemctl is-active --quiet bf7-pulse.service 2>/dev/null ||
   [ "$(systemctl is-active bf7-pulse.service 2>/dev/null)" = activating ]; then
	i=0
	until PULSE_SERVER="$PULSE" pactl info >/dev/null 2>&1; do
		i=$((i + 1))
		if [ "$i" -ge 20 ]; then
			echo "bf7-audio-up: служба сервера запущена, но он не отвечает 20 с — устройство не трогаю" >&2
			exit 1
		fi
		sleep 1
	done
fi
if PULSE_SERVER="$PULSE" pactl info >/dev/null 2>&1; then
	PULSE_SERVER="$PULSE" timeout 6 paplay --raw --rate=48000 \
		--channels=2 --format=s16le /dev/zero >/dev/null 2>&1 &
else
	timeout 6 speaker-test -D "hw:${card},0" -c 2 -t sine -f 50 -l 2 -P 4 \
		>/dev/null 2>&1 &
fi
stream=$!
sleep 2

for key in AUD_CLK_BUF_Switch Audio_Amp_L_Switch Audio_Amp_R_Switch; do
	amixer -c "$card" cset name="$key" 1 >/dev/null 2>&1 ||
		echo "bf7-audio-up: не принял $key" >&2
done

# Внешний усилитель переключаем ЧЕРЕЗ НОЛЬ. Драйвер выполняет действие только
# при смене значения: если ключ уже стоит в единице (например, остался с
# прошлого раза), повторная единица ничего не делает, и вывод остаётся низким —
# усилитель молчит, хотя микшер показывает «включено».
amixer -c "$card" cset name=Ext_Speaker_Amp_Switch 0 >/dev/null 2>&1 || true
sleep 1
amixer -c "$card" cset name=Ext_Speaker_Amp_Switch 1 >/dev/null 2>&1 ||
	echo "bf7-audio-up: не принял Ext_Speaker_Amp_Switch" >&2

# Микрофон: предусилитель и АЦП. Без них запись выходит пустой.
amixer -c "$card" cset name=Audio_ADC_1_Switch 1 >/dev/null 2>&1 || true
amixer -c "$card" cset name=Audio_Preamp1_Switch 1 >/dev/null 2>&1 || true
amixer -c "$card" cset name=Audio_MicSource1_Setting 0 >/dev/null 2>&1 || true
amixer -c "$card" cset name=Audio_PGA1_Setting 4 >/dev/null 2>&1 || true

# Второй канал записи. Без него правый канал выходил РОВНО нулевым, и всякая
# стереозапись получалась наполовину немой.
#
# Второго микрофона на плате нет: при любом значении Audio_MicSource2_Setting
# (перебрал все четыре) каналы почти совпадают — 28% отсчётов бит в бит,
# средняя разница 1,1. То есть оба АЦП слушают один и тот же микрофон.
# Но заполненный правый канал всё равно лучше немого: приложения, которые
# ждут стерео, больше не получают тишину в половине дорожки.
amixer -c "$card" cset name=Audio_ADC_2_Switch 1 >/dev/null 2>&1 || true
amixer -c "$card" cset name=Audio_Preamp2_Switch 1 >/dev/null 2>&1 || true
amixer -c "$card" cset name=Audio_MicSource2_Setting 0 >/dev/null 2>&1 || true
amixer -c "$card" cset name=Audio_PGA2_Setting 4 >/dev/null 2>&1 || true

[ -n "$stream" ] && wait "$stream" 2>/dev/null
true
echo "bf7-audio-up: аналоговый путь поднят"
