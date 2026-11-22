#!/usr/bin/env bash
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

case "${WING_PROFILE:-legacy}" in
    legacy) OMC_BIN="${PAYLOAD_DIR:?run compile.sh}/omc" ;;
    t55-recovery|t55-spibone) OMC_BIN="${PAYLOAD_DIR:?run compile.sh}/wing-t55/omc" ;;
    *) echo "[error] unknown WING_PROFILE: ${WING_PROFILE}" >&2; exit 1 ;;
esac
if [[ ! -s "${OMC_BIN}" ]]; then
    echo "[error] missing staged ARM OMC: ${OMC_BIN}" >&2
    exit 1
fi
if [[ "${WING_PROFILE:-legacy}" != legacy ]]; then
    (
        cd "${PAYLOAD_DIR}/wing-t55"
        sha256sum --strict -c SHA256SUMS
    )
    omc_elf="$(LC_ALL=C "${CROSS_COMPILE}readelf" -h -l "${OMC_BIN}")"
    if [[ "${omc_elf}" != *ELF32* || "${omc_elf}" != *'hard-float ABI'* || \
          "${omc_elf}" == *INTERP* || "${omc_elf}" == *DYNAMIC* ]] || \
        ! grep -Eq 'Machine:[[:blank:]]+ARM$' <<<"${omc_elf}" || \
        ! grep -Eq 'Type:[[:blank:]]+EXEC ' <<<"${omc_elf}"; then
        echo "[error] expected a static ARM hard-float OMC executable" >&2
        exit 1
    fi
fi

if [[ "${WING_PROFILE:-legacy}" == t55-spibone ]]; then
    # Keep the build host, user, time and incrementing kernel build number out of the image.
    umask 022
    export LC_ALL=C TZ=UTC SOURCE_DATE_EPOCH=1767225600
    export KBUILD_BUILD_TIMESTAMP="$(date -u -d "@${SOURCE_DATE_EPOCH}")"
    export KBUILD_BUILD_USER=openwing KBUILD_BUILD_HOST=builder KBUILD_BUILD_VERSION=1
fi

#######################################################################
#
#  ########  ########  ######## ########     ###    ########  ######## 
#  ##     ## ##     ## ##       ##     ##   ## ##   ##     ## ##       
#  ##     ## ##     ## ##       ##     ##  ##   ##  ##     ## ##       
#  ########  ########  ######   ########  ##     ## ########  ######   
#  ##        ##   ##   ##       ##        ######### ##   ##   ##       
#  ##        ##    ##  ##       ##        ##     ## ##    ##  ##       
#  ##        ##     ## ######## ##        ##     ## ##     ## ######## 
#
#######################################################################

mkdir -p "${BUILD_DIR}" "${OUT_DIR}" "${ROOTFS_DIR}"
cd "${BUILD_DIR}"

download() {
    if [[ "${WING_PROFILE:-legacy}" != t55-spibone ]]; then
        curl -L -o "$1" "$2"
    elif curl --fail --location --connect-timeout 15 --max-time 300 -o "$1.part" "$2"; then
        mv "$1.part" "$1"
    else
        rm -f "$1.part"
        return 1
    fi
}

if [[ ! -f "linux-${LINUX_VER}.tar.xz" ]]; then
    download "linux-${LINUX_VER}.tar.xz" "${LINUX_URL}"
fi
if [[ ! -f "busybox-${BUSYBOX_VER}.tar.bz2" ]]; then
    download "busybox-${BUSYBOX_VER}.tar.bz2" "${BUSYBOX_URL}"
fi
if [[ ! -f "dropbear-${DROPBEAR_VER}.tar.bz2" ]]; then
    download "dropbear-${DROPBEAR_VER}.tar.bz2" "${DROPBEAR_URL}"
fi
if [[ -n "${INCLUDE_DOOM}" && ! -f "doomgeneric-${DOOMGENERIC_REF}.tar.gz" ]]; then
    download "doomgeneric-${DOOMGENERIC_REF}.tar.gz" "${DOOMGENERIC_URL}"
fi
if [[ -n "${INCLUDE_DOOM}" && ! -f "doom_dos.zip" ]]; then
    download "doom_dos.zip" "${DOOM_SHAREWARE_URL}"
