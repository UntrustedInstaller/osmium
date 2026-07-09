#ifndef FS_H
#define FS_H

#include "types.h"

#define FAT_EOF 0xFFF
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
    uint8_t sectors_per_fat_buf[9 * 512];
    uint8_t root_dir_buf[14 * 512];
} filesystem_t;

extern filesystem_t fs;

void fs_init(void);
uint16_t fs_cluster_to_lba(uint16_t cluster);
uint16_t fs_next_cluster(uint16_t cluster);
void fs_list_dir(void);
uint8_t fs_read_file(const char* name, void* buf, uint16_t max);
uint8_t fs_write_file(const char* name, const void* data, uint32_t size);
uint8_t fs_delete_file(const char* name);
uint8_t fs_copy(const char* src, const char* dst, void* buf, uint16_t buf_size);
uint8_t fs_rename(const char* old_name, const char* new_name);

#endif
