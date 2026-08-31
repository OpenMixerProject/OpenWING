/*
 * wing_syscfg.c - Behringer Wing System Configuration Store Parser & MAC Configurator
 *
 * Reverse-engineered from stock Behringer Wing firmware (app_0x10004000.bin).
 * Parses the KEY=VALUE~ ASCII configuration stream from non-volatile storage
 * (eMMC partitions, SPI NOR flash, or memory dumps).
 *
 * Features:
 *   - Automatically discovers config block on eMMC/MTD/partitions.
 *   - Applies factory MAC address to network interfaces (lan1, lan2, eth0).
 *   - Allows reading individual keys (e.g. --get SN, --get MAC, --get DEVTYPE).
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
#include <net/if.h>
#include <net/if_arp.h>

#define MAX_ENTRIES 256
#define MAX_KEY_LEN 64
#define MAX_VAL_LEN 1024
#define SCAN_CHUNK_SIZE (256 * 1024)
#define MAX_SCAN_SIZE (64 * 1024 * 1024) /* Scan up to 64MB */

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
    "/dev/mmcblk2p1",
    "/dev/mmcblk2",
    "/dev/mmcblk0p1",
    "/dev/mmcblk0",
    "/dev/mmcblk2boot0",
    "/dev/mmcblk2boot1",
    "/dev/mtdblock0",
    "/dev/mtd0",
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

/* Parse hex bytes (e.g. "00:14:2D:BE:5B:47" or "00142DBE5B47") */
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

    if (strcasecmp(key, "MAC") == 0 || strcasecmp(key, "eth_cfg") == 0) {
        if (parse_mac_string(val, store->mac)) {
            store->has_mac = true;
        }
    }
}

/* Parse a memory buffer containing KEY=VALUE~ records */
static size_t parse_config_stream(const char *buf, size_t len, wing_config_store_t *store) {
    size_t initial_count = store->count;
    const char *ptr = buf;
    const char *end = buf + len;

    while (ptr < end && *ptr != '\0') {
        /* Skip leading non-alphanumeric junk or delimiters */
        while (ptr < end && (*ptr == '~' || *ptr == '\r' || *ptr == '\n' || (unsigned char)*ptr <= 0x20)) {
            ptr++;
        }
        if (ptr >= end || *ptr == '\0') break;

        const char *eq = memchr(ptr, '=', end - ptr);
        if (!eq) break;

        size_t key_len = eq - ptr;
        if (key_len == 0 || key_len >= MAX_KEY_LEN) {
            ptr = eq + 1;
            continue;
        }

        /* Check that key contains valid chars */
        bool valid_key = true;
        for (size_t i = 0; i < key_len; i++) {
            if (!isalnum((unsigned char)ptr[i]) && ptr[i] != '_' && ptr[i] != '-' && ptr[i] != '.') {
                valid_key = false;
                break;
            }
        }
        if (!valid_key) {
            ptr = eq + 1;
            continue;
        }

        const char *val_start = eq + 1;
        const char *val_end = val_start;
        while (val_end < end && *val_end != '~' && *val_end != '\0' && *val_end != '\n' && *val_end != '\r') {
            val_end++;
        }

        size_t val_len = val_end - val_start;
        if (val_len < MAX_VAL_LEN) {
            char k[MAX_KEY_LEN] = {0};
            char v[MAX_VAL_LEN] = {0};
            memcpy(k, ptr, key_len);
            memcpy(v, val_start, val_len);
            store_add_kv(store, k, v);
        }

        ptr = val_end;
        if (ptr < end && (*ptr == '~' || *ptr == '\n' || *ptr == '\r')) {
            ptr++;
        }
    }

    return store->count - initial_count;
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
        /* Check if chunk contains recognizable Wing config signatures */
        if (memmem(chunk, n, "MAC=", 4) != NULL ||
            memmem(chunk, n, "eth_cfg=", 8) != NULL ||
            memmem(chunk, n, "DEVTYPE=", 8) != NULL ||
            memmem(chunk, n, "MODEL=", 6) != NULL ||
            memmem(chunk, n, "SN=", 3) != NULL) {

            /* Find beginning of the cluster */
            const char *sig = memmem(chunk, n, "MAC=", 4);
            if (!sig) sig = memmem(chunk, n, "SN=", 3);
            if (!sig) sig = memmem(chunk, n, "DEVTYPE=", 8);

            if (sig) {
                /* Backtrack to beginning of record block if possible */
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

    /* Get current interface flags */
    if (ioctl(fd, SIOCGIFFLAGS, &ifr) < 0) {
        close(fd);
        return false;
    }

    short orig_flags = ifr.ifr_flags;
    /* Interface must be brought DOWN to change MAC address on most drivers */
    if (orig_flags & IFF_UP) {
        ifr.ifr_flags &= ~IFF_UP;
        ioctl(fd, SIOCSIFFLAGS, &ifr);
    }

    /* Set HW address */
    ifr.ifr_hwaddr.sa_family = ARPHRD_ETHER;
    memcpy(ifr.ifr_hwaddr.sa_data, mac, 6);
    int res = ioctl(fd, SIOCSIFHWADDR, &ifr);

    /* Restore original UP status if needed */
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
    printf(" Behringer Wing System Configuration (%s)\n", store->source_path[0] ? store->source_path : "in-memory");
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
        /* Escape quotes in values */
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
    printf("  %s --get SN\n", prog);
    printf("  %s --json -f /dev/mmcblk2p1\n", prog);
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
