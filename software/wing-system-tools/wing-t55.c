/* Manual WING Compact T55 bring-up; never used from init. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/if_alg.h>
#include <linux/gpio.h>
#include <linux/magic.h>
#include <linux/spi/spidev.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/random.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <time.h>
#include <unistd.h>

#define BUNDLE "/usr/share/wing-t55/"
#define MANIFEST BUNDLE "manifest.json"
#define RECEIPT "omc-wing-fpga.json"
#define SS0 "/dev/spidev1.0"
#define SPI_HZ 2000000u
#define RESET_MASK 0x0bu /* DSP1, 2, 4; never DSP3/A9. */
#include "spibone_uapi.h"
#define SS1 "/dev/spibone0"
_Static_assert(SPIBONE_IOC_WING_ENABLE == _IOW(0xB5, 8, uint32_t), "incompatible WING SPIBone enable ioctl");

struct input { const char *name; size_t size; const char *hash; };
#include "wing-t55-bundle.h"

static int changing_ctrl = -1;
static void require(bool ok, const char *what) {
    if (!ok) {
        uint32_t state = SPIBONE_STATE_OFFLINE;
        if (changing_ctrl >= 0 && ioctl(changing_ctrl, SPIBONE_IOC_SET_STATE, &state))
            fputs("wing-t55: could not restore offline state\n", stderr);
        fprintf(stderr, "wing-t55: %s\n", what);
        exit(EXIT_FAILURE);
    }
}

static void pause_ms(unsigned ms) {
    struct timespec remaining = {.tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L};
    while (nanosleep(&remaining, &remaining) < 0)
        require(errno == EINTR, "nanosleep failed");
}

static bool trusted_file(int fd, mode_t mode, off_t size) {
    struct stat st;
    return fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == 0 &&
           (st.st_mode & 07777) == mode && st.st_size == size;
}