fi
if [[ ! -f "htop-${HTOP_VER}.tar.xz" ]]; then
    download "htop-${HTOP_VER}.tar.xz" "${HTOP_URL}"
fi
if [[ ! -f "u-boot-${UBOOT_VER}.tar.bz2" ]]; then
    download "u-boot-${UBOOT_VER}.tar.bz2" "${UBOOT_URL}"
fi

if [[ "${WING_PROFILE:-legacy}" == t55-spibone ]]; then
    sha256sum --strict -c "${ROOT_DIR}/configs/t55-build.SHA256SUMS"
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

###########################################################
#
#  ##     ## ########   #######   #######  ######## 
#  ##     ## ##     ## ##     ## ##     ##    ##    
#  ##     ## ##     ## ##     ## ##     ##    ##    
#  ##     ## ########  ##     ## ##     ##    ##    
#  ##     ## ##     ## ##     ## ##     ##    ##    
#  ##     ## ##     ## ##     ## ##     ##    ##    
#   #######  ########   #######   #######     ##    
#
###########################################################

echo "[build] U-Boot bootloader"
cd "${BUILD_DIR}/u-boot-${UBOOT_VER}"
if [[ ! -f .patched ]]; then
    patch -p1 < "${ROOT_DIR}/patches/uboot/behringer-wing-imx6s.patch"
    touch .patched
fi
cp "${ROOT_DIR}/configs/uboot/"* configs/
mkdir -p "$(dirname "${UBOOT_IMX}")"

UBOOT_NEED_BUILD=0
if [[ ! -f "${UBOOT_IMX}" ]]; then
    UBOOT_NEED_BUILD=1
else
    # Check if config or patch is newer than the output
    for f in "${ROOT_DIR}/patches/uboot/behringer-wing-imx6s.patch" "${ROOT_DIR}/configs/uboot/"*; do
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

##########################################################################
#
#  ########   #######   #######  ######## ########  ######  
#  ##     ## ##     ## ##     ##    ##    ##       ##    ## 
#  ##     ## ##     ## ##     ##    ##    ##       ##       
#  ########  ##     ## ##     ##    ##    ######    ######  
#  ##   ##   ##     ## ##     ##    ##    ##             ## 
#  ##    ##  ##     ## ##     ##    ##    ##       ##    ## 
#  ##     ##  #######   #######     ##    ##        ######  
#
##########################################################################


rm -rf "${ROOTFS_DIR}"
mkdir -p "${ROOTFS_DIR}"

##########################################################################
#
#  ########  ##     ##  ######  ##    ## ########   #######  ##      ## 
#  ##     ## ##     ## ##    ##  ##  ##  ##     ## ##     ##  ##    ##  
#  ##     ## ##     ## ##         ####   ##     ## ##     ##   ##  ##   
#  ########  ##     ##  ######     ##    ########  ##     ##    ####    
#  ##     ## ##     ##       ##    ##    ##     ## ##     ##   ##  ##   
#  ##     ## ##     ## ##    ##    ##    ##     ## ##     ##  ##    ##  
#  ########   #######   ######     ##    ########   #######  ##      ## 
#
#########################################################################-

echo "[build] BusyBox static rootfs"
cd "${BUILD_DIR}/busybox-${BUSYBOX_VER}"
cp "${ROOT_DIR}/configs/config_busybox" .config

