__asm__(".code16gcc\n");
#include "fs.h"

void* memset(void* s, int c, int n) {
    uint8_t* p = (uint8_t*)s;
    for (int i = 0; i < n; i++) p[i] = (uint8_t)c;
    return s;
}

void* memcpy(void* d, const void* s, int n) {
    uint8_t* pd = (uint8_t*)d;
    const uint8_t* ps = (const uint8_t*)s;
    for (int i = 0; i < n; i++) pd[i] = ps[i];
    return d;
}

static int memcmp(const void* a, const void* b, int n) {
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    for (int i = 0; i < n; i++) {
        if (pa[i] != pb[i]) return pa[i] - pb[i];
    }
    return 0;
}

filesystem_t fs;
uint8_t fs_initialized = 0;
static uint8_t wr_sector[512];
static uint8_t rd_sector[512];

void fs_init(void) {
    if (fs_initialized) return;
    uint8_t boot[512];
    read_sector(0, boot);

    fs.bytes_per_sector    = boot[11] | (boot[12] << 8);
    fs.sectors_per_cluster = boot[13];
    fs.reserved_sectors    = boot[14] | (boot[15] << 8);
    fs.num_fats            = boot[16];
    fs.root_entries        = boot[17] | (boot[18] << 8);
    fs.total_sectors       = boot[19] | (boot[20] << 8);
    fs.sectors_per_fat     = boot[22] | (boot[23] << 8);

    fs.root_dir_sectors = (fs.root_entries * 32 + fs.bytes_per_sector - 1) / fs.bytes_per_sector;
    fs.root_dir_lba = fs.reserved_sectors + (fs.num_fats * fs.sectors_per_fat);
    fs.first_data_sector = fs.root_dir_lba + fs.root_dir_sectors;
    fs.total_clusters = (fs.total_sectors - fs.first_data_sector) / fs.sectors_per_cluster;

    read_sectors(fs.reserved_sectors, fs.sectors_per_fat, fs.sectors_per_fat_buf);
    read_sectors(fs.root_dir_lba, fs.root_dir_sectors, fs.root_dir_buf);
    fs_initialized = 1;
}

uint16_t fs_cluster_to_lba(uint16_t cluster) {
    return fs.first_data_sector + (cluster - 2) * fs.sectors_per_cluster;
}

static uint16_t get_fat_entry(uint16_t cluster) {
    uint32_t offset = cluster * 3 / 2;
    uint16_t word = fs.sectors_per_fat_buf[offset] | ((uint16_t)fs.sectors_per_fat_buf[offset + 1] << 8);
    if (cluster % 2 == 0) return word & 0x0FFF;
    else return word >> 4;
}

static void set_fat_entry(uint16_t cluster, uint16_t value) {
    uint32_t offset = cluster * 3 / 2;
    uint16_t word = fs.sectors_per_fat_buf[offset] | ((uint16_t)fs.sectors_per_fat_buf[offset + 1] << 8);
    if (cluster % 2 == 0) {
        word = (word & 0xF000) | (value & 0x0FFF);
    } else {
        word = (word & 0x000F) | (value << 4);
    }
    fs.sectors_per_fat_buf[offset] = word & 0xFF;
    fs.sectors_per_fat_buf[offset + 1] = (word >> 8) & 0xFF;
}

uint16_t fs_next_cluster(uint16_t cluster) {
    return get_fat_entry(cluster);
}

static uint16_t find_free_cluster(uint16_t start) {
    for (uint16_t c = start; c <= fs.total_clusters + 1; c++)
        if (get_fat_entry(c) == FAT_FREE) return c;
    for (uint16_t c = 2; c < start; c++)
        if (get_fat_entry(c) == FAT_FREE) return c;
    return 0;
}