static void check_hash(const struct input *file, const uint8_t *bytes) {
    const struct sockaddr_alg algorithm = {.salg_family = AF_ALG, .salg_type = "hash", .salg_name = "sha256"};
    int hash = socket(AF_ALG, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    require(hash >= 0 && bind(hash, (const struct sockaddr *)&algorithm, sizeof algorithm) == 0,
            "kernel SHA-256 unavailable");
    int job = accept4(hash, NULL, NULL, SOCK_CLOEXEC);
    require(job >= 0, "could not start SHA-256");
    for (size_t offset = 0; offset < file->size;) {
        size_t len = file->size - offset;
        if (len > 4096) len = 4096;
        require(send(job, bytes + offset, len, offset + len < file->size ? MSG_MORE : 0) == (ssize_t)len,
                "could not hash complete input");
        offset += len;
    }
    uint8_t digest[32];
    require(read(job, digest, sizeof digest) == sizeof digest, "SHA-256 result unavailable");
    close(job);
    close(hash);
    char hex[65];
    for (size_t i = 0; i < sizeof digest; ++i) snprintf(hex + 2*i, 3, "%02x", digest[i]);
    if (strcmp(hex, file->hash)) {
        fprintf(stderr, "wing-t55: SHA-256 mismatch: %s\n", file->name);
        exit(EXIT_FAILURE);
    }
}

/* Hash the same private buffer that will be transmitted, not a second open. */
static uint8_t *read_input(size_t index, size_t prefix) {
    const struct input *file = &inputs[index];
    char path[128];
    snprintf(path, sizeof path, BUNDLE "%s", file->name);
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    require(fd >= 0 && trusted_file(fd, 0444, file->size), path);
    uint8_t *bytes = malloc(prefix + file->size);
    require(bytes != NULL, "could not allocate input buffer");
    for (size_t offset = 0; offset < file->size;) {
        ssize_t count = read(fd, bytes + prefix + offset, file->size - offset);
        require(count > 0, "incomplete input read");
        offset += (size_t)count;
    }
    close(fd);
    check_hash(file, bytes + prefix);
    return bytes;
}

static int run_lock(void) {
    int run = open("/run", O_DIRECTORY | O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    struct stat st;
    struct statfs fs;
    require(run >= 0 && !fstat(run, &st) && st.st_uid == 0 && !(st.st_mode & 0022) &&
            !fstatfs(run, &fs) && fs.f_type == TMPFS_MAGIC, "need root-owned boot-local /run tmpfs");
    /* Keep the exclusive lock until process exit; C1 holds a shared lock. */
    int lock = openat(run, "wing-t55.lock", O_RDWR | O_CREAT | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0600);
    require(lock >= 0 && trusted_file(lock, 0600, 0) && !flock(lock, LOCK_EX | LOCK_NB),
            "bring-up lock unavailable (another operation may be running)");
    return run;
}

static void clear_receipt(int run) {
    require(unlinkat(run, RECEIPT, 0) == 0 || errno == ENOENT, "cannot invalidate FPGA receipt");
}

static int format_receipt(char *out, size_t capacity, unsigned mask) {
    int size = snprintf(out, capacity,
        "{\"manifest\":\"" MANIFEST "\",\"manifest_sha256\":\"%s\",\"build_id\":%u,\"chip_select\":1,"
        "\"bitstream_sha256\":\"%s\",\"spi_device\":\"" SS1 "\",\"dsp_runtime_mask\":%u}\n",
        inputs[1].hash, FABRIC_BUILD_ID, inputs[0].hash, mask);
    require(size > 0 && (size_t)size < capacity, "receipt too long");
    return size;
}

static unsigned take_receipt(int run) {
    char actual[384], expected[sizeof actual];
    int fd = openat(run, RECEIPT, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    clear_receipt(run); /* Absent before any mutating operation. */
    ssize_t size = fd < 0 ? -1 : read(fd, actual, sizeof actual);
    if (size > 0 && trusted_file(fd, 0600, size)) {
        for (unsigned mask = 0; mask <= RESET_MASK; ++mask) {
            if (!(mask & ~RESET_MASK) && size == format_receipt(expected, sizeof expected, mask) &&
                !memcmp(actual, expected, size)) {
                close(fd);
                return mask;
            }
        }
    }
    require(false, "no matching boot-local load receipt; do not probe");
    return 0;
}

static void publish_receipt(int run, unsigned mask) {
    char expected[384];
    int size = format_receipt(expected, sizeof expected, mask);
    char temp[] = "/run/.wing-t55.XXXXXX";
    int fd = mkstemp(temp); /* umask and mkstemp keep the staging inode root-only. */
    require(fd >= 0, "cannot create temporary receipt");
    bool complete = write(fd, expected, size) == size;
    complete = close(fd) == 0 && complete;
    if (!complete || renameat(AT_FDCWD, temp, run, RECEIPT) < 0) {
        unlink(temp);
        require(false, "cannot publish FPGA receipt");
    }
}

static int gpio_line(const char *name, unsigned offset, bool output) {
    glob_t chips;
    require(glob("/dev/gpiochip*", 0, NULL, &chips) == 0, "GPIO chips unavailable");
    int chip = -1;
    for (size_t i = 0; i < chips.gl_pathc; ++i) {
        int fd = open(chips.gl_pathv[i], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        struct gpio_v2_line_info info = {.offset = offset};
        if (fd >= 0 && !ioctl(fd, GPIO_V2_GET_LINEINFO_IOCTL, &info) && !strcmp(info.name, name)) {
            require(chip < 0, "ambiguous T55 GPIO line");
            chip = fd;
        } else if (fd >= 0) close(fd);
    }
    globfree(&chips);
    require(chip >= 0, name);
    struct gpio_v2_line_request line = {
        .offsets = {offset}, .num_lines = 1, .consumer = "wing-t55",
        .config.flags = output ? GPIO_V2_LINE_FLAG_OUTPUT : GPIO_V2_LINE_FLAG_INPUT,
    };
    /* Output starts at physical low: assert CRESET_N without a high pulse. */
    require(!ioctl(chip, GPIO_V2_GET_LINE_IOCTL, &line), name);
    close(chip);
    return line.fd;
}

static bool cdone(int fd) {
    struct gpio_v2_line_values values = {.mask = 1};
    require(!ioctl(fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &values), "CDONE read failed");
    return (values.bits & 1) != 0;
}

static void spi_mode(int fd, uint32_t mode) {
    uint8_t bits = 8;
    uint32_t speed = SPI_HZ;
    require(!ioctl(fd, SPI_IOC_WR_MODE32, &mode) && !ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) &&
            !ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed), "could not configure SPI mode/width/speed");
}

static int open_spi(const char *path) {
    int fd = open(path, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    require(fd >= 0, path);
    spi_mode(fd, SPI_MODE_0);
    return fd;
}

static bool transfer(int fd, const uint8_t *tx, uint8_t *rx, size_t size, uint32_t speed) {
    struct spi_ioc_transfer xfer = {
        .tx_buf = (uintptr_t)tx, .rx_buf = (uintptr_t)rx, .len = size,
        .speed_hz = speed, .bits_per_word = 8,
    };
    return ioctl(fd, SPI_IOC_MESSAGE(1), &xfer) == (int)size;
}

static uint32_t spibone(int fd, bool write, uint32_t addr, uint32_t data) {
    struct spibone_wb_xfer reg = {.addr = addr, .val = data};
    require(!ioctl(fd, write ? SPIBONE_IOC_POKE : SPIBONE_IOC_PEEK, &reg), "SPIBone register access failed");
    return reg.val;
}

static void verify_identity(int ctrl, int done) {
    require(cdone(done), "CDONE low; do not probe");
    uint32_t state;
    require(!ioctl(ctrl, SPIBONE_IOC_GET_STATE, &state) && state == SPIBONE_STATE_ONLINE,
            "SPIBone not online; do not probe");
    for (size_t i = 0; i < sizeof identity / sizeof identity[0]; ++i) {
        require(spibone(ctrl, false, identity[i].addr, 0) == identity[i].val, "FPGA identity mismatch");
    }
    require((spibone(ctrl, false, FABRIC_STATUS, 0) & FABRIC_READY) && cdone(done), "FPGA fabric not ready");
}

static uint32_t check_safe(int ctrl) {
    require(spibone(ctrl, false, AUDIO_ENABLE, 0) == 0 && spibone(ctrl, false, AUDIO_VALID, 0) == 0,
            "FS and VALID must both be off");
    uint32_t reset = spibone(ctrl, false, RESET_RELEASE, 0);
    require(!(reset & ~RESET_MASK), "unexpected DSP reset register");
    return reset;
}

static void load_fpga(int ctrl, int done, const uint8_t *bytes) {
    int config = open_spi(SS0);
    /* The SPI core owns both GPIO CS lines; NO_CS clocks the passive loader with neither selected. */
    spi_mode(config, SPI_MODE_0 | SPI_NO_CS);
    int reset = gpio_line("T55_CRESET_N", 17, true);
    pause_ms(2);
    require(!cdone(done), "CDONE did not go low with CRESET_N asserted");
    struct gpio_v2_line_values release = {.mask = 1, .bits = 1};
    require(!ioctl(reset, GPIO_V2_LINE_SET_VALUES_IOCTL, &release), "could not release CRESET_N");
    pause_ms(5);
    require(!cdone(done), "CDONE high before transfer; no trusted load");
    for (size_t offset = 0; offset < inputs[0].size;) {
        size_t len = inputs[0].size - offset;
        if (len > 1024) len = 1024;
        require(transfer(config, bytes + offset, NULL, len, SPI_HZ), "short or failed passive SPI upload");
        offset += len;
    }
    for (unsigned i = 0; i < 100 && !cdone(done); ++i) pause_ms(1);
    require(cdone(done), "CDONE did not assert after full passive SPI upload");
    spi_mode(config, SPI_MODE_0);
    close(config);
    close(reset);
    uint32_t build_id = FABRIC_BUILD_ID;
    require(cdone(done) && !ioctl(ctrl, SPIBONE_IOC_WING_ENABLE, &build_id),
            "CDONE/driver gate refused this FPGA image");
    verify_identity(ctrl, done);
    require(check_safe(ctrl) == 0, "fresh FPGA must hold all controllable DSPs in reset");
}

static uint32_t crc32(const uint8_t *bytes, size_t length) {
    uint32_t crc = 0xffffffffu;
    while (length--) {
        crc ^= *bytes++;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

static uint32_t get_be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void put_be(uint8_t *p, uint32_t value) {
    for (int i = 3; i >= 0; --i, value >>= 8) p[i] = value;
}

static bool qualify_dsp(int dsp, unsigned core) {
    /* Same WDSP v1 IDENTIFY frame and reply window as OMC. No runtime writes. */
    uint8_t tx[73] = {core}, rx[sizeof tx] = {0};
    uint8_t *frame = tx + 1;
    uint32_t nonce;
    require(getrandom(&nonce, sizeof nonce, GRND_NONBLOCK) == sizeof nonce, "WDSP nonce unavailable");
    put_be(frame, 0x57445350u);
    put_be(frame + 4, nonce);
    frame[11] = core;
    uint32_t check = crc32(frame, 36);
    put_be(frame + 36, check);
    require(transfer(dsp, tx, rx, sizeof tx, 10000), "short or failed WDSP identity transfer");
    for (size_t i = 41; i + 24 <= sizeof rx; ++i) {
        const uint8_t *reply = rx + i;
        if (get_be(reply) == 0x57445352u && get_be(reply + 4) == 1 &&
            get_be(reply + 8) == nonce && reply[12] == core && !reply[13] && reply[14] == 1 && !reply[15] &&
            get_be(reply + 16) == check && get_be(reply + 20) == crc32(reply, 20)) return true;
    }
    return false;
}

static unsigned boot_dsp(int ctrl, int done, unsigned core, unsigned mask) {
    const size_t file = core + 1;
    uint8_t *frame = read_input(file, 1);
    frame[0] = core;
    FILE *limit = fopen("/sys/module/spidev/parameters/bufsiz", "r");
    unsigned size = 0;
    require(limit && fscanf(limit, "%u", &size) == 1 && size >= 65536 && inputs[file].size < 65536,
            "spidev buffer too small for one continuous SS0 message");
    fclose(limit);
    int dsp = open_spi(SS0);
    uint32_t reset = check_safe(ctrl), bit = 1u << (core - 1);
    require(reset == mask, "live DSP reset state does not match the qualification mask");
    require(!(reset & bit), "selected DSP is no longer in reset; do not retry without a fresh load");
    require(cdone(done), "CDONE lost before DSP reset release");
    spibone(ctrl, true, RESET_RELEASE, reset | bit);
    require(spibone(ctrl, false, RESET_RELEASE, 0) == (reset | bit), "DSP reset release readback failed");
    pause_ms(10); /* Provisional 21489 reset-to-first-clock interval; qualify at the bench. */
    require(cdone(done), "CDONE lost before DSP transfer");
    /* One SPI message, one transfer, no cs_change: SS0 encloses target and all LDR bytes. */
    require(transfer(dsp, frame, NULL, 1 + inputs[file].size, SPI_HZ), "short or failed DSP transfer; do not retry");
    require(cdone(done), "CDONE lost during DSP transfer");
    pause_ms(10); /* Allow SPORT setup before the single WDSP identity request. */
    require(cdone(done) && qualify_dsp(dsp, core), "no matching armed WDSP v1 identity; reload before retrying");
    verify_identity(ctrl, done);
    require(check_safe(ctrl) == (mask | bit), "unsafe state after WDSP identity");
    close(dsp);
    free(frame);
    return mask | bit;
}

int main(int argc, char **argv) {
    bool load = argc >= 2 && !strcmp(argv[1], "load");
    if (load)
        require(argc == 4 && !strcmp(argv[2], "--bench-muted") && !strcmp(argv[3], "--clocks-verified"),
                "load requires a muted bench and external CLK0/R2 and CLK5/M3 verified against the final FPGA clock design"
                "; acknowledge with --bench-muted --clocks-verified");
    unsigned core = 0;
    if (argc == 4 && !strcmp(argv[1], "boot-dsp") && !strcmp(argv[3], "--bench-muted") &&
        strlen(argv[2]) == 1 && argv[2][0] >= '1' && argv[2][0] <= '4') core = argv[2][0] - '0';
    require(core != 3, "DSP3 blocked: no qualified reset, A9 must remain input");
    require(load || core, "usage: wing-t55 load --bench-muted --clocks-verified"
            " | boot-dsp {1|2|4} --bench-muted");
    require(geteuid() == 0 && access("/etc/wing-t55-bringup", F_OK) == 0, "requires manual T55 bring-up image and root");
    umask(077);
    int run = run_lock();
    unsigned mask = 0;
    if (load) clear_receipt(run);
    else mask = take_receipt(run);
    free(read_input(1, 0));
    uint8_t *image = load ? read_input(0, 0) : NULL;
    int done = gpio_line("T55_CDONE", 26, false);
    int ctrl = open(SS1, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    require(ctrl >= 0, SS1);
    changing_ctrl = ctrl;
    if (load) {
        uint32_t state = SPIBONE_STATE_OFFLINE;
        require(!ioctl(ctrl, SPIBONE_IOC_SET_STATE, &state), "could not take SPIBone offline before reset");
        load_fpga(ctrl, done, image);
        free(image);
    } else {
        verify_identity(ctrl, done);
        mask = boot_dsp(ctrl, done, core, mask);
    }
    publish_receipt(run, mask);
    changing_ctrl = -1;
    close(ctrl);
    close(done);
    if (load) puts("T55 volatile load + identity verified; fabric receipt published (not audio readiness).");
    if (core) printf("DSP%u WDSP identity qualified; FS/VALID remain off.\n", core);
    return EXIT_SUCCESS;
}
