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
OMC_DIR="${ROOT_DIR}/software/omc"
OMC_BIN="${OMC_DIR}/build/wing/wing-omc"
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
            printf '%s\n' "Makefile_wing"
            printf '%s\n' "compile-wing-docker.sh"
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

    current_signature="$(omc_input_signature)"
    previous_signature=""
    if [[ -f "${OMC_STAMP}" ]]; then
        previous_signature="$(<"${OMC_STAMP}")"
    fi

    if [[ -f "${OMC_BIN}" && "${current_signature}" == "${previous_signature}" ]]; then
        echo "[build] omc (wing target): unchanged, skipping compilation"
        return
    fi

    echo "[build] omc (wing target)"
    "${OMC_DIR}/compile-wing-docker.sh TARGET_WING"

    if [[ ! -f "${OMC_BIN}" ]]; then
        echo "[error] omc build did not produce ${OMC_BIN}" >&2
        exit 1
    fi
    mkdir -p "$(dirname "${OMC_STAMP}")"
    printf '%s\n' "${current_signature}" > "${OMC_STAMP}"
}

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

build_omc_if_needed

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
  ./compile_step2.sh