static int find_file(const char* name, uint16_t* out_cluster, uint32_t* out_size) {
    char search_name[9], search_ext[4];
    memset(search_name, ' ', 8); search_name[8] = '\0';
    memset(search_ext, ' ', 3);  search_ext[3] = '\0';

    const char* dot = 0;
    for (const char* p = name; *p; p++) if (*p == '.') { dot = p; break; }

    if (dot) {
        int nlen = dot - name; if (nlen > 8) nlen = 8;
        for (int i = 0; i < nlen; i++) {
            char c = name[i];
            if (c >= 'a' && c <= 'z') c -= 0x20;
            search_name[i] = c;
        }
        int elen = 0; while (dot[elen + 1]) elen++; if (elen > 3) elen = 3;
        for (int i = 0; i < elen; i++) {
            char c = dot[i + 1];
            if (c >= 'a' && c <= 'z') c -= 0x20;
            search_ext[i] = c;
        }
    } else {
        int nlen = 0; while (name[nlen]) nlen++; if (nlen > 8) nlen = 8;
        for (int i = 0; i < nlen; i++) {
            char c = name[i];
            if (c >= 'a' && c <= 'z') c -= 0x20;
            search_name[i] = c;
        }
    }

    int max = fs.root_dir_sectors * 512 / 32;
    for (int i = 0; i < max; i++) {
        uint8_t* e = fs.root_dir_buf + i * 32;
        if (e[0] == 0x00) break;
        if (e[0] == 0xE5) continue;
        if (e[11] == 0x0F) continue;

        if (memcmp(e, search_name, 8) == 0 && memcmp(e + 8, search_ext, 3) == 0) {
            *out_cluster = e[26] | (e[27] << 8);
            *out_size = e[28] | (e[29] << 8) | (e[30] << 16) | (e[31] << 24);
            return i;
        }
    }
    return -1;
}

void fs_list_dir(void) {
    if (!fs_initialized) fs_init();
    int max = fs.root_dir_sectors * 512 / 32;
    int count = 0;

    print_str("Name          Attrs  Size    Cluster\r\n");
    print_str("------------- ------ ------- -------\r\n");

    for (int i = 0; i < max; i++) {
        uint8_t* e = fs.root_dir_buf + i * 32;
        if (e[0] == 0x00) break;
        if (e[0] == 0xE5 || e[11] == 0x0F) continue;

        if (e[11] & 0x08) continue;

        for (int j = 0; j < 8; j++) {
            char c = e[j];
            if (c == ' ') break;
            print_char(c >= 'A' && c <= 'Z' ? c + 0x20 : c);
        }
        if (e[8] != ' ') {
            print_char('.');
            for (int j = 8; j < 11; j++) {
                char c = e[j];
                if (c == ' ') break;
                print_char(c >= 'A' && c <= 'Z' ? c + 0x20 : c);
            }
        }

        for (int s = 0; s < 14 - (e[8] != ' ' ? 9 : 8) + (e[8] == ' ' ? 1 : 0); s++)
            print_char(' ');

        print_str(e[11] & 0x10 ? "DIR     " : "FILE    ");

        uint32_t size = e[28] | (e[29] << 8) | (e[30] << 16) | (e[31] << 24);
        uint16_t cluster = e[26] | (e[27] << 8);

        print_char(' ');
        print_int(size);
        print_str("      ");
        print_int(cluster);
        print_str("\r\n");
        count++;
    }

    print_str("\r\n");
    print_int(count);
    print_str(" file(s)\r\n");
}

uint8_t fs_read_file(const char* name, void* buf, uint16_t max) {
    if (!fs_initialized) fs_init();
    uint16_t cluster;
    uint32_t size;

    if (find_file(name, &cluster, &size) < 0) return 1;

    uint32_t read = 0;
    while (1) {
        uint16_t start = cluster;
        uint16_t count = 1;
        uint16_t walk = cluster;

        while (1) {
            uint16_t next = fs_next_cluster(walk);
            if (next >= FAT_EOF) { walk = next; break; }
            if (next != walk + 1) { walk = next; break; }
            count++;
            walk = next;
        }

        uint32_t lba = fs_cluster_to_lba(start);
        uint16_t limit_sects = ((uint32_t)(max - read) + 511) / 512;
        uint16_t need_sects = ((size - read) + 511) / 512;
        if (count > limit_sects) count = limit_sects;
        if (count > need_sects) count = need_sects;

        if (count > 1) {
            if (read_sectors(lba, count, (uint8_t*)buf + read)) return 1;
        } else {
            if (read_sector(lba, rd_sector)) return 1;
            uint16_t to_copy = 512;
            if (read + to_copy > max) to_copy = max - read;
            if (read + to_copy > size) to_copy = size - read;
            memcpy((uint8_t*)buf + read, rd_sector, to_copy);
        }

        read += count * 512;
        if (read > size) { read = size; break; }
        if (read >= max) break;
        if (walk >= FAT_EOF) break;
        cluster = walk;
    }

    return 0;
}

