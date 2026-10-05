#!/bin/sh
# Repair essential character nodes after systemd receives /dev from initramfs.
ensure_char_device() {
    path=$1 major=$2 minor=$3 mode=$4
    if [ ! -c "$path" ]; then
        rm -f "$path"
        mknod "$path" c "$major" "$minor"
    fi
    chmod "$mode" "$path"
}

if [ ! -c /dev/null ]; then
    rm -f /dev/null
    mknod /dev/null c 1 3
fi
chmod 666 /dev/null

if [ ! -c /dev/full ]; then
    rm -f /dev/full
    mknod /dev/full c 1 7
fi
chmod 666 /dev/full

# The OEM kernel has no devtmpfs. These devices are registered but must be
# materialized for native userspace.
ensure_char_device /dev/fb0 29 0 660
mkdir -p /dev/dri /dev/snd /dev/input
ensure_char_device /dev/dri/card0 226 0 660
ensure_char_device /dev/dri/controlD64 226 64 660
ensure_char_device /dev/dri/renderD128 226 128 660
ensure_char_device /dev/pvr_sync 10 42 660

# The stock MediaTek ALSA card exposes speaker playback as card 0, device 30.
ensure_char_device /dev/snd/controlC0 116 46 660
ensure_char_device /dev/snd/pcmC0D0p 116 2 660
ensure_char_device /dev/snd/pcmC0D30p 116 44 660

# The stock touchscreen driver registers input devices normally, but this OEM
# kernel does not mount devtmpfs. Materialize every registered event node.
for event in /sys/class/input/event*; do
    [ -r "$event/dev" ] || continue
    dev=${event##*/}
    IFS=: read -r major minor < "$event/dev"
    ensure_char_device "/dev/input/$dev" "$major" "$minor" 660
done
