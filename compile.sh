#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build/linux-usb-console"
OUT_DIR="${ROOT_DIR}/build/output"
DOCKER_IMAGE="wing-usb-console-builder:trixie"
DOCKER_CONFIG_DIR="${BUILD_DIR}/docker-config"

if [[ -n "${DOCKER_HOST:-}" ]]; then
    DOCKER_HOST_URI="${DOCKER_HOST}"
elif [[ -S "${HOME}/.docker/run/docker.sock" ]]; then
    DOCKER_HOST_URI="unix://${HOME}/.docker/run/docker.sock"
else
    DOCKER_HOST_URI="unix:///var/run/docker.sock"
fi

LINUX_VER="6.6.30"
BUSYBOX_VER="1.36.1"
DROPBEAR_VER="2026.91"
DOOMGENERIC_REF="master"
HTOP_VER="3.3.0"
INCLUDE_DOOM="${INCLUDE_DOOM:-}"

UBOOT_IMX="${OUT_DIR}/u-boot-linux.imx"
OUTPUT_WINGFW="${OUT_DIR}/wing-compact-usb-console-linux.wingfw"

mkdir -p "${BUILD_DIR}" "${OUT_DIR}" "${DOCKER_CONFIG_DIR}"

DOCKER_CONFIG="${DOCKER_CONFIG_DIR}" DOCKER_HOST="${DOCKER_HOST_URI}" DOCKER_BUILDKIT=0 \
docker build -t "${DOCKER_IMAGE}" - <<'DOCKERFILE'
FROM debian:trixie
ENV DEBIAN_FRONTEND=noninteractive
RUN dpkg --add-architecture armhf \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
    bc \
    bison \
    bzip2 \
    ca-certificates \
    cpio \
    curl \
    device-tree-compiler \
    flex \
    gcc \
    gcc-arm-linux-gnueabihf \
    g++-arm-linux-gnueabihf \
    libc6-dev \
    libc6-dev-armhf-cross \
    libcrypt-dev:armhf \
    libncurses-dev:armhf \
    libssl-dev \
    make \
    patch \
    pkg-config \
    python3 \
    u-boot-tools \
    xz-utils \
  && rm -rf /var/lib/apt/lists/*
DOCKERFILE

echo "[build] omc (wing target)"
"${ROOT_DIR}/software/omc/compile-wing-docker.sh"

DOCKER_CONFIG="${DOCKER_CONFIG_DIR}" DOCKER_HOST="${DOCKER_HOST_URI}" \
docker run --rm \
  -i \
  -u "$(id -u):$(id -g)" \
  -v "${ROOT_DIR}:${ROOT_DIR}" \
  -w "${ROOT_DIR}" \
  -e ROOT_DIR="${ROOT_DIR}" \
  -e BUILD_DIR="${BUILD_DIR}" \
  -e OUT_DIR="${OUT_DIR}" \
  -e LINUX_VER="${LINUX_VER}" \
  -e BUSYBOX_VER="${BUSYBOX_VER}" \
  -e DROPBEAR_VER="${DROPBEAR_VER}" \
  -e DOOMGENERIC_REF="${DOOMGENERIC_REF}" \
  -e INCLUDE_DOOM="${INCLUDE_DOOM}" \
  -e HTOP_VER="${HTOP_VER}" \
  -e UBOOT_IMX="${UBOOT_IMX}" \
  -e OUTPUT_WINGFW="${OUTPUT_WINGFW}" \
  "${DOCKER_IMAGE}" \
  bash -s <<'BUILD'
set -euo pipefail

ARCH=arm
CROSS_COMPILE=arm-linux-gnueabihf-
ROOTFS_DIR="${BUILD_DIR}/rootfs"
LINUX_URL="https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${LINUX_VER}.tar.xz"
BUSYBOX_URL="https://busybox.net/downloads/busybox-${BUSYBOX_VER}.tar.bz2"
DROPBEAR_URL="https://matt.ucc.asn.au/dropbear/releases/dropbear-${DROPBEAR_VER}.tar.bz2"
DOOMGENERIC_URL="https://github.com/ozkl/doomgeneric/archive/refs/heads/${DOOMGENERIC_REF}.tar.gz"
DOOM_SHAREWARE_URL="https://archive.org/download/doom_20230531/doom_dos.ZIP"
HTOP_URL="https://github.com/htop-dev/htop/releases/download/${HTOP_VER}/htop-${HTOP_VER}.tar.xz"
UBOOT_VER="2024.01"
UBOOT_URL="https://ftp.denx.de/pub/u-boot/u-boot-${UBOOT_VER}.tar.bz2"

mkdir -p "${BUILD_DIR}" "${OUT_DIR}" "${ROOTFS_DIR}"
cd "${BUILD_DIR}"

if [[ ! -f "linux-${LINUX_VER}.tar.xz" ]]; then
    curl -L -o "linux-${LINUX_VER}.tar.xz" "${LINUX_URL}"
fi
if [[ ! -f "busybox-${BUSYBOX_VER}.tar.bz2" ]]; then
    curl -L -o "busybox-${BUSYBOX_VER}.tar.bz2" "${BUSYBOX_URL}"
fi
if [[ ! -f "dropbear-${DROPBEAR_VER}.tar.bz2" ]]; then
    curl -L -o "dropbear-${DROPBEAR_VER}.tar.bz2" "${DROPBEAR_URL}"
fi
if [[ -n "${INCLUDE_DOOM}" && ! -f "doomgeneric-${DOOMGENERIC_REF}.tar.gz" ]]; then
    curl -L -o "doomgeneric-${DOOMGENERIC_REF}.tar.gz" "${DOOMGENERIC_URL}"
fi
if [[ -n "${INCLUDE_DOOM}" && ! -f "doom_dos.zip" ]]; then
    curl -L -o "doom_dos.zip" "${DOOM_SHAREWARE_URL}"
fi
if [[ ! -f "htop-${HTOP_VER}.tar.xz" ]]; then
    curl -L -o "htop-${HTOP_VER}.tar.xz" "${HTOP_URL}"
fi
if [[ ! -f "u-boot-${UBOOT_VER}.tar.bz2" ]]; then
    curl -L -o "u-boot-${UBOOT_VER}.tar.bz2" "${UBOOT_URL}"
fi

if [[ ! -d "linux-${LINUX_VER}" ]]; then
    tar -xf "linux-${LINUX_VER}.tar.xz"
fi
if [[ ! -d "busybox-${BUSYBOX_VER}" ]]; then
    tar -xf "busybox-${BUSYBOX_VER}.tar.bz2"
fi
if [[ ! -d "dropbear-${DROPBEAR_VER}" ]]; then
    tar -xf "dropbear-${DROPBEAR_VER}.tar.bz2"
fi
if [[ -n "${INCLUDE_DOOM}" && ! -d "doomgeneric-${DOOMGENERIC_REF}" ]]; then
    tar -xf "doomgeneric-${DOOMGENERIC_REF}.tar.gz"
fi
if [[ ! -d "htop-${HTOP_VER}" ]]; then
    tar -xf "htop-${HTOP_VER}.tar.xz"
fi
if [[ ! -d "u-boot-${UBOOT_VER}" ]]; then
    tar -xf "u-boot-${UBOOT_VER}.tar.bz2"
fi

echo "[build] U-Boot bootloader"
cd "${BUILD_DIR}/u-boot-${UBOOT_VER}"
if [[ ! -f .patched ]]; then
    patch -p1 < "${ROOT_DIR}/u-boot/patches/behringer-wing-imx6s.patch"
    touch .patched
fi
cp "${ROOT_DIR}/u-boot/configs/"* configs/
mkdir -p "$(dirname "${UBOOT_IMX}")"

UBOOT_NEED_BUILD=0
if [[ ! -f "${UBOOT_IMX}" ]]; then
    UBOOT_NEED_BUILD=1
else
    # Check if config or patch is newer than the output
    for f in "${ROOT_DIR}/u-boot/patches/behringer-wing-imx6s.patch" "${ROOT_DIR}/u-boot/configs/"*; do
        if [[ "$f" -nt "${UBOOT_IMX}" ]]; then
            UBOOT_NEED_BUILD=1
            break
        fi
    done
fi

if [[ "${UBOOT_NEED_BUILD}" -eq 1 ]]; then
    rm -rf SPL
    make ARCH="${ARCH}" CROSS_COMPILE="${CROSS_COMPILE}" distclean
    make ARCH="${ARCH}" CROSS_COMPILE="${CROSS_COMPILE}" mx6d_linux_defconfig
    make ARCH="${ARCH}" CROSS_COMPILE="${CROSS_COMPILE}" -j"$(nproc)"
    cp u-boot-dtb.imx "${UBOOT_IMX}"
else
    echo "[build] U-Boot bootloader: up to date, skipping"
fi

rm -rf "${ROOTFS_DIR}"
mkdir -p "${ROOTFS_DIR}"

echo "[build] BusyBox static rootfs"
cd "${BUILD_DIR}/busybox-${BUSYBOX_VER}"
cp "${ROOT_DIR}/configs/config_busybox" .config
make ARCH="${ARCH}" CROSS_COMPILE="${CROSS_COMPILE}" -j"$(nproc)"
make ARCH="${ARCH}" CROSS_COMPILE="${CROSS_COMPILE}" CONFIG_PREFIX="${ROOTFS_DIR}" install

echo "[build] Dropbear SSH server"
cd "${BUILD_DIR}/dropbear-${DROPBEAR_VER}"
if [[ ! -f dropbearmulti ]]; then
    ./configure \
        --build="$(gcc -dumpmachine)" \
        --host="${CROSS_COMPILE%-}" \
        --prefix=/usr \
        --enable-static \
        --disable-zlib \
        --disable-shadow \
        --disable-lastlog \
        --disable-utmp \
        --disable-utmpx \
        --disable-wtmp \
        --disable-wtmpx \
        --disable-pututline \
        --disable-pututxline \
        --enable-bundled-libtom
    make clean
    make PROGRAMS="dropbear dropbearkey scp" STATIC=1 MULTI=1 -j"$(nproc)"
    "${CROSS_COMPILE}strip" dropbearmulti
else
    echo "[build] Dropbear SSH server: already built, skipping compilation"
fi
install -D -m 0755 dropbearmulti "${ROOTFS_DIR}/usr/sbin/dropbearmulti"
ln -sf /usr/sbin/dropbearmulti "${ROOTFS_DIR}/usr/sbin/dropbear"
ln -sf /usr/sbin/dropbearmulti "${ROOTFS_DIR}/usr/bin/dropbearkey"
ln -sf /usr/sbin/dropbearmulti "${ROOTFS_DIR}/usr/bin/scp"
mkdir -p "${ROOTFS_DIR}/etc/dropbear" "${ROOTFS_DIR}/root/.ssh"

if [[ -n "${INCLUDE_DOOM}" ]]; then
    echo "[build] Doom framebuffer port"
    cd "${BUILD_DIR}/doomgeneric-${DOOMGENERIC_REF}/doomgeneric"
    cp "${ROOT_DIR}/linux-tools/doomgeneric_wingfb.c" ./doomgeneric_wingfb.c
    make -f Makefile.linuxvt clean
    make -f Makefile.linuxvt \
        CC="${CROSS_COMPILE}gcc" \
        OUTPUT=wing-doom \
        LDFLAGS="-static -Wl,--gc-sections" \
        LIBS="-lm -lc" \
        SRC_DOOM="dummy.o am_map.o doomdef.o doomstat.o dstrings.o d_event.o d_items.o d_iwad.o d_loop.o d_main.o d_mode.o d_net.o f_finale.o f_wipe.o g_game.o hu_lib.o hu_stuff.o info.o i_cdmus.o i_endoom.o i_joystick.o i_scale.o i_sound.o i_system.o i_timer.o memio.o m_argv.o m_bbox.o m_cheat.o m_config.o m_controls.o m_fixed.o m_menu.o m_misc.o m_random.o p_ceilng.o p_doors.o p_enemy.o p_floor.o p_inter.o p_lights.o p_map.o p_maputl.o p_mobj.o p_plats.o p_pspr.o p_saveg.o p_setup.o p_sight.o p_spec.o p_switch.o p_telept.o p_tick.o p_user.o r_bsp.o r_data.o r_draw.o r_main.o r_plane.o r_segs.o r_sky.o r_things.o sha1.o sounds.o statdump.o st_lib.o st_stuff.o s_sound.o tables.o v_video.o wi_stuff.o w_checksum.o w_file.o w_main.o w_wad.o z_zone.o w_file_stdc.o i_input.o i_video.o doomgeneric.o doomgeneric_wingfb.o mus2mid.o" \
        -j"$(nproc)"
    "${CROSS_COMPILE}strip" wing-doom
    install -D -m 0755 wing-doom "${ROOTFS_DIR}/usr/bin/wing-doom.real"
    mkdir -p "${ROOTFS_DIR}/usr/share/doom"
    python3 - "${BUILD_DIR}/doom_dos.zip" "${ROOTFS_DIR}/usr/share/doom/doom1.wad" <<'PY'
import hashlib
import sys
import zipfile

archive, output = sys.argv[1:3]
expected_sha256 = "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771"

with zipfile.ZipFile(archive) as zf:
    wad_name = next((name for name in zf.namelist() if name.lower().endswith("doom1.wad")), None)
    if wad_name is None:
        raise SystemExit("doom1.wad not found in shareware archive")
    data = zf.read(wad_name)

actual_sha256 = hashlib.sha256(data).hexdigest()
if actual_sha256 != expected_sha256:
    raise SystemExit(f"unexpected doom1.wad sha256: {actual_sha256}")

with open(output, "wb") as f:
    f.write(data)
PY
    cat > "${ROOTFS_DIR}/usr/bin/wing-doom" <<'EOF'
#!/bin/sh
exec /usr/bin/wing-doom.real -iwad /usr/share/doom/doom1.wad -nosound "$@"
EOF
    chmod 0755 "${ROOTFS_DIR}/usr/bin/wing-doom"
    cat > "${ROOTFS_DIR}/usr/bin/doom" <<'EOF'
#!/bin/sh
exec /usr/bin/wing-doom "$@"
EOF
    chmod 0755 "${ROOTFS_DIR}/usr/bin/doom"
else
    echo "[build] Doom framebuffer port: skipped (set INCLUDE_DOOM=1 to include)"
fi

echo "[build] htop"
cd "${BUILD_DIR}/htop-${HTOP_VER}"
if [[ ! -f htop ]]; then
    make distclean >/dev/null 2>&1 || true
    PKG_CONFIG_LIBDIR="/usr/lib/arm-linux-gnueabihf/pkgconfig:/usr/share/pkgconfig" \
    ./configure \
        --build="$(gcc -dumpmachine)" \
        --host="${CROSS_COMPILE%-}" \
        --prefix=/usr \
        --enable-static \
        --disable-unicode \
        --disable-hwloc \
        --disable-sensors \
        --disable-affinity \
        --disable-capabilities \
        LIBS="-ltinfo"
    make -j"$(nproc)" LDFLAGS="-static"
    "${CROSS_COMPILE}strip" htop
else
    echo "[build] htop: already built, skipping compilation"
fi
install -D -m 0755 htop "${ROOTFS_DIR}/usr/bin/htop"

echo "[install] omc (wing target)"
OMC_BIN="${ROOT_DIR}/software/omc/build/wing/wing-omc"
if [ ! -f "${OMC_BIN}" ]; then
    echo "[error] omc build did not produce ${OMC_BIN}" >&2
    exit 1
fi
"${CROSS_COMPILE}strip" "${OMC_BIN}" || true
install -D -m 0755 "${OMC_BIN}" "${ROOTFS_DIR}/usr/bin/wing-omc"

echo "[build] Copy terminfo database"
mkdir -p "${ROOTFS_DIR}/lib/terminfo"
for term in x/xterm x/xterm-color x/xterm-256color x/xterm-ghostty g/ghostty l/linux a/ansi v/vt100 v/vt102 v/vt220 d/dumb; do
    if [ -f "/lib/terminfo/${term}" ]; then
        mkdir -p "${ROOTFS_DIR}/lib/terminfo/$(dirname ${term})"
        cp "/lib/terminfo/${term}" "${ROOTFS_DIR}/lib/terminfo/${term}"
    elif [ -f "/usr/share/terminfo/${term}" ]; then
        mkdir -p "${ROOTFS_DIR}/lib/terminfo/$(dirname ${term})"
        cp "/usr/share/terminfo/${term}" "${ROOTFS_DIR}/lib/terminfo/${term}"
    fi
done

# If the build host has no Ghostty terminfo entry, fall back to xterm-256color
# so SSH clients using TERM=xterm-ghostty or TERM=ghostty still work.
if [ -f "${ROOTFS_DIR}/lib/terminfo/x/xterm-256color" ]; then
    mkdir -p "${ROOTFS_DIR}/lib/terminfo/x" "${ROOTFS_DIR}/lib/terminfo/g"
    [ -f "${ROOTFS_DIR}/lib/terminfo/x/xterm-ghostty" ] || \
        ln -sf xterm-256color "${ROOTFS_DIR}/lib/terminfo/x/xterm-ghostty"
    [ -f "${ROOTFS_DIR}/lib/terminfo/g/ghostty" ] || \
        ln -sf ../x/xterm-256color "${ROOTFS_DIR}/lib/terminfo/g/ghostty"
fi

echo "[build] diagnostic UART tools"
mkdir -p "${ROOTFS_DIR}/usr/bin"
"${CROSS_COMPILE}gcc" -Os -static -Wall -Wextra -o "${ROOTFS_DIR}/usr/bin/wing_panel_init" \
    "${ROOT_DIR}/linux-tools/wing_panel_init.c"
"${CROSS_COMPILE}gcc" -Os -static -Wall -Wextra -o "${ROOTFS_DIR}/usr/bin/wing-surface-console" \
    "${ROOT_DIR}/linux-tools/wing_surface_console.c" \
    "${ROOT_DIR}/linux-tools/wing_surface_common.c" \
    "${ROOT_DIR}/linux-tools/wing_control_names.c" \
    -lncurses -ltinfo
"${CROSS_COMPILE}gcc" -Os -static -Wall -Wextra -o "${ROOTFS_DIR}/usr/bin/wing-draw" \
    "${ROOT_DIR}/linux-tools/wing_draw.c"
"${CROSS_COMPILE}gcc" -Os -static -Wall -Wextra -o "${ROOTFS_DIR}/usr/bin/pnlc_raw_dump" \
    "${ROOT_DIR}/linux-tools/pnlc_raw_dump.c"
cd "${ROOTFS_DIR}"
cp -a "${ROOT_DIR}/initramfs_root/"* .
mkdir -p dev proc root sys tmp
find . -print0 | cpio --null -o -H newc > "${BUILD_DIR}/initramfs.rootfs.cpio"
python3 - "${BUILD_DIR}/initramfs.rootfs.cpio" "${BUILD_DIR}/initramfs.cpio" <<'PY'
import stat
import sys
import time

src, dst = sys.argv[1:3]
data = open(src, "rb").read()


def align4(value):
    return (value + 3) & ~3


def find_trailer_offset(blob):
    offset = 0
    while offset + 110 <= len(blob):
        entry_start = offset
        header = blob[offset:offset + 110]
        if header[:6] not in (b"070701", b"070702"):
            raise SystemExit(f"invalid cpio header at offset {offset}")
        offset += 110
        namesize = int(header[94:102], 16)
        filesize = int(header[54:62], 16)
        name = blob[offset:offset + namesize].rstrip(b"\0")
        offset = align4(offset + namesize)
        offset = align4(offset + filesize)
        if name == b"TRAILER!!!":
            return entry_start
    raise SystemExit("cpio TRAILER!!! entry not found")


def newc_entry(name, mode, rdevmajor=0, rdevminor=0, inode=0):
    encoded_name = name.encode() + b"\0"
    fields = [
        b"070701",
        f"{inode:08x}".encode(),
        f"{mode:08x}".encode(),
        b"00000000",
        b"00000000",
        b"00000001",
        f"{int(time.time()):08x}".encode(),
        b"00000000",
        b"00000000",
        b"00000000",
        f"{rdevmajor:08x}".encode(),
        f"{rdevminor:08x}".encode(),
        f"{len(encoded_name):08x}".encode(),
        b"00000000",
    ]
    header = b"".join(fields)
    return header + encoded_name + (b"\0" * ((4 - (len(header) + len(encoded_name)) % 4) % 4))


trailer_offset = find_trailer_offset(data)
out = bytearray(data[:trailer_offset])
out += newc_entry("dev/console", stat.S_IFCHR | 0o600, 5, 1, 0x100001)
out += newc_entry("dev/null", stat.S_IFCHR | 0o666, 1, 3, 0x100002)
out += newc_entry("TRAILER!!!", 0)
out += b"\0" * ((512 - len(out) % 512) % 512)
open(dst, "wb").write(out)
PY
gzip -9 -f "${BUILD_DIR}/initramfs.cpio"

echo "[build] Linux kernel + unified console/display DTS"
cd "${BUILD_DIR}/linux-${LINUX_VER}"
cp "${ROOT_DIR}/configs/imx6dl-wing-usb-console.dts" arch/arm/boot/dts/nxp/imx/imx6dl-wing-usb-console.dts

if ! grep -q "imx6dl-wing-usb-console.dtb" arch/arm/boot/dts/nxp/imx/Makefile; then
    echo 'dtb-$(CONFIG_SOC_IMX6Q) += imx6dl-wing-usb-console.dtb' >> arch/arm/boot/dts/nxp/imx/Makefile
fi

cp "${ROOT_DIR}/configs/config_linux" .config
scripts/config --set-str INITRAMFS_SOURCE "${BUILD_DIR}/initramfs.cpio.gz"
scripts/config --enable INPUT
scripts/config --enable INPUT_EVDEV
scripts/config --enable HID_SUPPORT
scripts/config --enable HID
scripts/config --enable HID_GENERIC
scripts/config --enable USB_EHCI_HCD
scripts/config --enable USB_EHCI_ROOT_HUB_TT
scripts/config --enable USB_EHCI_TT_NEWSCHED
scripts/config --enable USB_EHCI_HCD_PLATFORM
scripts/config --enable USB_CHIPIDEA_HOST
scripts/config --enable USB_HID

make ARCH="${ARCH}" CROSS_COMPILE="${CROSS_COMPILE}" olddefconfig
make ARCH="${ARCH}" CROSS_COMPILE="${CROSS_COMPILE}" -j"$(nproc)" zImage nxp/imx/imx6dl-wing-usb-console.dtb

cp arch/arm/boot/zImage "${OUT_DIR}/wing-usb-console-zImage"
cp arch/arm/boot/dts/nxp/imx/imx6dl-wing-usb-console.dtb "${OUT_DIR}/imx6dl-wing-usb-console.dtb"
cp "${BUILD_DIR}/initramfs.cpio.gz" "${OUT_DIR}/wing-usb-console-initramfs.cpio.gz"

echo "[build] wingfw package"
python3 "${ROOT_DIR}/tools/build_linux_wingfw.py" \
    --uboot-imx "${UBOOT_IMX}" \
    --kernel "${OUT_DIR}/wing-usb-console-zImage" \
    --dtb "${OUT_DIR}/imx6dl-wing-usb-console.dtb" \
    --initramfs "${OUT_DIR}/wing-usb-console-initramfs.cpio.gz" \
    --fit-output "${OUT_DIR}/wing-usb-console.itb" \
    --version "linux-usb-console" \
    -o "${OUTPUT_WINGFW}"

echo "[done] ${OUTPUT_WINGFW}"
BUILD
