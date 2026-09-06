/*
 * wing_syscfg.c - Behringer Wing System Configuration Store Parser & MAC Configurator
 *
 * Reverse-engineered from stock Behringer Wing firmware (app_0x10004000.bin).
 * Parses the KEY=VALUE ASCII configuration stream from non-volatile storage:
 *   - Primary: SPI NOR Flash (Winbond W25Q64 / GD25Q64) at offsets:
 *              0x7FE000 (Factory Config: :NGC3279~HARDWARE=...~MAC=...~MODEL=...~SN=...)
 *              0x7FF000 (User/Runtime Config: consolename=...,ipmode=...,eth_cfg=...)
 *   - Fallback: MTD devices (/dev/mtd*, /dev/mtdblock*) and eMMC partitions.
 *
 * Features:
 *   - Directly reads factory configuration from SPI NOR Flash via hardware registers.
 *   - Applies factory MAC addresses to network interfaces (eth0, lan1, lan2).
 *   - Allows reading individual keys (e.g. --get SN, --get MAC, --get MODEL, --get DEVTYPE).
 *   - Dumps full system configuration in human-readable table or JSON format.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <dirent.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <arpa/inet.h>

#define MAX_ENTRIES 256
#define MAX_KEY_LEN 64
#define MAX_VAL_LEN 1024
#define SCAN_CHUNK_SIZE (256 * 1024)
#define MAX_SCAN_SIZE (64 * 1024 * 1024) /* Scan up to 64MB */

/* i.MX6 Register Bases for Direct SPI Hardware Access */
#define IOMUXC_BASE     0x020E0000
#define GPIO2_BASE      0x020A0000
#define GPIO3_BASE      0x020A4000

#define GPIO_DR         0x00
#define GPIO_GDIR       0x04
#define GPIO_PSR        0x08

#define FLASH_FACTORY_CFG_ADDR  0x7FE000
#define FLASH_USER_CFG_ADDR     0x7FF000
#define FLASH_CFG_BLOCK_SIZE    2048

typedef struct {
    char key[MAX_KEY_LEN];
    char val[MAX_VAL_LEN];
} kv_entry_t;

typedef struct {
    kv_entry_t entries[MAX_ENTRIES];
    size_t count;
    char source_path[256];
    unsigned char mac[6];
    bool has_mac;
} wing_config_store_t;

static const char *default_scan_paths[] = {
    "/dev/mtdblock0",
    "/dev/mtd0",
    "/dev/mtdblock1",
    "/dev/mtd1",
    "/dev/mmcblk2p1",
    "/dev/mmcblk2",
    "/dev/mmcblk0p1",
    "/dev/mmcblk0",
    "/dev/mmcblk2boot0",
    "/dev/mmcblk2boot1",
    "/etc/wing-config.txt",
    "/tmp/wing-config.txt",
    NULL
};