static void flush_fat(void) {
    for (uint16_t i = 0; i < fs.sectors_per_fat; i++) {
        write_sector(fs.reserved_sectors + i, fs.sectors_per_fat_buf + i * 512);
        write_sector(fs.reserved_sectors + fs.sectors_per_fat + i, fs.sectors_per_fat_buf + i * 512);
    }
}

static void flush_root(void) {
    for (uint16_t i = 0; i < fs.root_dir_sectors; i++)
        write_sector(fs.root_dir_lba + i, fs.root_dir_buf + i * 512);
}

uint8_t fs_write_file(const char* name, const void* data, uint32_t size) {
    if (!fs_initialized) fs_init();
    uint16_t old_cluster;
    uint32_t old_size;
    int dir_slot = find_file(name, &old_cluster, &old_size);
    int exists = dir_slot >= 0;

    int needed = (size + 511) / 512;
    if (needed == 0) needed = 1;

    if (!exists) {
        int max = fs.root_dir_sectors * 512 / 32;
        dir_slot = -1;
        for (int i = 0; i < max; i++) {
            uint8_t* e = fs.root_dir_buf + i * 32;
            if (e[0] == 0x00 || e[0] == 0xE5) { dir_slot = i; break; }
        }
        if (dir_slot < 0) return 1;
    }

    if (exists && old_cluster >= 2) {
        uint16_t c = old_cluster;
        while (c >= 2) {
            uint16_t next = get_fat_entry(c);
            set_fat_entry(c, FAT_FREE);
            if (next >= FAT_EOF) break;
            c = next;
        }
    }

    uint16_t first_cluster = 0;
    uint16_t prev = 0;
    uint16_t alloc_start = 2;

    for (int i = 0; i < needed; i++) {
        uint16_t free_c = find_free_cluster(alloc_start);
        if (free_c == 0) return 1;

        if (i == 0) first_cluster = free_c;
        if (prev != 0) set_fat_entry(prev, free_c);

        set_fat_entry(free_c, FAT_EOF);
        prev = free_c;

        uint32_t lba = fs_cluster_to_lba(free_c);
        memset(wr_sector, 0, 512);

        uint32_t off = i * 512;
        uint32_t to_copy = size - off;
        if (to_copy > 512) to_copy = 512;
        memcpy(wr_sector, (const uint8_t*)data + off, to_copy);

        if (write_sector(lba, wr_sector)) return 1;

        alloc_start = free_c + 1;
        if (alloc_start > fs.total_clusters + 1) alloc_start = 2;
    }

    flush_fat();

    uint8_t* de;
    if (exists) {
        de = fs.root_dir_buf + dir_slot * 32;
    } else {
        de = fs.root_dir_buf + dir_slot * 32;
        memset(de, 0, 32);

        for (int i = 0; i < 8; i++) de[i] = ' ';
        for (int i = 8; i < 11; i++) de[i] = ' ';

        int fi = 0;
        for (const char* p = name; *p && *p != '.' && fi < 8; p++, fi++) {
            char c = *p;
            if (c >= 'a' && c <= 'z') c -= 0x20;
            de[fi] = c;
        }
        const char* dot = 0;
        for (const char* p = name; *p; p++) if (*p == '.') { dot = p; break; }
        if (dot) {
            int ei = 0;
            for (const char* p = dot + 1; *p && ei < 3; p++, ei++) {
                char c = *p;
                if (c >= 'a' && c <= 'z') c -= 0x20;
                de[8 + ei] = c;
            }
        }
    }

    de[11] = 0x20;
    de[26] = first_cluster & 0xFF;
    de[27] = (first_cluster >> 8) & 0xFF;
    de[28] = size & 0xFF;
    de[29] = (size >> 8) & 0xFF;
    de[30] = (size >> 16) & 0xFF;
    de[31] = (size >> 24) & 0xFF;

    flush_root();
    return 0;
}

