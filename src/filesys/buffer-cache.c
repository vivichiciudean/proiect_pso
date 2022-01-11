#include "buffer-cache.h"

struct cache_table_entry cache_table[CACHE_SIZE];

void cache_init(){
    lock_init(&cache_lock);
    for(int i = 0; i < CACHE_SIZE; i++){
        cache_table[i].occupied = false;
        cache_table[i].open_cnt = 0;
        cache_table[i].dirty = false;
        cache_table[i].accessed = false;
    }

  //thread_create("cache_writeback", 0, func_periodic_writer, NULL);
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
        printf("Need eviction!\n");
        return 0;
    }
    return idx;
}

/*
    Modificam valoarea sectorului corespunzator din cache
*/
void cache_write(block_sector_t disk_sector, const void *source_buffer){
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
    memcpy (cache_table[idx].block, source_buffer, BLOCK_SECTOR_SIZE); 

    lock_release(&cache_lock);
}


/*
    Citim valoarea sectorului corespunzator din cache si il punem in buffer-ul destinatie
*/
void cache_read(block_sector_t disk_sector, void *destination_buffer){
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
    memcpy (destination_buffer, cache_table[idx].block, BLOCK_SECTOR_SIZE); 

    lock_release(&cache_lock);
}


//Poti face writeback fie la evict, fie la close SAU mereu la o cuanta de timp
/*
    Scriem toate datele din cache pe disc
*/
void cache_writeback(){
    lock_acquire (&cache_lock);

    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache_table[i].occupied && cache_table[i].dirty){
            //facem "flush"
            block_write (fs_device, cache_table[i].disk_sector, cache_table[i].block);
            cache_table[i].dirty = false;
        }
    }

    lock_release (&cache_lock);
}