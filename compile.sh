#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WING_PROFILE="${WING_PROFILE:-legacy}"
case "${WING_PROFILE}" in
    legacy)
        BUILD_PATH=build/linux-usb-console
        OUT_PATH=build/output
        WINGFW_NAME=wing-compact-usb-console-linux.wingfw
        ;;
    t55-spibone)
        BUILD_PATH=build/linux-t55-spibone
        OUT_PATH=build/output-t55-spibone
        WINGFW_NAME=wing-compact-t55-spibone.wingfw
        umask 022
        ;;
    *) echo "[error] unknown WING_PROFILE: ${WING_PROFILE}" >&2; exit 1 ;;
esac
BUILD_DIR="${ROOT_DIR}/${BUILD_PATH}"
OUT_DIR="${ROOT_DIR}/${OUT_PATH}"
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
BUSYBOX_VER="1.38.0"
DROPBEAR_VER="2026.91"
DOOMGENERIC_REF="master"
HTOP_VER="3.3.0"
INCLUDE_DOOM="${INCLUDE_DOOM:-}"

OMC_DIR="${OMC_DIR:-${ROOT_DIR}/software/omc}"
if [[ ! -f "${OMC_DIR}/Makefile" && "${OMC_DIR}" == "${ROOT_DIR}/software/omc" ]]; then
    OMC_DIR="${ROOT_DIR}/../OpenMixerControl"
fi
OMC_BIN="${OMC_DIR}/build/wing/omc"
OMC_STAMP="${OMC_DIR}/build/wing/.wing-omc.inputs.sha256"

hash_file() {
    local digest
    if command -v sha256sum >/dev/null 2>&1; then
        digest="$(sha256sum "$1")"
    else
        digest="$(shasum -a 256 "$1")"
    fi
    printf '%s\n' "${digest%% *}"
}

hash_stream() {
    local digest
    if command -v sha256sum >/dev/null 2>&1; then
        digest="$(sha256sum)"
    else
        digest="$(shasum -a 256)"
    fi
    printf '%s\n' "${digest%% *}"
}

omc_input_signature() {
    (
        cd "${OMC_DIR}"
        {
            printf '%s\n' "Makefile"
            printf '%s\n' "compile-docker.sh"
            find src lib files -type f -print
        } | LC_ALL=C sort | while IFS= read -r input_path; do
            if [[ -f "${input_path}" ]]; then
                printf '%s\n' "${input_path}"
                hash_file "${input_path}"
            fi
        done
    ) | hash_stream
}

build_omc_if_needed() {
    local current_signature previous_signature

    if [[ ! -f "${OMC_DIR}/Makefile" || ! -x "${OMC_DIR}/compile-docker.sh" ]]; then
        echo "[error] OMC checkout missing: initialize software/omc or set OMC_DIR" >&2
        exit 1
    fi
    current_signature="$(omc_input_signature)"
    previous_signature=""
    if [[ -f "${OMC_STAMP}" ]]; then
        previous_signature="$(<"${OMC_STAMP}")"
    fi

    if [[ -s "${OMC_BIN}" && "${current_signature}" == "${previous_signature}" ]]; then
        echo "[build] omc (wing target): unchanged, skipping compilation"
        return
    fi

    echo "[build] omc (wing target)"
    "${OMC_DIR}/compile-docker.sh" TARGET_WING

    if [[ ! -s "${OMC_BIN}" ]]; then
        echo "[error] omc build did not produce ${OMC_BIN}" >&2
        exit 1
    fi
    mkdir -p "$(dirname "${OMC_STAMP}")"
    printf '%s\n' "${current_signature}" > "${OMC_STAMP}"
}

mkdir -p "${BUILD_DIR}" "${OUT_DIR}" "${DOCKER_CONFIG_DIR}"
PAYLOAD_DIR="$(mktemp -d "${BUILD_DIR}/payload.XXXXXX")"
trap 'rm -rf -- "${PAYLOAD_DIR}"' EXIT

if [[ "${WING_PROFILE}" == t55-spibone ]]; then
    : "${WING_T55_OMC_DIR:?set WING_T55_OMC_DIR to the reviewed T55 OMC artifact directory}"
    : "${WING_T55_FPGA_DIR:?set WING_T55_FPGA_DIR to the built T55 SPIBone FPGA/CSR bundle}"
    : "${WING_T55_DSP_DIR:?set WING_T55_DSP_DIR to the built ADSP-21489 WDSP-v1 bundle}"
    : "${OPENAUDIOFPGA_DIR:?set OPENAUDIOFPGA_DIR to the WING OpenAudioFPGA source checkout}"
    if [[ "${1:-}" == *_menuconfig || -n "${INCLUDE_DOOM}" ]]; then
        echo '[error] menuconfig and INCLUDE_DOOM are not supported by t55-spibone' >&2
        exit 1
    fi
    python3 "${ROOT_DIR}/tools/stage_wing_t55.py" \
        "${WING_T55_FPGA_DIR}" "${WING_T55_DSP_DIR}" "${WING_T55_OMC_DIR}" \
        "${PAYLOAD_DIR}/wing-t55" --spibone "${OPENAUDIOFPGA_DIR}"
    python3 "${ROOT_DIR}/tools/wing_spibone_header.py" "${PAYLOAD_DIR}/wing-t55" > "${PAYLOAD_DIR}/wing-t55-bundle.h"