uint8_t fs_delete_file(const char* name) {
    if (!fs_initialized) fs_init();
    uint16_t cluster;
    uint32_t size;
    if (find_file(name, &cluster, &size) < 0) return 1;

    if (cluster >= 2) {
        uint16_t c = cluster;
        while (c >= 2) {
            uint16_t next = get_fat_entry(c);
            set_fat_entry(c, FAT_FREE);
            if (next >= FAT_EOF) break;
            c = next;
        }
    }
    flush_fat();

    int max = fs.root_dir_sectors * 512 / 32;
    for (int i = 0; i < max; i++) {
        uint8_t* e = fs.root_dir_buf + i * 32;
        if (e[0] == 0x00 || e[0] == 0xE5 || e[11] == 0x0F) continue;

        char search_name[9], search_ext[4];
        memset(search_name, ' ', 8); search_name[8] = '\0';
        memset(search_ext, ' ', 3);  search_ext[3] = '\0';

        const char* dot = 0;
        for (const char* p = name; *p; p++) if (*p == '.') { dot = p; break; }

        if (dot) {
            int nlen = dot - name; if (nlen > 8) nlen = 8;
            for (int j = 0; j < nlen; j++) {
                char c = name[j];
                if (c >= 'a' && c <= 'z') c -= 0x20;
                search_name[j] = c;
            }
            int elen = 0; while (dot[elen + 1]) elen++; if (elen > 3) elen = 3;
            for (int j = 0; j < elen; j++) {
                char c = dot[j + 1];
                if (c >= 'a' && c <= 'z') c -= 0x20;
                search_ext[j] = c;
            }
        } else {
            int nlen = 0; while (name[nlen]) nlen++; if (nlen > 8) nlen = 8;
            for (int j = 0; j < nlen; j++) {
                char c = name[j];
                if (c >= 'a' && c <= 'z') c -= 0x20;
                search_name[j] = c;
            }
        }

        if (memcmp(e, search_name, 8) == 0 && memcmp(e + 8, search_ext, 3) == 0) {
            e[0] = 0xE5;
            flush_root();
            return 0;
        }
    }
    return 1;
}

uint8_t fs_copy(const char* src, const char* dst, void* buf, uint16_t buf_size) {
    if (!fs_initialized) fs_init();
    uint16_t cluster;
    uint32_t size;
    if (find_file(src, &cluster, &size) < 0) return 1;
    if (size > buf_size) return 2;
    if (fs_read_file(src, buf, buf_size)) return 1;
    return fs_write_file(dst, buf, size);
}

uint8_t fs_rename(const char* old_name, const char* new_name) {
    if (!fs_initialized) fs_init();
    uint16_t cluster;
    uint32_t size;
    int slot = find_file(old_name, &cluster, &size);
    if (slot < 0) return 1;

    uint8_t* de = fs.root_dir_buf + slot * 32;

    for (int i = 0; i < 8; i++) de[i] = ' ';
    for (int i = 8; i < 11; i++) de[i] = ' ';

    int fi = 0;
    for (const char* p = new_name; *p && *p != '.' && fi < 8; p++, fi++) {
        char c = *p;
        if (c >= 'a' && c <= 'z') c -= 0x20;
        de[fi] = c;
    }
    const char* dot = 0;
    for (const char* p = new_name; *p; p++) if (*p == '.') { dot = p; break; }
    if (dot) {
        int ei = 0;
        for (const char* p = dot + 1; *p && ei < 3; p++, ei++) {
            char c = *p;
            if (c >= 'a' && c <= 'z') c -= 0x20;
            de[8 + ei] = c;
        }
    }

    flush_root();
    return 0;
}