/* Safely copy strings with null-termination */
static void safe_strcpy(char *dst, const char *src, size_t dst_size) {
    if (!dst || dst_size == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t len = strlen(src);
    if (len >= dst_size) len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* Parse hex bytes (e.g. "00:15:64:0D:62:80" or "0015640D6280") */
static bool parse_mac_string(const char *str, unsigned char out_mac[6]) {
    unsigned int bytes[6];
    int count = 0;

    if (sscanf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
               &bytes[0], &bytes[1], &bytes[2], &bytes[3], &bytes[4], &bytes[5]) == 6 ||
        sscanf(str, "%02x-%02x-%02x-%02x-%02x-%02x",
               &bytes[0], &bytes[1], &bytes[2], &bytes[3], &bytes[4], &bytes[5]) == 6) {
        count = 6;
    } else {
        /* Raw 12-char hex without separators */
        char clean[13] = {0};
        size_t cidx = 0;
        for (size_t i = 0; str[i] && cidx < 12; i++) {
            if (isxdigit((unsigned char)str[i])) {
                clean[cidx++] = str[i];
            }
        }
        if (cidx == 12) {
            if (sscanf(clean, "%02x%02x%02x%02x%02x%02x",
                       &bytes[0], &bytes[1], &bytes[2], &bytes[3], &bytes[4], &bytes[5]) == 6) {
                count = 6;
            }
        }
    }

    if (count == 6) {
        for (int i = 0; i < 6; i++) {
            out_mac[i] = (unsigned char)bytes[i];
        }
        /* Check if invalid (all 00 or all FF) */
        if ((out_mac[0] == 0xff && out_mac[1] == 0xff && out_mac[2] == 0xff &&
             out_mac[3] == 0xff && out_mac[4] == 0xff && out_mac[5] == 0xff) ||
            (out_mac[0] == 0x00 && out_mac[1] == 0x00 && out_mac[2] == 0x00 &&
             out_mac[3] == 0x00 && out_mac[4] == 0x00 && out_mac[5] == 0x00)) {
            return false;
        }
        return true;
    }
    return false;
}

/* Format MAC address as standard colon-separated string */
static void format_mac(const unsigned char mac[6], char *out, size_t out_len) {
    snprintf(out, out_len, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* Adds or updates key-value pair in store */
static void store_add_kv(wing_config_store_t *store, const char *key, const char *val) {
    if (!key || !val || strlen(key) == 0) return;

    for (size_t i = 0; i < store->count; i++) {
        if (strcasecmp(store->entries[i].key, key) == 0) {
            safe_strcpy(store->entries[i].val, val, sizeof(store->entries[i].val));
            return;
        }
    }

    if (store->count < MAX_ENTRIES) {
        safe_strcpy(store->entries[store->count].key, key, sizeof(store->entries[0].key));
        safe_strcpy(store->entries[store->count].val, val, sizeof(store->entries[0].val));
        store->count++;
    }

    if (strcasecmp(key, "MAC") == 0) {
        if (parse_mac_string(val, store->mac)) {
            store->has_mac = true;
        }
    }
}

/* Parse KEY=VALUE / :SIGNATURE tokens separated by '~', ',', '\n', '\r' */
static size_t parse_config_stream(const char *buf, size_t len, wing_config_store_t *store) {
    size_t initial_count = store->count;
    const char *ptr = buf;
    const char *end = buf + len;

    while (ptr < end) {
        /* Skip leading whitespace, null bytes, 0xFF padding, and delimiters */
        while (ptr < end && (*ptr == '~' || *ptr == ',' || *ptr == '\r' || *ptr == '\n' ||
                             (unsigned char)*ptr <= 0x20 || (unsigned char)*ptr == 0xFF || *ptr == '\0')) {
            ptr++;
        }
        if (ptr >= end) break;

        /* Find token boundary */
        const char *tok_end = ptr;
        while (tok_end < end && *tok_end != '~' && *tok_end != ',' &&
               *tok_end != '\r' && *tok_end != '\n' && *tok_end != '\0' && (unsigned char)*tok_end != 0xFF) {
            tok_end++;
        }

        size_t tok_len = tok_end - ptr;
        if (tok_len > 0) {
            /* Case 1: Leading signature token like ":NGC3279" */
            if (*ptr == ':' && tok_len > 1) {
                char sig[MAX_VAL_LEN] = {0};
                size_t cpy = (tok_len - 1 < sizeof(sig) - 1) ? tok_len - 1 : sizeof(sig) - 1;
                memcpy(sig, ptr + 1, cpy);
                store_add_kv(store, "DEVTYPE", sig);
            }
            /* Case 2: KEY=VALUE pair */
            else {
                const char *eq = memchr(ptr, '=', tok_len);
                if (eq && eq > ptr && eq < tok_end) {
                    size_t k_len = eq - ptr;
                    size_t v_len = tok_end - (eq + 1);

                    if (k_len < MAX_KEY_LEN && v_len < MAX_VAL_LEN) {
                        char k[MAX_KEY_LEN] = {0};
                        char v[MAX_VAL_LEN] = {0};
                        memcpy(k, ptr, k_len);
                        memcpy(v, eq + 1, v_len);

                        /* Verify key has valid characters */
                        bool valid = true;
                        for (size_t i = 0; i < k_len; i++) {
                            if (!isalnum((unsigned char)k[i]) && k[i] != '_' && k[i] != '-' && k[i] != '.') {
                                valid = false;
                                break;
                            }
                        }
                        if (valid) {
                            store_add_kv(store, k, v);
                        }
                    }
                }
            }
        }

        ptr = tok_end;
    }

    return store->count - initial_count;
}

/* ========================================================================= */
/* Direct Hardware SPI Flash Access (i.MX6 ECSPI1 Pins via Bit-Bang GPIO)    */
/* ========================================================================= */

static inline void writel(uint8_t *base, uint32_t off, uint32_t val) {
    *(volatile uint32_t *)(base + off) = val;
}
static inline uint32_t readl(uint8_t *base, uint32_t off) {
    return *(volatile uint32_t *)(base + off);
}

static inline void bb_set_sclk(uint8_t *gpio3, int v) {
    uint32_t dr = readl(gpio3, GPIO_DR);
    if (v) dr |= (1u << 16); else dr &= ~(1u << 16);
    writel(gpio3, GPIO_DR, dr);
}

static inline void bb_set_mosi(uint8_t *gpio3, int v) {
    uint32_t dr = readl(gpio3, GPIO_DR);
    if (v) dr |= (1u << 18); else dr &= ~(1u << 18);
    writel(gpio3, GPIO_DR, dr);
}

static inline int bb_get_miso(uint8_t *gpio3) {
    return (readl(gpio3, GPIO_PSR) >> 17) & 1;
}

static inline void bb_set_cs(uint8_t *gpio2, int v) {
    uint32_t dr = readl(gpio2, GPIO_DR);
    if (v) dr |= (1u << 30); else dr &= ~(1u << 30);
    writel(gpio2, GPIO_DR, dr);
}

static void bb_delay(void) {
    for (volatile int i = 0; i < 15; i++);
}

static uint8_t bb_spi_xfer_byte(uint8_t *gpio2, uint8_t *gpio3, uint8_t out) {
    (void)gpio2;
    uint8_t in = 0;
    for (int i = 7; i >= 0; i--) {
        bb_set_mosi(gpio3, (out >> i) & 1);
        bb_delay();
        bb_set_sclk(gpio3, 1);
        bb_delay();
        if (bb_get_miso(gpio3)) in |= (1 << i);
        bb_set_sclk(gpio3, 0);
        bb_delay();
    }
    return in;
}

static void bb_spi_read(uint8_t *gpio2, uint8_t *gpio3, uint32_t addr, uint8_t *buf, size_t len) {
    bb_set_cs(gpio2, 0);
    bb_delay();
    bb_spi_xfer_byte(gpio2, gpio3, 0x03);
    bb_spi_xfer_byte(gpio2, gpio3, (addr >> 16) & 0xFF);
    bb_spi_xfer_byte(gpio2, gpio3, (addr >> 8) & 0xFF);
    bb_spi_xfer_byte(gpio2, gpio3, addr & 0xFF);
    for (size_t i = 0; i < len; i++) {
        buf[i] = bb_spi_xfer_byte(gpio2, gpio3, 0x00);
    }
    bb_set_cs(gpio2, 1);
    bb_delay();
}

static bool read_spi_flash_config(wing_config_store_t *store) {
    int mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd < 0) return false;

    uint8_t *iomuxc = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, IOMUXC_BASE);
    uint8_t *gpio2  = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, GPIO2_BASE);
    uint8_t *gpio3  = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, GPIO3_BASE);

    if (iomuxc == MAP_FAILED || gpio2 == MAP_FAILED || gpio3 == MAP_FAILED) {
        if (iomuxc != MAP_FAILED) munmap(iomuxc, 0x1000);
        if (gpio2 != MAP_FAILED) munmap(gpio2, 0x1000);
        if (gpio3 != MAP_FAILED) munmap(gpio3, 0x1000);
        close(mem_fd);
        return false;
    }

    /* Configure Pinmux to GPIO Mode (matching stock spiflash.cpp) */
    writel(iomuxc, 0x0144, 0x05); writel(iomuxc, 0x0514, 0x1b0b0); /* EIM_D16: GPIO3_16 (SCLK) */
    writel(iomuxc, 0x0148, 0x15); writel(iomuxc, 0x0518, 0x1b0b0); /* EIM_D17: GPIO3_17 (MISO, SION=1) */
    writel(iomuxc, 0x014c, 0x05); writel(iomuxc, 0x051c, 0x1b0b0); /* EIM_D18: GPIO3_18 (MOSI) */
    writel(iomuxc, 0x01cc, 0x05); writel(iomuxc, 0x059c, 0x1b0b0); /* EIM_EB2: GPIO2_30 (/CS) */
    writel(iomuxc, 0x0150, 0x05); writel(iomuxc, 0x0520, 0x1b0b0); /* EIM_D19: GPIO3_19 (/WP) */
    writel(iomuxc, 0x0168, 0x05); writel(iomuxc, 0x0538, 0x1b0b0); /* EIM_D25: GPIO3_25 (/HOLD) */

    /* Configure GPIO Directions & Initial Output States */
    /* GPIO2_30 (/CS) output HIGH */
    writel(gpio2, GPIO_DR, readl(gpio2, GPIO_DR) | (1u << 30));
    writel(gpio2, GPIO_GDIR, readl(gpio2, GPIO_GDIR) | (1u << 30));

    /* GPIO3_16 (SCLK out 0), GPIO3_18 (MOSI out 0), GPIO3_19 (/WP out 1), GPIO3_25 (/HOLD out 1), GPIO3_17 (MISO in) */
    uint32_t dr3 = (readl(gpio3, GPIO_DR) | (1u << 19) | (1u << 25)) & ~((1u << 16) | (1u << 18));
    uint32_t gdir3 = (readl(gpio3, GPIO_GDIR) | (1u << 16) | (1u << 18) | (1u << 19) | (1u << 25)) & ~(1u << 17);
    writel(gpio3, GPIO_DR, dr3);
    writel(gpio3, GPIO_GDIR, gdir3);

    usleep(500);

    /* Verify SPI Flash JEDEC ID (0x9F) */
    bb_set_cs(gpio2, 0);
    bb_delay();
    bb_spi_xfer_byte(gpio2, gpio3, 0x9F);
    uint8_t id[3];
    id[0] = bb_spi_xfer_byte(gpio2, gpio3, 0x00);
    id[1] = bb_spi_xfer_byte(gpio2, gpio3, 0x00);
    id[2] = bb_spi_xfer_byte(gpio2, gpio3, 0x00);
    bb_set_cs(gpio2, 1);
    bb_delay();

    bool valid_flash = false;
    const char *flash_name = "SPI-NOR";
    if (id[0] == 0xEF && id[1] == 0x40 && id[2] == 0x17) {
        flash_name = "Winbond W25Q64";
        valid_flash = true;
    } else if (id[0] == 0xC8 && id[1] == 0x40 && id[2] == 0x17) {
        flash_name = "GigaDevice GD25Q64";
        valid_flash = true;
    } else if (id[0] == 0xEF && id[1] == 0x40 && id[2] == 0x19) {
        flash_name = "Winbond W25Q256";
        valid_flash = true;
    } else if (id[0] != 0x00 && id[0] != 0xFF) {
        valid_flash = true;
    }

    if (valid_flash) {
        /* Read Factory Config Block at 0x7FE000 (2048 bytes) */
        uint8_t fact_buf[FLASH_CFG_BLOCK_SIZE];
        bb_spi_read(gpio2, gpio3, FLASH_FACTORY_CFG_ADDR, fact_buf, sizeof(fact_buf));
        parse_config_stream((const char *)fact_buf, sizeof(fact_buf), store);

        /* Read User/Console Config Block at 0x7FF000 (2048 bytes) */
        uint8_t user_buf[FLASH_CFG_BLOCK_SIZE];
        bb_spi_read(gpio2, gpio3, FLASH_USER_CFG_ADDR, user_buf, sizeof(user_buf));
        parse_config_stream((const char *)user_buf, sizeof(user_buf), store);

        snprintf(store->source_path, sizeof(store->source_path),
                 "SPI NOR Flash (%s, 0x%06X)", flash_name, FLASH_FACTORY_CFG_ADDR);
    }

    munmap(iomuxc, 0x1000);
    munmap(gpio2, 0x1000);
    munmap(gpio3, 0x1000);
    close(mem_fd);

    return valid_flash && (store->count > 0);
}

