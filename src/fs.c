__asm__(".code16gcc\n");
#include "fs.h"

// Directory support lives here now. The filesystem is a proper FAT12 layout:
//   - Subdirectories are stored as cluster chains in the data area, each with
//     "."  (self) and ".." (parent) entries in their first sector.
//   - Every file operation resolves its path against the current working
//     directory (fs_cwd == 0 means root), and accepts absolute paths that
//     start with '\' or '/'.
//   - Path components "." and ".." are honoured during resolution.
//   - fs_mkdir / fs_rmdir create and remove directories; fs_copy recurses into
//     directories; fs_list_dir lists whatever directory we are in.
//
// Remaining gaps (deliberate): no long file names, no timestamps written for
// new entries, and directories can only grow inside their cluster chain (the
// root directory is fixed-size, 224 entries).

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

// ---- current working directory state ----
static uint16_t fs_cwd = 0;        // 0 = root directory
static char fs_cwd_path[96];       // absolute display path, "" = root
static uint8_t dir_sec[512];       // scratch buffer for one directory sector
static char name_stack[24][12];    // scratch for rebuilding the cwd path string
static uint8_t escratch[32];       // scratch for copying a directory entry

static void flush_fat(void);
static void flush_root(void);

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

// =====================================================================
//  DIRECTORY ACCESS
// =====================================================================

// Locates one 32-byte directory entry: the owning directory (0 = root),
// and for subdirectories the cluster whose sector contains it.
typedef struct {
    uint16_t dc;    // owning directory: 0 = root, else its first cluster
    uint16_t cl;    // subdir only: cluster whose sector holds the entry
    uint16_t off;   // byte offset of the 32-byte entry within the sector/root buffer
} fs_entry_t;

// Returns a writable pointer to the entry. For subdirectories this (re)loads
// the sector into dir_sec, so the caller must commit before touching dir_sec
// again (data writes go through wr_sector/rd_sector and never clobber it).
static uint8_t* dir_load(const fs_entry_t* e) {
    if (e->dc == 0) return fs.root_dir_buf + e->off;
    read_sector(fs_cluster_to_lba(e->cl), dir_sec);
    return dir_sec + e->off;
}

// Persists a modified entry back to disk.
static void dir_commit(const fs_entry_t* e) {
    if (e->dc == 0) { flush_root(); return; }
    write_sector(fs_cluster_to_lba(e->cl), dir_sec);
}

static int is_sep(char c) { return c == '\\' || c == '/'; }

static int has_sep(const char* s) {
    for (; *s; s++) if (is_sep(*s)) return 1;
    return 0;
}

