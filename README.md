# BF7 Linux

**Real Debian on a budget Android phone whose vendor never released the kernel source.**

TECNO BF7 (Spark Go 2023 / Pop 7 Pro) — MediaTek MT6761 (Helio A22),
4× Cortex-A53, PowerVR GE8300, 4 GB RAM. It now boots its own Linux
4.19.325-CIP kernel straight into Debian 12. systemd is PID 1, and Phosh
runs on the panel with GPU acceleration. No chroot, no Halium, no Android
userspace running underneath.

> Русская версия: [README.ru.md](README.ru.md). The full research log (Russian,
> ~15,800 lines, timestamped, wrong turns included) is in
> [docs/bf7-kernel/research-log.md](docs/bf7-kernel/research-log.md).

## What works

| | Status | How |
|---|---|---|
| Kernel | Linux **4.19.325-CIP**, built from source | x657b tree + 40 of our commits → [annel0/bf7-kernel-x657b](https://github.com/annel0/bf7-kernel-x657b) |
| Boot | ~25 s to the shell, A/B slots, Android kept as fallback | stock LK; candidate in one slot, known-good in the other |
| Display | 720×1612 TD4160 panel, KMS, fbcon, night light | vendor `mtkfb` path + our own DRM driver `bf7_drm` |
| Touch | multitouch | Synaptics TCM over SPI, ported from a donor tree |
| GPU | **GLES 3.2 + Vulkan 1.1**, ~60 fps in the shell | Android PowerVR DDK through a heavily patched libhybris |
| Shell | Phosh 0.24 on phoc/wlroots, Xwayland with glamor | our patches + a GBM backend on top of gralloc |
| Video | hardware H.264 / H.265, zero-copy to the panel | our own VA-API driver → daemon → MediaTek Codec2 |
| Camera | rear camera as `/dev/video0`, 30 fps, V4L2 controls | sensor, MIPI CSI1 and CAMSV brought up by hand |
| Wi-Fi | 2.4 + 5 GHz, hotspot | MediaTek gen4m; missing `plat_priv.c` rewritten from scratch |
| Bluetooth | works, factory MAC | bridge to a virtual HCI (`btbridge`) |
| Audio | speaker, mic, in-call audio | MediaTek ALSA + vendor audio HAL for the voice path |
| Cellular | calls, SMS, mobile data, LTE | modem without Android, AT bridge, 7 ModemManager patches |
| GPS | chip alive, NMEA to geoclue | `gps_drv` + vendor `mnld` + our own HAL client |
| Sleep | s2idle; the modem wakes it, alarms work | wakeup audit, kernel and service fixes |
| Misc | torch, vibration, notification LED, sensors, buttons | see the [checklist](docs/bf7-native/телефон-доводка.md) |

## The path

### 1. No sources (August 2026)

The factory kernel is `4.19.191-g216400aba14e-dirty`, built by Transsion in
December 2024. No source was ever released, so the picture was pieced together:

- **Config** extracted from the running kernel (`extract-ikconfig`). Diffing it
  against MediaTek's open ALPS base turned up
  [107 Transsion Kconfig symbols](docs/bf7-kernel/oem-symbols-missing-from-alps191.txt)
  with no public source anywhere.
- **Donors**: open trees of other MT6761 phones, matched chip by chip for
  touch, Wi-Fi/WMT and audio ([sources](docs/bf7-kernel/sources.md),
  [compatibility matrix](docs/bf7-kernel/compatibility-matrix.md)).
- **Panel driver recovered from the binary**: init sequences, timings and LCM
  parameters lifted out of the factory kernel into `bf7_lcm_recovered`.
- **First foothold** on the stock kernel: a custom ramdisk mounts a Debian
  partition and execs systemd. USB-RNDIS corrupted outgoing frames; fixed in
  `u_ether` ([write-up](docs/bf7-kernel/usb-rndis-tx-corruption.md)).

### 2. Our own kernel boots (September 11–15)

- `fastboot boot` can't work on this device. LK logs recovered from `expdb`
  said `boot image decapsulate fail`. Disassembling `lk_b` showed that in
  fastboot mode it ignores the downloaded image. So candidates are flashed
  to the inactive A/B slot instead, and the bootloader falls back on its own
  once the boot attempts run out.
- The first boot had no display: Transsion's `struct tag_video_lfb` carries
  one extra `u32`. Found by offsets, fixed.
- Moved to the [x657b 4.19.325 tree](https://github.com/MostafaAshry513/x657b_kernel_4.19)
  (CIP SLTS, native MT6761), 134 stable releases newer than the factory kernel.
  Its author had concluded that a from-source kernel wasn't feasible for their
  phone because the Transsion drivers were missing. On the BF7 it booted after
  three of our patches and the touch driver were carried over.
- fbcon reached the panel only after fixing two `mtkfb` bugs: it overwrote
  its own `fb_ops`, and `setcolreg` was missing.

### 3. The GPU firmware that wouldn't start (September 14–15)

The longest fight. The PowerVR kernel driver loaded, but the firmware running
on the GPU's internal MIPS core never reached `bFirmwareStarted`.

- Memory, clocks and power were all checked through `/dev/mem` and all were fine.
- No debugger, so the call chain inside the firmware was unwound by writing
  `$ra` return addresses into an uncached `kseg1` page and reading it from
  the host. That led to `RGXFWAlignChecks`, where the firmware compares 25
  structure sizes and offsets against the ones the driver sends.
- Three entries disagreed, all about one struct: `RGXFWIF_FWCOMMONCONTEXT`
  is 168 bytes in the factory `rgx.fw` but 160 in our driver. MediaTek had
  shipped two variants of the same PowerVR drop. The later one, found in
  someone else's GPL release for the MT6765, has `ui32DeferCount` and
  `ui32MiscFlags`. Fixed in C. The factory firmware is untouched.

### 4. Graphics on plain glibc Linux (September 16–23)

The PowerVR userspace exists only for Android/bionic. The bridge is
[libhybris](https://github.com/libhybris/libhybris), and it needed a lot of work
([our patch](hybris/bf7-patches/libhybris-bf7.patch), 28 files):

- a bionic TLS slot stub; calls through a null pointer when a HAL is missing;
  bare `dlopen` names that silently resolved to stubs;
- GL entry points must come from a direct `dlsym`, or glvnd hands back `NULL`
  from `glGetString`; EGL extensions the driver never exports;
- `linux-dmabuf` for Wayland clients, an X11 platform, Vulkan on Wayland, and
  a shader blob cache (−64% CPU for GTK on GLES).

On top of that:

- **our own KMS driver** (`bf7_drm`): a real connector, zero-copy scanout of
  foreign dma-bufs, panel capture;
- **a GBM backend over gralloc** ([gbm-backend](gbm-backend)) plus `wl_drm`
  ([gui-wldrm](gui-wldrm)), which gave Xwayland glamor on PowerVR
  ([patch](patches/xwayland-22.1.9-bf7-powervr.patch));
- **Phosh + wlroots**, accelerated: three intercepts in the shim and 128 MB of CMA.

### 5. Video, camera, audio (September 16–22)

- **Hardware decode** is only reachable through Codec2 over Android binder. So:
  our own VA-API driver ([vaapi-shim](vaapi-shim)) talks to a 32-bit daemon
  that runs MediaTek Codec2 with stubbed binder, properties and SELinux
  ([hybris/bf7-codec2](hybris/bf7-codec2)). H.264/H.265 work in ffmpeg, mpv
  and GStreamer, and in GStreamer frames reach the panel with zero copies.
- **Camera**: the sensor stayed silent until it got an MCLK. The receiver
  turned out to be on CSI1, and CAMSV was missing subblock clocks. Result:
  `/dev/video0` with standard V4L2 controls, palette white balance, frame
  stacking and dark-frame subtraction ([camera.md](docs/bf7-native/camera.md)).
- **Audio panicked the kernel**: the driver copied from userspace straight
  into device memory. We reproduced it on purpose and fixed it with a bounce
  buffer. Four more drivers had the same bug.

### 6. Making it a phone (September 22 – October 5)

- **Modem without Android**: `ccci_mdinit`, the NVRAM HAL and `ccci_rpcd` under
  shims, plus an AT bridge to ModemManager. Incoming calls stayed silent until
  the modem got its `+EAIC` approval. Data goes through `ccmniN` selected by
  `+EIF`, not PPP. LTE needs an attach APN via `AT+EIAAPN`. In-call audio is
  switched by the vendor audio HAL ([hybris/bf7-modem](hybris/bf7-modem)).
- **Bluetooth**: bridged to a virtual HCI, with a spurious-wakeup fix in
  `BT_read` and the factory address read from `nvdata`.
- **Sleep**: s2idle works. Wakeups were audited through `/proc/*/status` and
  polling loops removed from the kernel and services. `hang_detect` reboots
  a frozen phone.
- **GPS**: `gps_drv` from a donor, the vendor `mnld` under a property shim,
  and HAL callbacks that are 112 bytes on MediaTek instead of AOSP's 96.
- Plus a torch (an LM3644 clone), ringtone vibration, a notification LED that
  was wired inverted (fixed in the kernel), night light through the MediaTek
  gamma block, a hotspot that keeps clients off the home LAN, and a Russian UI.

## Traps worth knowing first

1. **`ip=0x0` in a crash log almost always means libhybris** called into
   something the Android side doesn't have.
2. **The logic lives in Android daemons, not the kernel.** Battery, GPU,
   video and modem come alive through daemons from `/vendor`, not driver patches.
3. **Drivers exist in several copies of the source tree.** Find the `.o` in
   the build directory before editing anything.
4. **Rebuild the connectivity modules in the same pass as the kernel**, or
   systemd hangs and it looks like a boot failure. Flashing is one command:
   [`tools/bf7-proshit.sh`](tools/bf7-proshit.sh), with a module CRC check.
5. **`rmmod` on these drivers panics or hangs.**
6. **The framebuffer can lie.** Check the panel with a camera
   ([kernel-work/device-tests](kernel-work/device-tests)).
7. **The display once reported a 1 mHz refresh rate.** GTK redrew once every
   1,000 seconds, and the UI looked frozen.
8. **After a panic, check the root filesystem** (`e2fsck` from the ramdisk).
   Without an ext4 journal, corruption was routine.

## Repository layout

| Path | Contents |
|---|---|
| [docs/bf7-kernel](docs/bf7-kernel) | research log, sources, individual bug write-ups |
| [docs/bf7-native](docs/bf7-native) | how the running system works: GPU, camera, network, boot, recovery |
| [kernel-work/x657b-patches](kernel-work/x657b-patches) | our 40 kernel commits (`git am`) |
| [kernel-work/configs](kernel-work/configs) | kernel configs; current one is `bf7-x657b-v3.config` |
| [kernel-work](kernel-work) `bf7-*` | out-of-tree modules: backlight, Bluetooth, GPS, torch, connectivity fixes |
| [kernel-work/probes](kernel-work/probes) | small hardware probes: i2c, camera, touch, keys |
| [hybris/bf7-gpu](hybris/bf7-gpu) | services, udev rules, the Phosh session, installer `bf7-install.sh`, self-test |
| [hybris/bf7-modem](hybris/bf7-modem) | modem, calls, voice path |
| [hybris/bf7-codec2](hybris/bf7-codec2), [vaapi-shim](vaapi-shim) | hardware video |
| [hybris/bf7-camera](hybris/bf7-camera) | camera daemon, app, burst capture |
| [hybris/bf7-gps](hybris/bf7-gps) | GPS |
| [hybris/bf7-patches](hybris/bf7-patches), [patches](patches) | patches for libhybris, phosh, phoc, ModemManager, mpv, RetroArch, seatd, Xwayland, gsd |
| [gbm-backend](gbm-backend), [gui-wldrm](gui-wldrm) | GBM over gralloc, `wl_drm` |
| [tools](tools) | kernel flashing, wakeup measurement, AT-SPI clicks, probes |
| [golos-pc](golos-pc) | talk to the phone by voice: whisper + Piper on the PC |

## Building

This is a working port repository, not a distribution. It assumes you have
a BF7 in hand and are reading the log.

1. **Kernel**: [annel0/bf7-kernel-x657b](https://github.com/annel0/bf7-kernel-x657b),
   branch `bf7`. Or apply `kernel-work/x657b-patches/*.patch` with `git am` on
   [x657b_kernel_4.19](https://github.com/MostafaAshry513/x657b_kernel_4.19) at
   `75934cf7f`. Config: `kernel-work/configs/bf7-x657b-v3.config`. Toolchain:
   AOSP clang 11 (`clang-r383902`). Scripts are in [kernel-work/scripts](kernel-work/scripts).
2. **Modules** from `kernel-work/bf7-*` plus the Wi-Fi modules, built in the
   same pass (`build.sh` in each directory).
3. **Flash** with `tools/bf7-proshit.sh`. The candidate goes to the inactive
   slot; the known-good one stays.
4. **Userspace**: libhybris plus [the patch](hybris/bf7-patches/libhybris-bf7.patch),
   then `hybris/bf7-gpu/bf7-install.sh` (`--proverka` only shows the differences).

**No blobs here.** The PowerVR userspace, Codec2, audio and GPS HALs, and the
modem and Wi-Fi firmware all come from your own phone's `/vendor`.

⚠️ Before flashing anything, back up `boot_a`, `boot_b`, `para` and
`nvdata`/`nvram` (IMEI and calibration live there), and keep Android in one
slot. `mtkclient` was deliberately never used in this work.

## What doesn't work, and why

- **Front camera**: its sensor driver exists only inside the factory kernel
  binary, with no source anywhere.
- **Camera flash**: apparently not present on this board; the torch is a
  separate LED.
- **Charge limiting**: the AW32257 control driver doesn't load, so the command
  never reaches the chip.
- **GTK4 on the GPU**: early buffer commit gave half-drawn frames on the panel,
  so GTK4 renders with Cairo (60 fps).
- **Zero-copy in mpv/Firefox**: a gralloc and hardware limit, not an
  unfinished feature. Only GStreamer can hand frames to the panel without
  copying.

## Credits

- [MostafaAshry513/x657b_kernel_4.19](https://github.com/MostafaAshry513/x657b_kernel_4.19): the 4.19.325 MT6761 tree this kernel is based on.
- [libhybris](https://github.com/libhybris/libhybris): the bridge to Android drivers.
- MediaTek GPL drops scattered across other vendors' trees.
- Debian, Phosh, phoc/wlroots, ModemManager.

## Licenses

Kernel code and kernel modules are GPL-2.0, like the trees they came from.
Patches to other projects follow those projects' licenses. Android headers
are Apache-2.0. No license has been chosen yet for the rest of our own code.

## How it was made

Two of us: the phone's owner and Claude (Anthropic), working in Claude Code.
The research log is written by Claude, in the first person, as the work went.
Wrong conclusions were left in place and corrected by later entries, so you
can see how the search actually went.