/* Scans a binary file/block device looking for config sequences */
static bool load_config_from_file(const char *path, wing_config_store_t *store) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;

    char *chunk = malloc(SCAN_CHUNK_SIZE + 1024);
    if (!chunk) {
        close(fd);
        return false;
    }

    size_t total_read = 0;
    ssize_t n;
    bool found = false;

    while (total_read < MAX_SCAN_SIZE && (n = read(fd, chunk, SCAN_CHUNK_SIZE)) > 0) {
        if (memmem(chunk, n, "MAC=", 4) != NULL ||
            memmem(chunk, n, ":NGC", 4) != NULL ||
            memmem(chunk, n, "MODEL=", 6) != NULL ||
            memmem(chunk, n, "SN=", 3) != NULL) {

            const char *sig = memmem(chunk, n, "MAC=", 4);
            if (!sig) sig = memmem(chunk, n, ":NGC", 4);
            if (!sig) sig = memmem(chunk, n, "SN=", 3);

            if (sig) {
                const unsigned char *block_start = (const unsigned char *)sig;
                while ((const char *)block_start > chunk && *(block_start - 1) != 0x00 && *(block_start - 1) != 0xFF) {
                    block_start--;
                }
                size_t parsed = parse_config_stream((const char *)block_start, (chunk + n) - (const char *)block_start, store);
                if (parsed > 0) {
                    safe_strcpy(store->source_path, path, sizeof(store->source_path));
                    found = true;
                    break;
                }
            }
        }
        total_read += n;
    }

    free(chunk);
    close(fd);
    return found && (store->count > 0);
}

