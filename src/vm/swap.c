#include "swap.h"
#include <stdio.h>

struct bitmap * swap_table;
struct block  * swap_block;

void swap_init(void)
{
	block_sector_t swap_number_of_sectors = 0;

	// get the swap block using block_get_role(BLOCK_SWAP)
	swap_block = block_get_role(BLOCK_SWAP);
    if(NULL == swap_block)
    {
    	PANIC("Unable to get the swap block!");
    }

    // get the number of sectors in the swap block
    swap_number_of_sectors = block_size(swap_block);

    //printf("[swap_table] Initializing for %d sectors\n", swap_number_of_sectors);
    //printf("[swap_table] Number of entries in the bitmap: %d\n",  swap_number_of_sectors / SWAP_SECTORS_PER_PAGE);

    // initialize a bitmap to represent the free "slots" in the swap space
    swap_table = bitmap_create( swap_number_of_sectors / SWAP_SECTORS_PER_PAGE);
    if(NULL == swap_table)
    {
    	PANIC("Unable to initialize swap table!");
    }
	
    // mark all the indexes in the swap table to free
    bitmap_set_all(swap_table, SWAP_FREE);
}


void swap_uninit(void)
{
	printf("[swap_table] destroying the swap table");
	bitmap_destroy(swap_table);
}

/*
 * Swap in a frame from the swap space to the disk
 */
void swap_in(size_t bitmap_idx, void *frame_addr)
{
	unsigned int i;

	ASSERT(NULL != swap_table);
	ASSERT(NULL != swap_block);
	ASSERT(bitmap_test(swap_table, bitmap_idx) == SWAP_USED);

    // swap in all the sectors for the current frame;
	printf("Let`s swap IN!\n");
	// swap out the frame to the swap space

	bitmap_set(swap_table, bitmap_idx, SWAP_FREE);
	for (i = 0; i < SWAP_SECTORS_PER_PAGE; i++) {
		block_read (swap_block, (bitmap_idx * SWAP_SECTORS_PER_PAGE) + i , frame_addr + (i * SWAP_SECTORS_PER_PAGE));
	}
}

/*
 * Save contents of a frame to the swap space
 */
size_t swap_out(void *frame_addr)
{
	unsigned int i;
    size_t free_idx = 0;

	ASSERT(NULL != swap_table);
	ASSERT(NULL != swap_block);

	// find a free index in the swap table
	//KERNEL PANIC daca swap partition e plin
	// swap out the frame to the swap space



	// find a free index in the swap table
	free_idx = bitmap_scan_and_flip (swap_table, 0, 1, SWAP_FREE);

	printf("Let`s swap!\n");
	// swap out the frame to the swap space

	for (i = 0; i < SWAP_SECTORS_PER_PAGE; i++) {
		block_write (swap_block, (free_idx * SWAP_SECTORS_PER_PAGE) + i , frame_addr + (i * SWAP_SECTORS_PER_PAGE));
	}
	return free_idx;
}

