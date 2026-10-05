#!/bin/sh
# BF7: акустическая петля — проверка звука без ушей.
#
# Телефон играет тон 1 кГц через динамик и одновременно пишет его своим же
# микрофоном. Уровень тона в записи над шумом говорит, дошёл ли звук до
# воздуха: при исправном тракте это 68–71 дБ, в тишине — 8–18 дБ.
#
# Зачем. Снимок экрана и счётчики ALSA не отвечают на вопрос «слышно ли».
# Так 2026-09-23 перемерено опасение, из-за которого звуковой сервер не
# засыпал никогда (см. hybris/bf7-gpu/units/bf7-pulse.pa).
#
# Вызов: bf7-zvuk-petlya.sh [метка]   — печатает уровни по 0,1 с и итог.
# Звук слышен — телефон пискнет на две секунды.
set -eu

metka=${1:-petlya}
telefon=${BF7_TELEFON:-bf7}
vrem=$(mktemp -d)
trap 'rm -rf "$vrem"' EXIT

ssh "$telefon" "export PULSE_SERVER=unix:/run/bf7-pulse/native
	ffmpeg -hide_banner -loglevel error -f lavfi \
		-i 'sine=frequency=1000:duration=2:sample_rate=48000' \
		-ac 2 -f s16le -y /tmp/bf7-ton.raw
	rm -f /tmp/bf7-$metka.raw
	setsid parecord --raw --rate=48000 --channels=2 --format=s16le \
		-d bf7in /tmp/bf7-$metka.raw >/dev/null 2>&1 < /dev/null &
	sleep 0.7
	paplay --raw --rate=48000 --channels=2 --format=s16le /tmp/bf7-ton.raw
	sleep 0.5
	pkill -x parecord
	sleep 0.3"

scp -q "$telefon:/tmp/bf7-$metka.raw" "$vrem/zapis.raw"

python3 - "$vrem/zapis.raw" <<'PY'
import sys
import numpy as np

a = np.fromfile(sys.argv[1], dtype="<i2").reshape(-1, 2).astype(float)
x, sr = a[:, 0], 48000
kus = sr // 10
urovni = []
for i in range(0, len(x) - kus, kus):
    s = x[i:i + kus]
    f = np.abs(np.fft.rfft(s * np.hanning(len(s))))
    fr = np.fft.rfftfreq(len(s), 1 / sr)
    ton = f[(fr > 950) & (fr < 1050)].max()
    shum = np.median(f[(fr > 1200) & (fr < 3000)]) + 1e-9
    urovni.append(20 * np.log10(ton / shum))

print("тон 1 кГц над шумом, дБ, по 0,1 с:", " ".join("%.0f" % u for u in urovni))
pik = max(urovni) if urovni else 0
if pik >= 50:
    print("итог: звук есть, пик %.0f дБ" % pik)
else:
    print("итог: ЗВУКА НЕТ, пик %.0f дБ (при исправном тракте ~69)" % pik)
    sys.exit(1)
PY