/* Automatically search for config store in default locations */
static bool auto_discover_config(wing_config_store_t *store) {
    /* Step 1: Direct SPI Hardware Read (Stock Behringer Factory Config Location) */
    if (read_spi_flash_config(store)) {
        return true;
    }

    /* Step 2: MTD / eMMC Storage Fallback */
    for (int i = 0; default_scan_paths[i] != NULL; i++) {
        const char *p = default_scan_paths[i];
        if (access(p, R_OK) == 0) {
            if (load_config_from_file(p, store)) {
                return true;
            }
        }
    }
    return false;
}

/* Set network interface MAC address via SIOCSIFHWADDR ioctl */
static bool apply_interface_mac(const char *ifname, const unsigned char mac[6]) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return false;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    safe_strcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));

    /* Try setting HW address directly (supported while UP on many drivers including DSA) */
    ifr.ifr_hwaddr.sa_family = ARPHRD_ETHER;
    memcpy(ifr.ifr_hwaddr.sa_data, mac, 6);
    int res = ioctl(fd, SIOCSIFHWADDR, &ifr);
    if (res == 0) {
        close(fd);
        return true;
    }

    /* If direct set fails, temporarily toggle IFF_UP */
    if (ioctl(fd, SIOCGIFFLAGS, &ifr) < 0) {
        close(fd);
        return false;
    }

    short orig_flags = ifr.ifr_flags;
    ifr.ifr_flags &= ~IFF_UP;
    ioctl(fd, SIOCSIFFLAGS, &ifr);

    ifr.ifr_hwaddr.sa_family = ARPHRD_ETHER;
    memcpy(ifr.ifr_hwaddr.sa_data, mac, 6);
    res = ioctl(fd, SIOCSIFHWADDR, &ifr);

    if (orig_flags & IFF_UP) {
        ifr.ifr_flags = orig_flags;
        ioctl(fd, SIOCSIFFLAGS, &ifr);
    }

    close(fd);
    return (res == 0);
}

