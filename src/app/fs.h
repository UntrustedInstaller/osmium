#ifndef FS_H
#define FS_H

#include "types.h"

#define FAT_EOF 0xFF8
#define FAT_FREE 0x000

typedef struct {
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t num_fats;
    uint16_t root_entries;
    uint16_t total_sectors;
    uint16_t sectors_per_fat;
    uint16_t root_dir_lba;
    uint16_t root_dir_sectors;
    uint16_t first_data_sector;
    uint16_t total_clusters;
    uint8_t volume_serial[4];
    uint8_t sectors_per_fat_buf[9 * 512];
    uint8_t root_dir_buf[14 * 512];
} filesystem_t;

extern filesystem_t fs;
extern uint8_t fs_initialized;

void fs_init(void);
// Re-reads the BPB, FAT and root directory from the boot drive. Used after a
// disk swap so new files become visible without a reboot. Resets the working
// directory to the root. Returns 0 on success, 1 if sector 0 is unreadable
// (no disk; cached volume kept), 2 if the BPB is not a supported FAT12 layout,
// or 3 if the FAT/root could not be read. On 2 and 3 the cached volume is
// dropped so no command can use the old geometry on a different disk.
uint8_t fs_reload(void);

// Returns 1 if the disk in the drive no longer matches the cached volume,
// judged by the BPB volume serial and the first root-directory sector. Used as
// a fallback when the BIOS has no floppy change-line support. Returns 0 when
// there is no readable disk, leaving the cached volume untouched.
uint8_t fs_media_mismatch(void);

// Read-only drive snapshot for diagnostics (see the return codes in fs.c).
// Does not alter the cache or clear the BIOS change line.
uint8_t fs_disk_state(void);
uint16_t fs_cluster_to_lba(uint16_t cluster);
uint16_t fs_next_cluster(uint16_t cluster);
// List the current directory. show_all != 0 also lists the "." and ".."
// entries of subdirectories.
void fs_list_dir(uint8_t show_all);
uint8_t fs_read_file(const char* name, void* buf, uint16_t max);
uint8_t fs_write_file(const char* name, const void* data, uint32_t size);
// fs_delete_file: 0 ok, 1 not found, 2 is a directory, 5 disk write failed
uint8_t fs_delete_file(const char* name);
// fs_copy: 0 ok, 1 source not found/unreadable, 2 copy failed (too big for the
//          buffer, disk full, or a directory copied into its own subtree)
uint8_t fs_copy(const char* src, const char* dst, void* buf, uint16_t buf_size);
// fs_rename: 0 ok, 1 not found / target exists / directory moved into itself,
//            2 destination full, 5 disk write failed
uint8_t fs_rename(const char* old_name, const char* new_name);

// ---- working directory & navigation ----
// Returns the current directory as a cluster number (0 = root).
uint16_t fs_get_cwd(void);
// Returns the absolute display path of the current directory, e.g. "\GAMES\SNAKE".
// An empty string "" means we are in the root directory.
const char* fs_get_cwd_path(void);
// Change directory. Accepts absolute ("\DIR\SUB") and relative ("DIR\SUB") paths,
// plus "." and ".." components. An empty path or "\" changes to root.
// Returns 0 on success, nonzero if the path does not resolve.
uint8_t fs_cd(const char* path);

// ---- directory operations ----
// Write failures are reported instead of being treated as success, so the
// shell never claims a change stuck when the disk refused it.
// fs_mkdir:  0 ok, 1 invalid / already exists, 2 parent directory full or disk full
uint8_t fs_mkdir(const char* path);
// fs_mkdir_p: like fs_mkdir, but creates any missing parent directories.
//   0 ok, 1 a path component exists as a file, 2 disk / directory full,
//   5 disk write failed
uint8_t fs_mkdir_p(const char* path);
// fs_rmdir:  0 ok, 1 not found, 2 not a directory, 3 not empty, 4 directory in use
uint8_t fs_rmdir(const char* path);
// fs_rm_recursive: deletes a file, or a directory and everything inside it.
//   0 ok, 1 not found, 2 could not remove (depth/disk error), 4 directory in use
uint8_t fs_rm_recursive(const char* path);

#endif