static int streq(const char* a, const char* b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

// Do two entry descriptors refer to the same slot?
static int same_entry(const fs_entry_t* a, const fs_entry_t* b) {
    if (a->dc != b->dc || a->off != b->off) return 0;
    if (a->dc != 0 && a->cl != b->cl) return 0;
    return 1;
}

// Normalizes a base filename (no path) to an 11-byte DOS name: uppercase,
// space-padded to 11 chars, with a NUL terminator in out[11].
// Leading dots are part of the name (handles ".", "..", and dot-prefixed
// names); the extension separator is the first dot that appears after at
// least one non-dot character.
static void dos_name(const char* base, char* out) {
    char n8[8], e3[3];
    int i;
    for (i = 0; i < 8; i++) n8[i] = ' ';
    for (i = 0; i < 3; i++) e3[i] = ' ';

    int dot = -1;                       // index of the ext separator, or -1
    int prev_plain = 0;                 // seen a non-dot char yet?
    for (i = 0; base[i]; i++) {
        if (base[i] == '.') {
            if (prev_plain && dot < 0) dot = i;
        } else {
            prev_plain = 1;
        }
    }

    int nlen = 0;
    while (base[nlen] && nlen < 8 && (dot < 0 || nlen < dot)) nlen++;
    for (i = 0; i < nlen; i++) {
        char c = base[i];
        if (c >= 'a' && c <= 'z') c -= 0x20;
        n8[i] = c;
    }
    if (dot >= 0) {
        int elen = 0;
        const char* q = base + dot + 1;
        while (*q && elen < 3) { elen++; q++; }
        for (i = 0; i < elen; i++) {
            char c = base[dot + 1 + i];
            if (c >= 'a' && c <= 'z') c -= 0x20;
            e3[i] = c;
        }
    }

    for (i = 0; i < 8; i++) out[i] = n8[i];
    for (i = 0; i < 3; i++) out[8 + i] = e3[i];
    out[11] = 0;
}

// Scans directory dc for an entry named dos11 (11 bytes). Also records the
// first reusable slot (0x00 end marker or 0xE5 tombstone) in f_loc when
// have_free is non-NULL. Returns 1 if a matching entry was found.
static uint8_t dir_scan(uint16_t dc, const char* dos11,
                        fs_entry_t* m_loc, uint16_t* m_cluster, uint32_t* m_size, uint8_t* m_attr,
                        uint8_t* have_free, fs_entry_t* f_loc) {
    if (have_free) *have_free = 0;

    if (dc == 0) {
        int max = fs.root_dir_sectors * 512 / 32;
        for (int i = 0; i < max; i++) {
            uint8_t* e = fs.root_dir_buf + i * 32;
            if (e[0] == 0x00) {
                if (have_free && !*have_free) {
                    *have_free = 1;
                    f_loc->dc = 0; f_loc->cl = 0; f_loc->off = i * 32;
                }
                break;
            }
            if (e[0] == 0xE5) {
                if (have_free && !*have_free) {
                    *have_free = 1;
                    f_loc->dc = 0; f_loc->cl = 0; f_loc->off = i * 32;
                }
                continue;
            }
            if (e[11] == 0x0F) continue;
            if (dos11 && memcmp(e, dos11, 11) == 0) {
                if (m_loc)      { m_loc->dc = 0; m_loc->cl = 0; m_loc->off = i * 32; }
                if (m_cluster)  *m_cluster = e[26] | (e[27] << 8);
                if (m_size)     *m_size = e[28] | (e[29] << 8) | (e[30] << 16) | (e[31] << 24);
                if (m_attr)     *m_attr = e[11];
                return 1;
            }
        }
        return 0;
    }

    uint16_t c = dc;
    while (c >= 2 && c < FAT_EOF) {
        read_sector(fs_cluster_to_lba(c), dir_sec);
        for (int j = 0; j < 16; j++) {
            uint8_t* e = dir_sec + j * 32;
            if (e[0] == 0x00) {
                if (have_free && !*have_free) {
                    *have_free = 1;
                    f_loc->dc = dc; f_loc->cl = c; f_loc->off = j * 32;
                }
                return 0;
            }
            if (e[0] == 0xE5) {
                if (have_free && !*have_free) {
                    *have_free = 1;
                    f_loc->dc = dc; f_loc->cl = c; f_loc->off = j * 32;
                }
                continue;
            }
            if (e[11] == 0x0F) continue;
            if (dos11 && memcmp(e, dos11, 11) == 0) {
                if (m_loc)      { m_loc->dc = dc; m_loc->cl = c; m_loc->off = j * 32; }
                if (m_cluster)  *m_cluster = e[26] | (e[27] << 8);
                if (m_size)     *m_size = e[28] | (e[29] << 8) | (e[30] << 16) | (e[31] << 24);
                if (m_attr)     *m_attr = e[11];
                return 1;
            }
        }
        c = fs_next_cluster(c);
    }
    return 0;
}

// Looks up a directory by scanning its parent for an entry whose first cluster
// matches, returning that entry's 11-byte DOS name.
static uint8_t dir_scan_cluster(uint16_t dc, uint16_t cluster, char* out_dos11) {
    if (dc == 0) {
        int max = fs.root_dir_sectors * 512 / 32;
        for (int i = 0; i < max; i++) {
            uint8_t* e = fs.root_dir_buf + i * 32;
            if (e[0] == 0x00) break;
            if (e[0] == 0xE5 || e[11] == 0x0F) continue;
            if ((e[26] | (e[27] << 8)) == cluster) {
                if (out_dos11) { memcpy(out_dos11, e, 11); out_dos11[11] = 0; }
                return 1;
            }
        }
        return 0;
    }
    uint16_t c = dc;
    while (c >= 2 && c < FAT_EOF) {
        read_sector(fs_cluster_to_lba(c), dir_sec);
        for (int j = 0; j < 16; j++) {
            uint8_t* e = dir_sec + j * 32;
            if (e[0] == 0x00) goto sc_next;
            if (e[0] == 0xE5 || e[11] == 0x0F) continue;
            if ((e[26] | (e[27] << 8)) == cluster) {
                if (out_dos11) { memcpy(out_dos11, e, 11); out_dos11[11] = 0; }
                return 1;
            }
        }
    sc_next:
        c = fs_next_cluster(c);
    }
    return 0;
}

// Parent of a directory via its ".." entry (root has no parent).
static uint16_t dir_parent(uint16_t dc) {
    if (dc == 0) return 0;
    char dd[12];
    dos_name("..", dd);
    fs_entry_t m;
    uint16_t cc;
    uint32_t cs;
    uint8_t ca;
    if (dir_scan(dc, dd, &m, &cc, &cs, &ca, 0, 0)) return cc;
    return 0;
}

// Finds a free directory-entry slot in dc, extending the directory's cluster
// chain (subdirectories only) if it is full. Root is fixed-size.
// Returns 0 with *out set on success, 1 if the directory is full.
static uint8_t dir_alloc_slot(uint16_t dc, fs_entry_t* out) {
    if (dc == 0) {
        int max = fs.root_dir_sectors * 512 / 32;
        for (int i = 0; i < max; i++) {
            uint8_t* e = fs.root_dir_buf + i * 32;
            if (e[0] == 0x00 || e[0] == 0xE5) {
                out->dc = 0; out->cl = 0; out->off = i * 32;
                return 0;
            }
        }
        return 1;
    }

    uint8_t have;
    fs_entry_t fl;
    dir_scan(dc, 0, 0, 0, 0, 0, &have, &fl);
    if (have) { *out = fl; return 0; }

    uint16_t newc = find_free_cluster(2);
    if (newc == 0) return 1;
    uint16_t c = dc;
    while (c >= 2 && c < FAT_EOF) {
        uint16_t nx = get_fat_entry(c);
        if (nx >= FAT_EOF) break;
        c = nx;
    }
    set_fat_entry(c, newc);
    set_fat_entry(newc, FAT_EOF);
    flush_fat();

    memset(wr_sector, 0, 512);
    write_sector(fs_cluster_to_lba(newc), wr_sector);

    out->dc = dc; out->cl = newc; out->off = 0;
    return 0;
}

// =====================================================================
//  PATH RESOLUTION
// =====================================================================

// Resolves a path to (parent directory cluster, DOS basename).
// start is the cluster used for relative paths; paths beginning with an
// absolute separator start at root. Returns 1 on any resolution failure.
static uint8_t resolve_to(uint16_t start, const char* path, uint16_t* out_parent, char* out_dos) {
    uint16_t dc = is_sep(path[0]) ? 0 : start;
    const char* p = path;
    char comp[64];

    while (1) {
        while (*p && is_sep(*p)) p++;
        if (!*p) return 1;                  // nothing left → no target

        int n = 0;
        while (*p && !is_sep(*p) && n < 63) comp[n++] = *p++;
        comp[n] = 0;

        if (!*p) {                          // final component = basename
            if (streq(comp, ".") || streq(comp, "..")) return 1;
            *out_parent = dc;
            dos_name(comp, out_dos);
            return 0;
        }

        if (streq(comp, ".")) continue;
        if (streq(comp, "..")) { dc = dir_parent(dc); continue; }

        char d[12];
        dos_name(comp, d);
        fs_entry_t m;
        uint16_t cc;
        uint32_t cs;
        uint8_t ca;
        if (!dir_scan(dc, d, &m, &cc, &cs, &ca, 0, 0)) return 1;
        if (!(ca & 0x10)) return 1;         // not a directory
        dc = cc;
    }
}

// Resolves a path whose final component must be a directory (used by cd),
// returning that directory's cluster. Returns 1 on failure.
static uint8_t resolve_dir(uint16_t start, const char* path, uint16_t* out) {
    char buf[96];
    int bl = 0;
    while (path[bl] && bl < 95) { buf[bl] = path[bl]; bl++; }
    buf[bl] = 0;
    while (bl > 0 && is_sep(buf[bl - 1])) buf[--bl] = 0;   // strip trailing seps

    if (bl == 0) { *out = 0; return 0; }    // "" or all separators → root

    uint16_t dc = is_sep(buf[0]) ? 0 : start;
    const char* p = buf;
    while (*p && is_sep(*p)) p++;
    if (!*p) { *out = 0; return 0; }

    char comp[64];
    while (1) {
        while (*p && is_sep(*p)) p++;
        if (!*p) break;
        int n = 0;
        while (*p && !is_sep(*p) && n < 63) comp[n++] = *p++;
        comp[n] = 0;
        int last = !*p;

        if (last) {
            if (streq(comp, ".")) { *out = dc; return 0; }
            if (streq(comp, "..")) { *out = dir_parent(dc); return 0; }
            char d[12];
            dos_name(comp, d);
            fs_entry_t m;
            uint16_t cc;
            uint32_t cs;
            uint8_t ca;
            if (!dir_scan(dc, d, &m, &cc, &cs, &ca, 0, 0)) return 1;
            if (!(ca & 0x10)) return 1;
            *out = cc;
            return 0;
        }

        if (streq(comp, ".")) continue;
        if (streq(comp, "..")) { dc = dir_parent(dc); continue; }
        char d[12];
        dos_name(comp, d);
        fs_entry_t m;
        uint16_t cc;
        uint32_t cs;
        uint8_t ca;
        if (!dir_scan(dc, d, &m, &cc, &cs, &ca, 0, 0)) return 1;
        if (!(ca & 0x10)) return 1;
        dc = cc;
    }
    *out = dc;
    return 0;
}

// Rebuilds fs_cwd_path from fs_cwd by climbing the ".." links to root.
static void rebuild_cwd_str(void) {
    int n = 0;
    uint16_t dc = fs_cwd;
    while (dc != 0 && n < 24) {
        uint16_t parent = dir_parent(dc);
        if (!dir_scan_cluster(parent, dc, name_stack[n])) break;
        n++;
        dc = parent;
    }
    int len = 0;
    fs_cwd_path[0] = 0;
    for (int i = n - 1; i >= 0; i--) {
        int clen = 0;
        while (clen < 11 && name_stack[i][clen] != ' ') clen++;
        if (len + clen + 1 >= (int)sizeof(fs_cwd_path)) break;
        fs_cwd_path[len++] = '\\';
        for (int j = 0; j < clen; j++) fs_cwd_path[len++] = name_stack[i][j];
        fs_cwd_path[len] = 0;
    }
}

uint16_t fs_get_cwd(void) {
    return fs_cwd;
}

const char* fs_get_cwd_path(void) {
    return fs_cwd_path;
}

uint8_t fs_cd(const char* path) {
    if (!fs_initialized) fs_init();
    uint16_t target;
    if (resolve_dir(fs_cwd, path ? path : "", &target)) return 1;
    fs_cwd = target;
    rebuild_cwd_str();
    return 0;
}

// =====================================================================
//  DIRECTORY LISTING
// =====================================================================

static void list_print_entry(const uint8_t* e, uint32_t* files, uint32_t* dirs) {
    // Build the display name; directories are wrapped in [brackets] so they
    // stand out from files.
    char disp[16];
    int d = 0;
    if (e[11] & 0x10) disp[d++] = '[';
    for (int j = 0; j < 8; j++) {
        char c = e[j];
        if (c == ' ') break;
        disp[d++] = (c >= 'A' && c <= 'Z') ? c + 0x20 : c;
    }
    if (e[8] != ' ') {
        disp[d++] = '.';
        for (int j = 8; j < 11; j++) {
            char c = e[j];
            if (c == ' ') break;
            disp[d++] = (c >= 'A' && c <= 'Z') ? c + 0x20 : c;
        }
    }
    if (e[11] & 0x10) disp[d++] = ']';
    disp[d] = 0;

    for (int i = 0; i < d; i++) print_char(disp[i]);
    for (int s = d; s < 14; s++) print_char(' ');

    print_str(e[11] & 0x10 ? "DIR     " : "FILE    ");

    uint32_t size = e[28] | (e[29] << 8) | (e[30] << 16) | (e[31] << 24);
    uint16_t cluster = e[26] | (e[27] << 8);

    print_char(' ');
    print_int(size);
    print_str("      ");
    print_int(cluster);
    print_str("\r\n");
    if (e[11] & 0x10) (*dirs)++; else (*files)++;
}

void fs_list_dir(uint8_t show_all) {
    if (!fs_initialized) fs_init();

    if (fs_cwd != 0) {
        print_str("Directory: OS:");
        print_str(fs_cwd_path);
        print_str("\r\n");
    }

    print_str("Name          Type   Size    Cluster\r\n");
    print_str("------------- ------ ------- -------\r\n");
    uint32_t files = 0, dirs = 0;

    if (fs_cwd == 0) {
        int max = fs.root_dir_sectors * 512 / 32;
        for (int i = 0; i < max; i++) {
            uint8_t* e = fs.root_dir_buf + i * 32;
            if (e[0] == 0x00) break;
            if (e[0] == 0xE5 || e[11] == 0x0F) continue;
            if (e[11] & 0x08) continue;         // volume label
            list_print_entry(e, &files, &dirs);
        }
    } else {
        uint16_t c = fs_cwd;
        while (c >= 2 && c < FAT_EOF) {
            read_sector(fs_cluster_to_lba(c), dir_sec);
            for (int j = 0; j < 16; j++) {
                uint8_t* e = dir_sec + j * 32;
                if (e[0] == 0x00) goto dir_done;
                if (e[0] == 0xE5 || e[11] == 0x0F) continue;
                if (e[11] & 0x08) continue;
                if (!show_all && e[0] == '.' && (e[1] == ' ' || e[1] == '.')) continue; // . and ..
                list_print_entry(e, &files, &dirs);
            }
            c = fs_next_cluster(c);
        }
    dir_done: ;
    }

    print_str("\r\n");
    print_int(files);
    print_str(" file(s)\r\n");
    print_int(dirs);
    print_str(" folder(s)\r\n");
}

// =====================================================================
//  FILE I/O
// =====================================================================

uint8_t fs_read_file(const char* name, void* buf, uint16_t max) {
    if (!fs_initialized) fs_init();

    uint16_t parent, cluster;
    uint32_t size;
    char dos[12];
    fs_entry_t m;
    uint8_t attr;

    if (resolve_to(fs_cwd, name, &parent, dos)) return 1;
    if (!dir_scan(parent, dos, &m, &cluster, &size, &attr, 0, 0)) return 1;
    if (attr & 0x10) return 1;                   // can't read a directory as a file

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

// Frees a cluster chain back to the free list.
static void free_chain(uint16_t start) {
    uint16_t c = start;
    while (c >= 2 && c < FAT_EOF) {
        uint16_t next = get_fat_entry(c);
        set_fat_entry(c, FAT_FREE);
        if (next >= FAT_EOF) break;
        c = next;
    }
}

uint8_t fs_write_file(const char* name, const void* data, uint32_t size) {
    if (!fs_initialized) fs_init();

    uint16_t parent;
    char dos[12];
    fs_entry_t m;
    uint16_t old_cluster;
    uint32_t old_size;
    uint8_t old_attr;

    if (resolve_to(fs_cwd, name, &parent, dos)) return 1;
    int found = dir_scan(parent, dos, &m, &old_cluster, &old_size, &old_attr, 0, 0);
    if (found && (old_attr & 0x10)) return 2;   // target is a directory
    int exists = found;

    int needed = (size + 511) / 512;
    if (needed == 0) needed = 1;

    if (!exists) {
        if (dir_alloc_slot(parent, &m)) return 2;   // parent directory full
    }

    if (exists && old_cluster >= 2) free_chain(old_cluster);

    uint16_t first_cluster = 0;
    uint16_t prev = 0;
    uint16_t alloc_start = 2;

    for (int i = 0; i < needed; i++) {
        uint16_t free_c = find_free_cluster(alloc_start);
        if (free_c == 0) return 2;

        if (i == 0) first_cluster = free_c;
        if (prev != 0) set_fat_entry(prev, free_c);

        set_fat_entry(free_c, FAT_EOF);
        prev = free_c;

        uint32_t lba = fs_cluster_to_lba(free_c);
        memset(wr_sector, 0, 512);

        uint32_t off = (uint32_t)i * 512;
        uint32_t to_copy = size - off;
        if (to_copy > 512) to_copy = 512;
        memcpy(wr_sector, (const uint8_t*)data + off, to_copy);

        if (write_sector(lba, wr_sector)) return 1;

        alloc_start = free_c + 1;
        if (alloc_start > fs.total_clusters + 1) alloc_start = 2;
    }

    flush_fat();

    uint8_t* de = dir_load(&m);

    if (!exists) {
        memset(de, 0, 32);
        memcpy(de, dos, 11);
    }

    de[11] = 0x20;
    de[26] = first_cluster & 0xFF;
    de[27] = (first_cluster >> 8) & 0xFF;
    de[28] = size & 0xFF;
    de[29] = (size >> 8) & 0xFF;
    de[30] = (size >> 16) & 0xFF;
    de[31] = (size >> 24) & 0xFF;

    dir_commit(&m);
    return 0;
}

uint8_t fs_delete_file(const char* name) {
    if (!fs_initialized) fs_init();

    uint16_t parent;
    char dos[12];
    fs_entry_t m;
    uint16_t cluster;
    uint32_t size;
    uint8_t attr;

    if (resolve_to(fs_cwd, name, &parent, dos)) return 1;
    if (!dir_scan(parent, dos, &m, &cluster, &size, &attr, 0, 0)) return 1;
    if (attr & 0x10) return 2;                  // is a directory

    if (cluster >= 2) free_chain(cluster);
    flush_fat();

    uint8_t* de = dir_load(&m);
    de[0] = 0xE5;
    dir_commit(&m);
    return 0;
}

uint8_t fs_mkdir(const char* path) {
    if (!fs_initialized) fs_init();
    if (!path || !path[0]) return 1;

    uint16_t parent;
    char dos[12];
    if (resolve_to(fs_cwd, path, &parent, dos)) return 1;

    fs_entry_t m;
    uint16_t cc;
    uint32_t cs;
    uint8_t ca;
    if (dir_scan(parent, dos, &m, &cc, &cs, &ca, 0, 0)) return 1;   // already exists

    fs_entry_t loc;
    if (dir_alloc_slot(parent, &loc)) return 2;                     // parent full

    uint16_t nc = find_free_cluster(2);
    if (nc == 0) return 2;
    set_fat_entry(nc, FAT_EOF);
    flush_fat();

    // Build the new directory's first sector: "." (self) and ".." (parent).
    memset(dir_sec, 0, 512);
    char sym[12];
    uint8_t* de = dir_sec;
    dos_name(".", sym);
    memcpy(de, sym, 11);
    de[11] = 0x10;
    de[26] = nc & 0xFF;
    de[27] = (nc >> 8) & 0xFF;
    dos_name("..", sym);
    memcpy(de + 32, sym, 11);
    de[43] = 0x10;
    de[58] = parent & 0xFF;
    de[59] = (parent >> 8) & 0xFF;
    write_sector(fs_cluster_to_lba(nc), dir_sec);

    // Fill in the parent's entry for the new directory.
    uint8_t* pe = dir_load(&loc);
    memset(pe, 0, 32);
    memcpy(pe, dos, 11);
    pe[11] = 0x10;
    pe[26] = nc & 0xFF;
    pe[27] = (nc >> 8) & 0xFF;
    dir_commit(&loc);
    return 0;
}

// Like fs_mkdir, but creates any missing parent directories first.
uint8_t fs_mkdir_p(const char* path) {
    if (!fs_initialized) fs_init();
    if (!path || !path[0]) return 1;

    char tmp[80];
    int n = 0;
    while (path[n] && n < (int)sizeof(tmp) - 1) { tmp[n] = path[n]; n++; }
    tmp[n] = '\0';

    for (int i = 0; i < n; i++)
        if (tmp[i] == '/') tmp[i] = '\\';

    for (int i = 1; i <= n; i++) {
        if (tmp[i] == '\\' || tmp[i] == '\0') {
            if (!is_sep(tmp[i - 1])) {          // a real component boundary
                char save = tmp[i];
                tmp[i] = '\0';
                uint8_t r = fs_mkdir(tmp);       // 0 = created, 1 = already exists
                tmp[i] = save;
                if (r == 2) return 2;            // disk / parent full
            }
        }
    }
    return 0;
}

uint8_t fs_rmdir(const char* path) {
    if (!fs_initialized) fs_init();
    if (!path || !path[0]) return 1;

    uint16_t parent;
    char dos[12];
    if (resolve_to(fs_cwd, path, &parent, dos)) return 1;

    fs_entry_t m;
    uint16_t cc;
    uint32_t cs;
    uint8_t ca;
    if (!dir_scan(parent, dos, &m, &cc, &cs, &ca, 0, 0)) return 1;  // not found
    if (!(ca & 0x10)) return 2;                                     // not a directory

    // Refuse if the current directory (or an ancestor of it) is the target.
    for (uint16_t t = fs_cwd; t != 0; t = dir_parent(t))
        if (t == cc) return 4;

    // Must only contain "." and "..".
    if (cc >= 2) {
        uint16_t c = cc;
        while (c >= 2 && c < FAT_EOF) {
            read_sector(fs_cluster_to_lba(c), dir_sec);
            for (int j = 0; j < 16; j++) {
                uint8_t* e = dir_sec + j * 32;
                if (e[0] == 0x00) goto rd_next;
                if (e[0] == 0xE5 || e[11] == 0x0F) continue;
                if (e[0] == '.' && (e[1] == ' ' || e[1] == '.')) continue;
                return 3;                                           // not empty
            }
        rd_next:
            c = fs_next_cluster(c);
        }
    }

    free_chain(cc);
    flush_fat();

    uint8_t* de = dir_load(&m);
    de[0] = 0xE5;
    dir_commit(&m);
    return 0;
}

// Frees every entry inside the directory starting at `dircluster` (skipping
// "." / ".."), recursively clearing subdirectories and tombstoning each
// directory entry as it goes. Returns 0 on success, 1 if the depth limit is
// exceeded (corrupt / looping tree). Called on the main kernel stack, so the
// depth is capped and each frame kept small.
static uint8_t rm_tree_children(uint16_t dircluster, uint8_t depth) {
    if (depth > 8) return 1;

    uint16_t c = dircluster;
    while (c >= 2 && c < FAT_EOF) {
        for (int j = 0; j < 16; j++) {
            read_sector(fs_cluster_to_lba(c), dir_sec);
            uint8_t* e = dir_sec + j * 32;
            if (e[0] == 0x00) return 0;                 // end of entries
            if (e[0] == 0xE5 || e[11] == 0x0F) continue;
            if (e[11] & 0x08) continue;                 // volume label
            if (e[0] == '.' && (e[1] == ' ' || e[1] == '.')) continue;

            uint16_t ec = e[26] | (e[27] << 8);
            if (e[11] & 0x10) {                         // subdirectory
                if (rm_tree_children(ec, depth + 1)) return 1;
            }
            if (ec >= 2) free_chain(ec);

            // The recursive walk / free_chain clobbered dir_sec — reload.
            read_sector(fs_cluster_to_lba(c), dir_sec);
            dir_sec[j * 32] = 0xE5;
            write_sector(fs_cluster_to_lba(c), dir_sec);
        }
        c = fs_next_cluster(c);
    }
    return 0;
}

// Recursively deletes a file or directory tree.
//   0 ok, 1 not found, 2 could not remove (depth/disk error), 4 directory in use
uint8_t fs_rm_recursive(const char* path) {
    if (!fs_initialized) fs_init();
    if (!path || !path[0]) return 1;

    uint16_t parent;
    char dos[12];
    if (resolve_to(fs_cwd, path, &parent, dos)) return 1;

    fs_entry_t m;
    uint16_t cc;
    uint32_t cs;
    uint8_t ca;
    if (!dir_scan(parent, dos, &m, &cc, &cs, &ca, 0, 0)) return 1;   // not found

    if (ca & 0x10) {
        // Refuse if the target is the current directory or one of its ancestors.
        for (uint16_t t = fs_cwd; t != 0; t = dir_parent(t))
            if (t == cc) return 4;

        if (cc >= 2) {
            if (rm_tree_children(cc, 0)) return 2;
            free_chain(cc);
        }
    } else {
        if (cc >= 2) free_chain(cc);
    }
    flush_fat();

    uint8_t* de = dir_load(&m);
    de[0] = 0xE5;
    dir_commit(&m);
    return 0;
}

// Renames a file or moves it into another directory. Same-directory renames
// just update the name fields; cross-directory moves re-home the entry and
// (for directories) fix the ".." link inside the moved directory.
uint8_t fs_rename(const char* old_name, const char* new_name) {
    if (!fs_initialized) fs_init();

    uint16_t parent1;
    char dos1[12];
    fs_entry_t m1;
    uint16_t cluster1;
    uint32_t size1;
    uint8_t attr1;

    if (resolve_to(fs_cwd, old_name, &parent1, dos1)) return 1;
    if (!dir_scan(parent1, dos1, &m1, &cluster1, &size1, &attr1, 0, 0)) return 1;

    uint16_t parent2;
    char dos2[12];
    int same_dir = !has_sep(new_name);
    if (same_dir) {
        parent2 = parent1;
        dos_name(new_name, dos2);
    } else {
        if (resolve_to(fs_cwd, new_name, &parent2, dos2)) return 1;
    }

    // Refuse silly targets.
    if (dos2[0] == '.' && (dos2[1] == ' ' || dos2[1] == '.')) return 1;

    fs_entry_t m2;
    uint16_t c2;
    uint32_t s2;
    uint8_t a2;
    if (dir_scan(parent2, dos2, &m2, &c2, &s2, &a2, 0, 0)) {
        if (same_entry(&m1, &m2)) return 0;   // renaming a thing to itself — no-op
        return 1;                             // target already exists
    }

    if (parent2 == parent1) {
        uint8_t* de = dir_load(&m1);
        memcpy(de, dos2, 11);
        dir_commit(&m1);
        return 0;
    }

    if (dir_alloc_slot(parent2, &m2)) return 2;   // destination directory full

    // Snapshot the entry, then delete it from the source directory.
    uint8_t* de = dir_load(&m1);
    memcpy((void*)escratch, de, 32);
    de[0] = 0xE5;
    dir_commit(&m1);

    de = dir_load(&m2);
    memcpy(de, (const void*)escratch, 32);
    memcpy(de, dos2, 11);
    dir_commit(&m2);

    // Moving a directory: its ".." entry must now point at the new parent.
    if (attr1 & 0x10) {
        read_sector(fs_cluster_to_lba(cluster1), dir_sec);
        dir_sec[58] = parent2 & 0xFF;
        dir_sec[59] = (parent2 >> 8) & 0xFF;
        write_sector(fs_cluster_to_lba(cluster1), dir_sec);
    }
    return 0;
}

// Joins "\name" (trimmed to namelen) onto base into out, bounds checked.
static int path_join(char* out, int outsz, const char* base, const char* name, int namelen) {
    int blen = 0;
    while (base[blen]) blen++;
    if (blen + namelen + 1 >= outsz) return 1;
    int i = 0;
    for (; i < blen; i++) out[i] = base[i];
    out[i++] = '\\';
    for (int j = 0; j < namelen; j++) out[i++] = name[j];
    out[i] = 0;
    return 0;
}

// Recursively copies a directory (src → dst). Depth-limited so the real-mode
// stack doesn't get eaten; each level carries two child path buffers.
static uint8_t copy_dir_rec(const char* src, const char* dst, void* buf, uint16_t buf_size, uint8_t depth) {
    if (depth > 6) return 2;
    if (fs_mkdir(dst)) return 2;

    uint16_t parent;
    char dos[12];
    fs_entry_t m;
    uint16_t cluster;
    uint32_t size;
    uint8_t attr;

    if (resolve_to(fs_cwd, src, &parent, dos)) return 2;
    if (!dir_scan(parent, dos, &m, &cluster, &size, &attr, 0, 0)) return 2;
    if (!(attr & 0x10)) return 2;

    char child_src[96], child_dst[96];
    uint16_t c = cluster;
    while (c >= 2 && c < FAT_EOF) {
        for (int j = 0; j < 16; j++) {
            // Re-read the sector each iteration: recursive/file-copy calls
            // below reuse the shared dir_sec scratch buffer.
            read_sector(fs_cluster_to_lba(c), dir_sec);
            uint8_t* e = dir_sec + j * 32;
            if (e[0] == 0x00) goto cp_next;
            if (e[0] == 0xE5 || e[11] == 0x0F) continue;
            if (e[11] & 0x08) continue;
            if (e[0] == '.' && (e[1] == ' ' || e[1] == '.')) continue;

            char name[13];
            int k = 0;
            for (int b = 0; b < 8 && e[b] != ' '; b++) name[k++] = e[b];
            if (e[8] != ' ') {
                name[k++] = '.';
                for (int x = 8; x < 11 && e[x] != ' '; x++) name[k++] = e[x];
            }
            name[k] = 0;

            if (path_join(child_src, sizeof child_src, src, name, k)) return 2;
            if (path_join(child_dst, sizeof child_dst, dst, name, k)) return 2;

            if (e[11] & 0x10) {
                if (copy_dir_rec(child_src, child_dst, buf, buf_size, depth + 1)) return 2;
            } else {
                if (fs_copy(child_src, child_dst, buf, buf_size)) return 2;
            }
        }
    cp_next:
        c = fs_next_cluster(c);
    }
    return 0;
}

uint8_t fs_copy(const char* src, const char* dst, void* buf, uint16_t buf_size) {
    if (!fs_initialized) fs_init();

    uint16_t parent;
    char dos[12];
    fs_entry_t m;
    uint16_t cluster;
    uint32_t size;
    uint8_t attr;

    if (resolve_to(fs_cwd, src, &parent, dos)) return 1;
    if (!dir_scan(parent, dos, &m, &cluster, &size, &attr, 0, 0)) return 1;

    if (attr & 0x10) {
        // Directory copy — recurse (calls fs_copy for the files inside).
        return copy_dir_rec(src, dst, buf, buf_size, 0) ? 2 : 0;
    }

    if (size > buf_size) return 2;
    if (fs_read_file(src, buf, buf_size)) return 1;
    uint8_t r = fs_write_file(dst, buf, size);
    return r ? 2 : 0;
}