fi

if [[ ! -f "${ROOT_DIR}/lib/private/tools/build_linux_wingfw.py" ]]; then
    echo "[error] missing firmware packer; run git submodule update --init lib/private" >&2
    exit 1
fi

BUILDER_BASE=debian:trixie
if [[ "${WING_PROFILE}" == t55-spibone ]]; then
    DOCKER_IMAGE=wing-t55-spibone-builder:trixie
    BUILDER_BASE=debian@sha256:9c6e381421d0f48d1b8133e573f33c185ee16cbf505eda9c4a8a4791b25ea8a6
fi
DOCKER_CONFIG="${DOCKER_CONFIG_DIR}" DOCKER_HOST="${DOCKER_HOST_URI}" DOCKER_BUILDKIT=0 \
docker build --build-arg BUILDER_BASE="${BUILDER_BASE}" -t "${DOCKER_IMAGE}" - <<'DOCKERFILE'
ARG BUILDER_BASE
FROM ${BUILDER_BASE}
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
    libncurses-dev \
    ncurses-dev:armhf \
    ncurses-dev \
    libssl-dev \
    make \
    patch \
    pkg-config \
    python3 \
    u-boot-tools \
    xz-utils \
  && rm -rf /var/lib/apt/lists/*
DOCKERFILE

if [[ "${WING_PROFILE}" == legacy ]]; then
    if [[ -n "${WING_LEGACY_OMC_BIN:-}" ]]; then
        if [[ ! -s "${WING_LEGACY_OMC_BIN}" ]]; then
            echo "[error] missing or empty WING_LEGACY_OMC_BIN: ${WING_LEGACY_OMC_BIN}" >&2
            exit 1
        fi
        cp "${WING_LEGACY_OMC_BIN}" "${PAYLOAD_DIR}/omc"
    else
        build_omc_if_needed
        cp "${OMC_BIN}" "${PAYLOAD_DIR}/omc"
    fi
fi

docker_tty=()
if [[ -t 0 && -t 1 ]]; then docker_tty=(-t); fi
build_volume=()
CONTAINER_BUILD_DIR="/openwing/${BUILD_PATH}"
if [[ "${WING_PROFILE}" == t55-spibone && -n "${WING_BUILD_VOLUME:-}" ]]; then
    # The volume root may hold another profile's kernel tree. Never build there.
    CONTAINER_BUILD_DIR=/wing-build/t55-spibone
    build_volume=(-v "${WING_BUILD_VOLUME}:/wing-build")
elif [[ -n "${WING_BUILD_VOLUME:-}" ]]; then
    CONTAINER_BUILD_DIR=/wing-build
    build_volume=(-v "${WING_BUILD_VOLUME}:${CONTAINER_BUILD_DIR}")
fi

DOCKER_CONFIG="${DOCKER_CONFIG_DIR}" DOCKER_HOST="${DOCKER_HOST_URI}" \
docker run --rm \
  -i ${docker_tty[@]+"${docker_tty[@]}"} \
  -u "$(id -u):$(id -g)" \
  -v "${ROOT_DIR}:/openwing" \
  ${build_volume[@]+"${build_volume[@]}"} \
  -w "/openwing" \
  -e ROOT_DIR="/openwing" \
  -e BUILD_DIR="${CONTAINER_BUILD_DIR}" \
  -e OUT_DIR="/openwing/${OUT_PATH}" \
  -e WING_PROFILE="${WING_PROFILE}" \
  -e PAYLOAD_DIR="/openwing/${PAYLOAD_DIR#"${ROOT_DIR}/"}" \
  -e LINUX_VER="${LINUX_VER}" \
  -e BUSYBOX_VER="${BUSYBOX_VER}" \
  -e DROPBEAR_VER="${DROPBEAR_VER}" \
  -e DOOMGENERIC_REF="${DOOMGENERIC_REF}" \
  -e INCLUDE_DOOM="${INCLUDE_DOOM}" \
  -e HTOP_VER="${HTOP_VER}" \
  -e UBOOT_IMX="/openwing/${OUT_PATH}/u-boot-linux.imx" \
  -e OUTPUT_WINGFW="/openwing/${OUT_PATH}/${WINGFW_NAME}" \
  "${DOCKER_IMAGE}" \
  ./compile_step2.sh "$@"
