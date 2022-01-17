#include "devices/block.h"
#include "devices/timer.h"
#include "threads/synch.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/thread.h"

#define CACHE_SIZE 64

struct cache_table_entry {
    bool occupied;

    int open_cnt;

    //Pentru LRU/Clock algorithm
    bool accessed;
    bool dirty;

    uint8_t block[BLOCK_SECTOR_SIZE]; //fiecare block din cache trebuie sa aiba o copie perfecta a hard disk-ului
    block_sector_t disk_sector; //ca sa accesam efectiv
};

struct lock cache_lock;

void cache_init();
int cache_lookup(block_sector_t disk_sector);
int cache_get_free_entry();
int cache_evict();
void cache_read(block_sector_t disk_sector, void *destination_buffer, uint32_t sector_ofs, uint32_t size);
void cache_read_block(block_sector_t disk_sector, void *destination_buffer);
void cache_write(block_sector_t disk_sector, void *source_buffer, uint32_t sector_ofs, uint32_t size);
void cache_write_block(block_sector_t disk_sector, void *source_buffer);
void cache_writeback(bool last_write_back);