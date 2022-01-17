#include "buffer-cache.h"

struct cache_table_entry cache_table[CACHE_SIZE];
static int round_robin_idx = 0;


//Poti face writeback fie la evict, fie la close SAU mereu la o cuanta de timp
/*
    Scriem toate datele din cache pe disc
*/
void cache_writeback(bool last_write_back){
    lock_acquire (&cache_lock);

    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache_table[i].occupied && cache_table[i].dirty){
            //facem "flush"
            block_write (fs_device, cache_table[i].disk_sector, cache_table[i].block);
            cache_table[i].dirty = false;
        }

        if(last_write_back) {
            cache_table[i].occupied = false;
            cache_table[i].open_cnt = 0;
            cache_table[i].dirty = false;
            cache_table[i].accessed = false;
        }
  
    }

    lock_release (&cache_lock);
}

void periodic_write(void *aux UNUSED)
{
    while(true)
    {
        timer_sleep(4 * TIMER_FREQ);
        cache_writeback(false);
    }
}

void cache_init(){
    lock_init(&cache_lock);
    for(int i = 0; i < CACHE_SIZE; i++){
        cache_table[i].occupied = false;
        cache_table[i].open_cnt = 0;
        cache_table[i].dirty = false;
        cache_table[i].accessed = false;
    }

  thread_create("cache_writeback", 0, periodic_write, NULL);
}

/*
    Functie folosita pentru a gasi un sector de pe disk in cache
*/
int cache_lookup(block_sector_t disk_sector) {
    for(int i = 0; i < CACHE_SIZE; i++) {
        if(cache_table[i].disk_sector == disk_sector) {
            if(cache_table[i].occupied) {
                return i;
            }
        }
    }
    return -1;
}

/*
    Pentru a gasi un slot liber in cache
*/
int cache_get_free_entry() {
    for(int i = 0; i < CACHE_SIZE; i++) {
        if(!cache_table[i].occupied)
        {
            cache_table[i].occupied = true;
            return i;
        }
    }

    return -1;
}

/*
    Cautam in tot cache-ul si daca avem un slot liber il returnam, altfel facem loc
*/
int cache_evict(){
    int idx = cache_get_free_entry();

    if(idx == -1){
        idx = (++round_robin_idx) % CACHE_SIZE;
        //printf("Need eviction!\n");
        if(cache_table[idx].dirty == true)
        {
          block_write(fs_device, cache_table[idx].disk_sector, &cache_table[idx].block);
        }

        cache_table[idx].occupied = false;
        cache_table[idx].open_cnt = 0;
        cache_table[idx].dirty = false;
        cache_table[idx].accessed = false;
    }
    return idx;
}

void cache_write_block(block_sector_t disk_sector, void *source_buffer){
    cache_write(disk_sector, source_buffer, 0, BLOCK_SECTOR_SIZE);
}

/*
    Modificam valoarea sectorului corespunzator din cache
*/
void cache_write(block_sector_t disk_sector, void *source_buffer, uint32_t sector_ofs, uint32_t size){
    lock_acquire(&cache_lock); //avem grija la sincronizare
 
    int idx = cache_lookup (disk_sector);

    if(idx == -1){
        //printf("Cache miss! Bring from memory (evict)\n");

        //nu il avem in memorie -> trebuie sa il aducem
        //evict -> daca avem unul liber il aduce, daca nu avem nimic liber scoatem unul
        idx = cache_evict();

        cache_table[idx].occupied = true;
        cache_table[idx].disk_sector = disk_sector;
        block_read (fs_device, disk_sector, cache_table[idx].block); //citim datele din disk
    }

    cache_table[idx].accessed = true;
    cache_table[idx].dirty = true;
    memcpy (cache_table[idx].block + sector_ofs, source_buffer, size); 

    lock_release(&cache_lock);
}

void cache_read_block(block_sector_t disk_sector, void *destination_buffer){
    cache_read(disk_sector, destination_buffer, 0, BLOCK_SECTOR_SIZE);
}

/*
    Citim valoarea sectorului corespunzator din cache si il punem in buffer-ul destinatie
*/
void cache_read(block_sector_t disk_sector, void *destination_buffer, uint32_t sector_ofs, uint32_t size){
    lock_acquire(&cache_lock); //avem grija la sincronizare
 
    int idx = cache_lookup (disk_sector);

    if(idx == -1){
        //printf("Cache miss! Bring from memory (evict)\n");

        //nu il avem in memorie -> trebuie sa il aducem
        //evict -> daca avem unul liber il aduce, daca nu avem nimic liber scoatem unul
        idx = cache_evict();

        cache_table[idx].occupied = true;
        cache_table[idx].disk_sector = disk_sector;
        cache_table[idx].dirty = false;
        block_read (fs_device, disk_sector, cache_table[idx].block); //citim datele din disk
    }

    cache_table[idx].accessed = true;
    //cache_table[idx].open_cnt++;
    memcpy (destination_buffer, cache_table[idx].block + sector_ofs, size); 

    lock_release(&cache_lock);
}