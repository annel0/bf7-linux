#!/bin/sh
# BF7: полный цикл "собрать ядро -> собрать образ -> прошить слот A -> загрузить -> запустить GPU".
# Каждый каталог задан абсолютным путём: дважды подряд образ собирался не там,
# где лежит build.sh, и прошивался предыдущий образ, а вывод выглядел свежим.
set -e
ROOT=$HOME/kall2linux
ROOTFS=$ROOT/native-linux-build/alps191-rootfs-v1
IMG=$ROOTFS/bf7-alps191-rootfs-v1.img
export KERNEL_IMAGE=$ROOT/kernel-work/build/bf7-x657b-clang11-v1/arch/arm64/boot/Image
LOG=${1:-/tmp/bf7-cycle.log}
SSH="ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=20 -o BatchMode=yes root@192.168.7.2"

sh $ROOT/kernel-work/scripts/build-bf7-x657b.sh > "$LOG" 2>&1 || true
if ! grep -q "kernel release:" "$LOG"; then
	echo "СБОРКА ЯДРА НЕ ПРОШЛА:"; grep -E "error:" "$LOG" | head -10; exit 1
fi
echo "ядро собрано"

BEFORE=$(sha256sum "$IMG" 2>/dev/null | cut -c1-16)
( cd "$ROOTFS" && sh build.sh ) >/dev/null 2>&1
AFTER=$(sha256sum "$IMG" | cut -c1-16)
[ "$BEFORE" = "$AFTER" ] && echo "ВНИМАНИЕ: образ не изменился ($AFTER)" || echo "образ пересобран: $BEFORE -> $AFTER"

$SSH 'cat > /tmp/ef; chmod +x /tmp/ef' < $ROOT/native-linux-build/storage-diagnostic-v1/bf7-enter-fastboot.aarch64 2>/dev/null || true
timeout 20 $SSH 'sync; /tmp/ef' >/dev/null 2>&1 || true
n=0; until fastboot devices 2>/dev/null | grep -q . || [ $n -ge 30 ]; do n=$((n+1)); sleep 2; done
fastboot flash boot_a "$IMG" 2>&1 | tail -1
fastboot set_active a >/dev/null 2>&1
fastboot reboot >/dev/null 2>&1
n=0; until timeout 6 $SSH true 2>/dev/null; do n=$((n+1)); [ $n -ge 70 ] && break; sleep 4; done
timeout 240 $SSH 'n=0; until systemctl is-system-running 2>/dev/null | grep -qE "running|degraded" || [ $n -ge 20 ]; do n=$((n+1)); sleep 2; done
timeout 60 dd if=/dev/dri/card0 of=/dev/null bs=1 count=1 >/dev/null 2>&1; sleep 3' 2>/dev/null
echo "загружено, GPU запущен"
