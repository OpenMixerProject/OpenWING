# Makefile for pldc_button_mapper
#
# Cross-compile for Wing (ARMv7l hard-float Linux):
#   make
#
# Requires one of these cross-compilers (macOS via Homebrew):
#   brew tap messense/macos-cross-toolchains
#   brew install arm-unknown-linux-gnueabihf
# or:
#   brew install arm-linux-gnueabihf-binutils  # (binutils only, may need separate GCC)
#
# To build a native x86_64 binary for local testing:
#   make native

TARGET  := pldc_button_mapper
SRC     := pldc_button_mapper.c
CFLAGS  := -O2 -Wall -Wextra -std=c11
LDFLAGS := -static

# ── ARM cross-compiler detection ─────────────────────────────────────────────
ifneq ($(shell which arm-linux-gnueabihf-gcc 2>/dev/null),)
    CC_ARM := arm-linux-gnueabihf-gcc
else ifneq ($(shell which arm-unknown-linux-gnueabihf-gcc 2>/dev/null),)
    CC_ARM := arm-unknown-linux-gnueabihf-gcc
else
    CC_ARM :=
endif

# ── default: cross-compile for ARM ───────────────────────────────────────────
.PHONY: all arm native clean deploy

all: arm

arm: $(TARGET)

$(TARGET): $(SRC)
ifeq ($(CC_ARM),)
	$(error No ARM cross-compiler found. Install with: brew tap messense/macos-cross-toolchains && brew install arm-unknown-linux-gnueabihf)
endif
	$(CC_ARM) $(CFLAGS) $(LDFLAGS) -o $@ $<
	@echo "Built ARM binary: $@"
	@command -v file >/dev/null 2>&1 && file $@ || true

# ── native build for local smoke-testing ────────────────────────────────────
native: $(TARGET)-native

$(TARGET)-native: $(SRC)
	$(CC) $(CFLAGS) -o $@ $<
	@echo "Built native binary: $@"

# ── deploy to Wing ───────────────────────────────────────────────────────────
WING_HOST ?= root@10.0.0.93
WING_PATH ?= /tmp/pldc_button_mapper

deploy: $(TARGET)
	ssh $(WING_HOST) "cat > $(WING_PATH) && chmod +x $(WING_PATH) && echo 'Deployed OK'" < $(TARGET)
	@echo "Run with:  ssh $(WING_HOST) $(WING_PATH)"

clean:
	rm -f $(TARGET) $(TARGET)-native