/* Sets MAC addresses for all available ethernet interfaces */
static int apply_all_macs(const wing_config_store_t *store, bool verbose) {
    if (!store->has_mac) {
        if (verbose) fprintf(stderr, "[wing-syscfg] No valid MAC address found in config store.\n");
        return 1;
    }

    char mac_str[20];
    format_mac(store->mac, mac_str, sizeof(mac_str));

    if (verbose) {
        printf("[wing-syscfg] Applying Factory MAC: %s (from %s)\n", mac_str, store->source_path);
    }

    DIR *dir = opendir("/sys/class/net");
    if (!dir) {
        perror("opendir /sys/class/net");
        return 1;
    }

    struct dirent *ent;
    int applied_count = 0;

    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.' || strcmp(ent->d_name, "lo") == 0 ||
            strncmp(ent->d_name, "usb", 3) == 0 || strncmp(ent->d_name, "dummy", 5) == 0) {
            continue;
        }

        /* Check if it's an ethernet device */
        char type_path[512];
        snprintf(type_path, sizeof(type_path), "/sys/class/net/%s/type", ent->d_name);
        FILE *tf = fopen(type_path, "r");
        if (tf) {
            int dev_type = 0;
            if (fscanf(tf, "%d", &dev_type) == 1 && dev_type == 1 /* ARPHRD_ETHER */) {
                unsigned char iface_mac[6];
                memcpy(iface_mac, store->mac, 6);

                /* For dual switch ports: lan1 gets base MAC, lan2 gets base MAC + 1 */
                if (strcmp(ent->d_name, "lan2") == 0) {
                    iface_mac[5] = (unsigned char)(iface_mac[5] + 1);
                }

                char iface_mac_str[20];
                format_mac(iface_mac, iface_mac_str, sizeof(iface_mac_str));

                if (apply_interface_mac(ent->d_name, iface_mac)) {
                    if (verbose) {
                        printf("[wing-syscfg] Successfully set %s HWaddr to %s\n", ent->d_name, iface_mac_str);
                    }
                    applied_count++;
                } else {
                    if (verbose) {
                        fprintf(stderr, "[wing-syscfg] Warning: failed to set HWaddr on %s: %s\n",
                                ent->d_name, strerror(errno));
                    }
                }
            }
            fclose(tf);
        }
    }

    closedir(dir);
    return (applied_count > 0) ? 0 : 2;
}

