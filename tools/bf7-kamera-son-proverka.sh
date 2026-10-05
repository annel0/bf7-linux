#!/bin/sh
# BF7: проверка сна матрицы камеры в покое (bf7-camd).
#
# Что проверяем, и почему именно так:
#   1. в покое демон пишет «матрица в дежурном режиме» и почти не ест
#      процессор — матрица и блок захвата не крутятся зря;
#   2. при зрителе кадры идут с обычной частотой;
#   3. кадры ЖИВЫЕ: соседние различаются шумом матрицы. Если бы демон
#      отдавал зрителю сохранённый кадр из памяти, все кадры совпали бы —
#      а картинку глазами тут не проверить: телефон лежит экраном вверх, и
#      задний объектив упирается в стол;
#   4. после зрителя матрица снова засыпает;
#   5. страховка не сработала («сон отключён» в журнале — провал).
#
# Вызов: tools/bf7-kamera-son-proverka.sh
set -eu

telefon=${BF7_TELEFON:-bf7}
vrem=$(mktemp -d)
trap 'rm -rf "$vrem"' EXIT

# Демон пишет не в журнал systemd, а в /tmp/camera.log (StandardError=append
# в службе) — смотрим прирост этого файла с момента перезапуска.
ssh "$telefon" 'zhurnal() { tail -c +$((n0 + 1)) /tmp/camera.log | grep -aE "дежурн|разбуж|отключён" | tail -"$1" | tr "\n" "|"; }
	n0=$(stat -c %s /tmp/camera.log 2>/dev/null || echo 0)
	systemctl restart bf7-camd; sleep 12
	v=$(systemctl show -p MainPID --value bf7-camd)
	echo "1. журнал после старта: $(zhurnal 2)"
	a=$(awk "{print \$14+\$15}" /proc/$v/stat); sleep 20; b=$(awk "{print \$14+\$15}" /proc/$v/stat)
	echo "   процессор в покое: $(( (b - a) * 100 / 2000 )).$(( (b - a) * 1000 / 2000 % 10 ))% ядра (до сна матрицы было 1,6%)"
	t0=$(date +%s%N)
	timeout 25 ffmpeg -hide_banner -loglevel error -f v4l2 -i /dev/video0 \
		-frames:v 40 -f rawvideo -pix_fmt gray -y /tmp/bf7-kamera-kadry.raw 2>&1 | tail -1
	t1=$(date +%s%N)
	echo "2. 40 кадров зрителю за $(( (t1 - t0) / 1000000 )) мс"
	sleep 8
	echo "4. журнал после зрителя: $(zhurnal 3)"'

scp -q "$telefon:/tmp/bf7-kamera-kadry.raw" "$vrem/kadry.raw"

python3 - "$vrem/kadry.raw" <<'PY'
import sys
import numpy as np

a = np.fromfile(sys.argv[1], dtype=np.uint8)
for w, h in ((1024, 768), (1280, 960), (640, 480)):
    if a.size % (w * h) == 0 and a.size // (w * h) >= 10:
        break
k = a.reshape(-1, h, w).astype(float)
raznosti = [np.abs(k[i + 1] - k[i]).mean() for i in range(len(k) - 1)]
odinakovyh = sum(1 for r in raznosti if r == 0)
print("3. кадров %d (%dx%d), средняя разница соседних %.3f, одинаковых пар %d"
      % (len(k), w, h, float(np.mean(raznosti)), odinakovyh))
# Первые кадры могут быть сохранённым кадром из памяти, пока матрица
# просыпается, — смотрим хвост.
hvost = raznosti[len(raznosti) // 2:]
if hvost and min(hvost) > 0:
    print("   кадры живые — матрица проснулась и снимает")
else:
    print("   ВНИМАНИЕ: кадры в хвосте повторяются — зрителю идёт сохранённый кадр")
    sys.exit(1)
PY