if [ $# -ge 1 ] && [ "$1" == "busybox_menuconfig" ]; then

    make ARCH="${ARCH}" menuconfig

    # copy config back
    cp .config "${ROOT_DIR}/configs/config_busybox"

fi

make ARCH="${ARCH}" CROSS_COMPILE="${CROSS_COMPILE}" -j"$(nproc)"
make ARCH="${ARCH}" CROSS_COMPILE="${CROSS_COMPILE}" CONFIG_PREFIX="${ROOTFS_DIR}" install

#####################################################################################
#
#  ########  ########   #######  ########  ########  ########    ###    ########  
#  ##     ## ##     ## ##     ## ##     ## ##     ## ##         ## ##   ##     ## 
#  ##     ## ##     ## ##     ## ##     ## ##     ## ##        ##   ##  ##     ## 
#  ##     ## ########  ##     ## ########  ########  ######   ##     ## ########  
#  ##     ## ##   ##   ##     ## ##        ##     ## ##       ######### ##   ##   
#  ##     ## ##    ##  ##     ## ##        ##     ## ##       ##     ## ##    ##  
#  ########  ##     ##  #######  ##        ########  ######## ##     ## ##     ## 
#
#####################################################################################

echo "[build] Dropbear SSH server"
cd "${BUILD_DIR}/dropbear-${DROPBEAR_VER}"
cat > localoptions.h.tmp <<'EOF'
/* OpenWING initramfs SSH settings.
 *
 * Disable REEXEC: static BusyBox/musl/glibc binaries can deadlock or lose
 * the childpipe fd across re-exec under rapid connection bursts.
 * Increase unauthenticated connection limits and enable aggressive keepalive.
 */
#define DROPBEAR_REEXEC 0
#define MAX_UNAUTH_PER_IP 128
#define MAX_UNAUTH_CLIENTS 256
#define MAX_AUTH_TRIES 20
#define DROPBEAR_MAX_CLI_PASS 20
#define DEFAULT_KEEPALIVE 10
EOF
if ! cmp -s localoptions.h.tmp localoptions.h; then
    mv localoptions.h.tmp localoptions.h
else
    rm localoptions.h.tmp
fi

DROPBEAR_NEED_BUILD=0
if [[ ! -f dropbearmulti ]]; then
    DROPBEAR_NEED_BUILD=1
elif [[ localoptions.h -nt dropbearmulti || "${ROOT_DIR}/compile.sh" -nt dropbearmulti ]]; then
    DROPBEAR_NEED_BUILD=1
fi

if [[ "${DROPBEAR_NEED_BUILD}" -eq 1 ]]; then
    make distclean >/dev/null 2>&1 || true
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
    echo "[build] Dropbear SSH server: up to date, skipping compilation"
fi
install -D -m 0755 dropbearmulti "${ROOTFS_DIR}/usr/sbin/dropbearmulti"
ln -sf /usr/sbin/dropbearmulti "${ROOTFS_DIR}/usr/sbin/dropbear"
ln -sf /usr/sbin/dropbearmulti "${ROOTFS_DIR}/usr/bin/dropbearkey"
ln -sf /usr/sbin/dropbearmulti "${ROOTFS_DIR}/usr/bin/scp"
mkdir -p "${ROOTFS_DIR}/etc/dropbear" "${ROOTFS_DIR}/root/.ssh"

#############################################
#
#  ########   #######   #######  ##     ## 
#  ##     ## ##     ## ##     ## ###   ### 
#  ##     ## ##     ## ##     ## #### #### 
#  ##     ## ##     ## ##     ## ## ### ## 
#  ##     ## ##     ## ##     ## ##     ## 
#  ##     ## ##     ## ##     ## ##     ## 
#  ########   #######   #######  ##     ## 
#
#############################################


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

#########################################
#
#  ##     ## ########  #######  ########  
#  ##     ##    ##    ##     ## ##     ## 
#  ##     ##    ##    ##     ## ##     ## 
#  #########    ##    ##     ## ########  
#  ##     ##    ##    ##     ## ##        
#  ##     ##    ##    ##     ## ##        
#  ##     ##    ##     #######  ##        
#
#########################################

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

########################################################################################################################################################
#
#   #######  ########  ######## ##    ## ##     ## #### ##      ## ######## ########   ######   #######  ##    ## ######## ########   #######  ##       
#  ##     ## ##     ## ##       ###   ## ###   ###  ##   ##    ##  ##       ##     ## ##    ## ##     ## ###   ##    ##    ##     ## ##     ## ##       
#  ##     ## ##     ## ##       ####  ## #### ####  ##    ##  ##   ##       ##     ## ##       ##     ## ####  ##    ##    ##     ## ##     ## ##       
#  ##     ## ########  ######   ## ## ## ## ### ##  ##     ####    ######   ########  ##       ##     ## ## ## ##    ##    ########  ##     ## ##       
#  ##     ## ##        ##       ##  #### ##     ##  ##    ##  ##   ##       ##   ##   ##       ##     ## ##  ####    ##    ##   ##   ##     ## ##       
#  ##     ## ##        ##       ##   ### ##     ##  ##   ##    ##  ##       ##    ##  ##    ## ##     ## ##   ###    ##    ##    ##  ##     ## ##       
#   #######  ##        ######## ##    ## ##     ## #### ##      ## ######## ##     ##  ######   #######  ##    ##    ##    ##     ##  #######  ######## 
#
########################################################################################################################################################

echo "[install] omc (wing target)"
if [[ "${WING_PROFILE:-legacy}" != legacy ]]; then
    bundle="${ROOTFS_DIR}/usr/share/wing-t55"
    mkdir -p "${bundle}"
    cp -R "${PAYLOAD_DIR}/wing-t55/." "${bundle}/"
    find "${bundle}" -type f -exec chmod 0444 {} +
    chmod 0555 "${bundle}/omc"
    mkdir -p "${ROOTFS_DIR}/etc"
    touch "${ROOTFS_DIR}/etc/wing-${WING_PROFILE/t55-spibone/t55-bringup}"
else
    install -D -m 0755 "${OMC_BIN}" "${ROOTFS_DIR}/usr/bin/omc"
    "${CROSS_COMPILE}strip" "${ROOTFS_DIR}/usr/bin/omc" || true
fi

#########################################################################
#
#  ######## ######## ########  ##     ## #### ##    ## ########  #######  
#     ##    ##       ##     ## ###   ###  ##  ###   ## ##       ##     ## 
#     ##    ##       ##     ## #### ####  ##  ####  ## ##       ##     ## 
#     ##    ######   ########  ## ### ##  ##  ## ## ## ######   ##     ## 
#     ##    ##       ##   ##   ##     ##  ##  ##  #### ##       ##     ## 
#     ##    ##       ##    ##  ##     ##  ##  ##   ### ##       ##     ## 
#     ##    ######## ##     ## ##     ## #### ##    ## ##        #######  
#
#########################################################################

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

###################################################
#
#  ########  #######   #######  ##        ######  
#     ##    ##     ## ##     ## ##       ##    ## 
#     ##    ##     ## ##     ## ##       ##       
#     ##    ##     ## ##     ## ##        ######  
#     ##    ##     ## ##     ## ##             ## 
#     ##    ##     ## ##     ## ##       ##    ## 
#     ##     #######   #######  ########  ######  
#
###################################################

echo "[build] System Tools"

"${CROSS_COMPILE}gcc" -Os -static -Wall -Wextra -o "${ROOTFS_DIR}/usr/bin/wing-sysconfig" "${ROOT_DIR}/software/wing-system-tools/wing-sysconfig.c"
"${CROSS_COMPILE}gcc" -Os -static -Wall -Wextra -o "${ROOTFS_DIR}/usr/bin/wing-panel-init" "${ROOT_DIR}/software/wing-system-tools/wing-panel-init.c"
if [[ "${WING_PROFILE:-legacy}" == legacy ]]; then
    "${CROSS_COMPILE}gcc" -Os -static -Wall -Wextra -o "${ROOTFS_DIR}/usr/bin/wing-fpga-dsp-tool" "${ROOT_DIR}/software/wing-system-tools/wing-fpga-dsp-tool.c"
fi

echo "[build] Audio Tools"

if [[ "${WING_PROFILE:-legacy}" == t55-spibone ]]; then
    "${CROSS_COMPILE}gcc" -Os -static -Wall -Wextra -I"${PAYLOAD_DIR}" -I"${PAYLOAD_DIR}/spibone" \
        -o "${ROOTFS_DIR}/usr/bin/wing-t55" "${ROOT_DIR}/software/wing-system-tools/wing-t55.c"
else
    "${CROSS_COMPILE}gcc" -Os -static -Wall -Wextra -o "${ROOTFS_DIR}/usr/bin/wing-audio-clock" "${ROOT_DIR}/software/wing-audio-clock/wing-audio-clock.c"
fi

echo "[build] Delevopment Tools"
if [[ "${WING_PROFILE:-legacy}" == legacy ]]; then
    "${CROSS_COMPILE}gcc" -Os -static -Wall -Wextra -o "${ROOTFS_DIR}/usr/bin/test_all_pins" "${ROOT_DIR}/tools/fpga-pin-monitor/test_all_pins.c"
fi

###################################################################################
#
#   #### ##    ## #### ######## ########     ###    ##     ## ########  ######  
#    ##  ###   ##  ##     ##    ##     ##   ## ##   ###   ### ##       ##    ## 
#    ##  ####  ##  ##     ##    ##     ##  ##   ##  #### #### ##       ##       
#    ##  ## ## ##  ##     ##    ########  ##     ## ## ### ## ######    ######  
#    ##  ##  ####  ##     ##    ##   ##   ######### ##     ## ##             ## 
#    ##  ##   ###  ##     ##    ##    ##  ##     ## ##     ## ##       ##    ## 
#   #### ##    ## ####    ##    ##     ## ##     ## ##     ## ##        ######  
#
###################################################################################

cd "${ROOTFS_DIR}"
cp -a "${ROOT_DIR}/initramfs_root/"* .
mkdir -p dev proc root sys tmp
cpio_owner=()
if [[ "${WING_PROFILE:-legacy}" != legacy ]]; then
    rm -f etc/dropbear/dropbear_ed25519_host_key
    (cd usr/share/wing-t55 && sha256sum --strict -c SHA256SUMS)
    cpio_owner=(--owner=0:0)
fi
if [[ "${WING_PROFILE:-legacy}" == t55-spibone ]]; then
    find . -exec touch -h -d "@${SOURCE_DATE_EPOCH}" {} +
    find . -print0 | sort -z | cpio --null -o -H newc --reproducible "${cpio_owner[@]}" > "${BUILD_DIR}/initramfs.rootfs.cpio"
else
    find . -print0 | cpio --null -o -H newc "${cpio_owner[@]}" > "${BUILD_DIR}/initramfs.rootfs.cpio"
fi
python3 - "${BUILD_DIR}/initramfs.rootfs.cpio" "${BUILD_DIR}/initramfs.cpio" <<'PY'
import os
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
        f"{int(os.environ.get('SOURCE_DATE_EPOCH') or time.time()):08x}".encode(),
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

gzip_options=()
if [[ "${WING_PROFILE:-legacy}" == t55-spibone ]]; then gzip_options=(-n); fi
gzip -9 -f "${gzip_options[@]}" "${BUILD_DIR}/initramfs.cpio"

#############################################################
#
#  ##     ## ######## ########  ##    ## ######## ##       
#  ##    ##  ##       ##     ## ###   ## ##       ##       
#  ##   ##   ##       ##     ## ####  ## ##       ##       
#  #####     ######   ########  ## ## ## ######   ##       
#  ##   ##   ##       ##   ##   ##  #### ##       ##       
#  ##    ##  ##       ##    ##  ##   ### ##       ##       
#  ##     ## ######## ##     ## ##    ## ######## ######## 
#
#############################################################

echo "[build] Linux kernel + unified console/display DTS"
cd "${BUILD_DIR}/linux-${LINUX_VER}"
cp "${ROOT_DIR}/configs/imx6dl-wing-usb-console.dts" arch/arm/boot/dts/nxp/imx/imx6dl-wing-usb-console.dts
if [[ "${WING_PROFILE:-legacy}" == t55-spibone ]]; then
    cat "${ROOT_DIR}/configs/imx6dl-wing-t55-bringup.dtsi" >> arch/arm/boot/dts/nxp/imx/imx6dl-wing-usb-console.dts
    cat "${ROOT_DIR}/configs/imx6dl-wing-t55-spibone.dtsi" >> arch/arm/boot/dts/nxp/imx/imx6dl-wing-usb-console.dts
    echo "[build] In-tree SPIBone driver from OpenAudioFPGA"
    install -D -m 0644 "${ROOT_DIR}/configs/spibone/Kconfig" drivers/misc/spibone/Kconfig
    install -m 0644 "${ROOT_DIR}/configs/spibone/Kbuild" drivers/misc/spibone/
    cp "${PAYLOAD_DIR}/spibone/"* drivers/misc/spibone/
    if ! grep -Fxq 'source "drivers/misc/spibone/Kconfig"' drivers/misc/Kconfig; then
        printf '\nsource "drivers/misc/spibone/Kconfig"\n' >> drivers/misc/Kconfig
    fi
    if ! grep -Fxq 'obj-$(CONFIG_SPIBONE) += spibone/' drivers/misc/Makefile; then
        printf '\nobj-$(CONFIG_SPIBONE) += spibone/\n' >> drivers/misc/Makefile
    fi
fi

if ! grep -q "imx6dl-wing-usb-console.dtb" arch/arm/boot/dts/nxp/imx/Makefile; then
    echo 'dtb-$(CONFIG_SOC_IMX6Q) += imx6dl-wing-usb-console.dtb' >> arch/arm/boot/dts/nxp/imx/Makefile
fi

if [ $# -ge 1 ] && [ "$1" == "kernel_menuconfig" ]; then

    # get config
    cp "${ROOT_DIR}/configs/config_linux" .config
    
    # start menuconfig
    make ARCH="${ARCH}" CROSS_COMPILE="${CROSS_COMPILE}" olddefconfig
    make ARCH="${ARCH}" menuconfig

    # store config back, so we can commit it (you need to save the config in menuconfig to make this work!)
    cp .config "${ROOT_DIR}/configs/config_linux"

fi
    
cp "${ROOT_DIR}/configs/config_linux" .config
if [[ "${WING_PROFILE:-legacy}" == t55-spibone ]]; then
    # U-Boot replaces chosen.bootargs. Extend its arguments only in this kernel.
    scripts/config --disable CMDLINE_FROM_BOOTLOADER --enable CMDLINE_EXTEND \
        --set-str CMDLINE 'spidev.bufsiz=65536' --enable CRYPTO_USER_API_HASH --enable SPIBONE
fi

make ARCH="${ARCH}" CROSS_COMPILE="${CROSS_COMPILE}" olddefconfig
if [[ "${WING_PROFILE:-legacy}" == t55-spibone ]] && ! grep -Fxq CONFIG_SPIBONE=y .config; then
    echo '[error] SPIBone was not enabled in the kernel configuration' >&2
    exit 1
fi
make ARCH="${ARCH}" CROSS_COMPILE="${CROSS_COMPILE}" -j"$(nproc)" zImage nxp/imx/imx6dl-wing-usb-console.dtb
if [[ "${WING_PROFILE:-legacy}" == t55-spibone ]] && ! grep -qw spibone_probe System.map; then
    echo '[error] SPIBone driver missing from linked ARM kernel' >&2
    exit 1
fi

############################################################
#
#  ##      ## #### ##    ##  ######   ######## ##      ## 
#  ##  ##  ##  ##  ###   ## ##    ##  ##       ##  ##  ## 
#  ##  ##  ##  ##  ####  ## ##        ##       ##  ##  ## 
#  ##  ##  ##  ##  ## ## ## ##   #### ######   ##  ##  ## 
#  ##  ##  ##  ##  ##  #### ##    ##  ##       ##  ##  ## 
#  ##  ##  ##  ##  ##   ### ##    ##  ##       ##  ##  ## 
#   ###  ###  #### ##    ##  ######   ##        ###  ###  
#
############################################################

cp arch/arm/boot/zImage "${OUT_DIR}/wing-usb-console-zImage"
cp arch/arm/boot/dts/nxp/imx/imx6dl-wing-usb-console.dtb "${OUT_DIR}/imx6dl-wing-usb-console.dtb"
cp "${BUILD_DIR}/initramfs.cpio.gz" "${OUT_DIR}/wing-usb-console-initramfs.cpio.gz"

echo "[build] wingfw package"
version=linux-usb-console
if [[ "${WING_PROFILE:-legacy}" != legacy ]]; then version="linux-${WING_PROFILE}"; fi
cdate=()
if [[ "${WING_PROFILE:-legacy}" == t55-spibone ]]; then
    cdate=(--cdate "$(date -u -d "@${SOURCE_DATE_EPOCH}" +%Y-%m-%d_%H:%M:%S)")
fi
python3 "${ROOT_DIR}/tools/build_linux_wingfw.py" \
    --uboot-imx "${UBOOT_IMX}" \
    --kernel "${OUT_DIR}/wing-usb-console-zImage" \
    --dtb "${OUT_DIR}/imx6dl-wing-usb-console.dtb" \
    --initramfs "${OUT_DIR}/wing-usb-console-initramfs.cpio.gz" \
    --fit-output "${OUT_DIR}/wing-usb-console.itb" \
    --version "${version}" \
    "${cdate[@]}" \
    -o "${OUTPUT_WINGFW}"

echo "[done] ${OUTPUT_WINGFW}"