/* Get single value by key */
static const char *store_get_val(const wing_config_store_t *store, const char *key) {
    for (size_t i = 0; i < store->count; i++) {
        if (strcasecmp(store->entries[i].key, key) == 0) {
            return store->entries[i].val;
        }
    }
    return NULL;
}

/* Print human-readable dump table */
static void dump_config_table(const wing_config_store_t *store) {
    printf("===================================================================\n");
    printf(" Behringer Wing System Configuration\n");
    printf(" Source: %s\n", store->source_path[0] ? store->source_path : "in-memory");
    printf("===================================================================\n");
    printf(" %-20s | %s\n", "KEY", "VALUE");
    printf("---------------------+---------------------------------------------\n");

    for (size_t i = 0; i < store->count; i++) {
        if (strcasecmp(store->entries[i].key, "MAC") == 0 && store->has_mac) {
            char fmt[20];
            format_mac(store->mac, fmt, sizeof(fmt));
            printf(" %-20s | %s  (formatted: %s)\n", store->entries[i].key, store->entries[i].val, fmt);
        } else {
            printf(" %-20s | %s\n", store->entries[i].key, store->entries[i].val);
        }
    }
    printf("===================================================================\n");
    printf(" Total parameters: %zu\n", store->count);
}

/* Print JSON output */
static void dump_config_json(const wing_config_store_t *store) {
    printf("{\n");
    printf("  \"_source\": \"%s\",\n", store->source_path);
    for (size_t i = 0; i < store->count; i++) {
        printf("  \"%s\": \"", store->entries[i].key);
        for (const char *p = store->entries[i].val; *p; p++) {
            if (*p == '"' || *p == '\\') putchar('\\');
            if (*p >= 0x20 && *p <= 0x7E) putchar(*p);
        }
        printf("\"%s\n", (i + 1 < store->count || store->has_mac) ? "," : "");
    }
    if (store->has_mac) {
        char fmt[20];
        format_mac(store->mac, fmt, sizeof(fmt));
        printf("  \"MAC_formatted\": \"%s\"\n", fmt);
    }
    printf("}\n");
}

static void print_usage(const char *prog) {
    printf("Behringer Wing System Configuration Tool\n\n");
    printf("Usage: %s [OPTIONS] [FILE_OR_DEVICE]\n\n", prog);
    printf("Options:\n");
    printf("  -a, --apply-mac       Set MAC address on network interfaces (lan1, lan2, eth0)\n");
    printf("  -d, --dump            Dump all configuration parameters in formatted table\n");
    printf("  -j, --json            Output full configuration as JSON\n");
    printf("  -g, --get KEY         Print value of specified configuration key\n");
    printf("  -f, --file PATH       Specify storage device or binary dump path\n");
    printf("  -q, --quiet           Suppress informational messages during --apply-mac\n");
    printf("  -h, --help            Show this help text\n\n");
    printf("Examples:\n");
    printf("  %s --apply-mac\n", prog);
    printf("  %s --dump\n", prog);
    printf("  %s --get MAC\n", prog);
    printf("  %s --get SN\n", prog);
    printf("  %s --json\n", prog);
}

int main(int argc, char *argv[]) {
    bool opt_apply_mac = false;
    bool opt_dump = false;
    bool opt_json = false;
    bool opt_quiet = false;
    const char *opt_get_key = NULL;
    const char *custom_path = NULL;

    static struct option long_options[] = {
        {"apply-mac", no_argument,       0, 'a'},
        {"dump",      no_argument,       0, 'd'},
        {"json",      no_argument,       0, 'j'},
        {"get",       required_argument, 0, 'g'},
        {"file",      required_argument, 0, 'f'},
        {"quiet",     no_argument,       0, 'q'},
        {"help",      no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "adjg:f:qh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'a': opt_apply_mac = true; break;
            case 'd': opt_dump = true; break;
            case 'j': opt_json = true; break;
            case 'g': opt_get_key = optarg; break;
            case 'f': custom_path = optarg; break;
            case 'q': opt_quiet = true; break;
            case 'h': print_usage(argv[0]); return 0;
            default:  print_usage(argv[0]); return 1;
        }
    }

    if (optind < argc && !custom_path) {
        custom_path = argv[optind];
    }

    /* Default action if none specified: dump table */
    if (!opt_apply_mac && !opt_dump && !opt_json && !opt_get_key) {
        opt_dump = true;
    }

    wing_config_store_t store;
    memset(&store, 0, sizeof(store));

    bool loaded = false;
    if (custom_path) {
        loaded = load_config_from_file(custom_path, &store);
        if (!loaded) {
            fprintf(stderr, "[wing-syscfg] Error: Failed to find valid system config in '%s'\n", custom_path);
            return 1;
        }
    } else {
        loaded = auto_discover_config(&store);
        if (!loaded) {
            if (!opt_quiet) {
                fprintf(stderr, "[wing-syscfg] Error: Could not locate Behringer Wing config block on any storage device.\n");
            }
            return 1;
        }
    }

    int rc = 0;

    if (opt_get_key) {
        const char *val = store_get_val(&store, opt_get_key);
        if (val) {
            printf("%s\n", val);
        } else {
            if (!opt_quiet) fprintf(stderr, "[wing-syscfg] Key '%s' not found.\n", opt_get_key);
            rc = 1;
        }
    }

    if (opt_dump) {
        dump_config_table(&store);
    }

    if (opt_json) {
        dump_config_json(&store);
    }

    if (opt_apply_mac) {
        rc = apply_all_macs(&store, !opt_quiet);
    }

    return rc;
}
