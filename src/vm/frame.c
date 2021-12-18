#include "frame.h"
#include <bitmap.h>
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include <stdio.h>
#include <string.h>
#include "swap.h"
#include "spte.h"

static struct frame_entry * frame_table;
static void * user_frames = NULL;

#define FRAME_FREE 0
#define FRAME_USED 1
static struct bitmap *free_frames_bitmap;

void frame_table_init(size_t number_of_user_pages)
{
	// allocate an array of frame entries
	//printf("[frame_table] Initialize for %d frames\n", number_of_user_pages);
	frame_table = malloc(number_of_user_pages * sizeof(struct frame_entry));
	if(NULL == frame_table)
	{
		PANIC("Unable to allocate space for the frame table.");
	}
	memset(frame_table, 0, number_of_user_pages * sizeof(struct frame_entry));

	user_frames = palloc_get_multiple(PAL_USER, number_of_user_pages);
	if(NULL == user_frames)
	{
		PANIC("Unable to claim user space for the frame manager.");
	}

	//printf("[frame_table] Claimed %d user frames\n", number_of_user_pages);
	//printf("[frame_table] Address at 0x%X\n", user_frames);

	// initialize a bitmap to represent the free frames
	free_frames_bitmap = bitmap_create( number_of_user_pages);
	if(NULL == free_frames_bitmap)
	{
	    PANIC("Unable to initialize swap table!");
	}

	// mark all the indexes in the frame table as free
	bitmap_set_all(free_frames_bitmap, FRAME_FREE);
}

void *frame_alloc( enum palloc_flags flags,  struct supl_pte* spte)
{
	ASSERT(0 != (flags & PAL_USER));
	ASSERT(NULL != frame_table);
	ASSERT(NULL != free_frames_bitmap);

    // find the first free frame;
	size_t free_idx = 0;

	free_idx = bitmap_scan_and_flip(free_frames_bitmap, 0, 1, FRAME_FREE);
	if(BITMAP_ERROR == free_idx)
	{
		PANIC("[frame_table] Table is full.");
		// TODO: evict a page and install a new one
	}
	//printf("[frame_table] Allocated frame with index = %d\n", free_idx);

	frame_table[free_idx].spte = spte;
    frame_table[free_idx].ownner_thread = thread_current();

    if(0 != (PAL_ZERO & flags))
    {
    	memset((char *)user_frames + PGSIZE * free_idx, 0, PGSIZE);
    }

    //frame_evict(free_idx);

    return (char *)user_frames + PGSIZE * free_idx;
}

void frame_evict( void *kernel_va)
{
	ASSERT(NULL != frame_table);
	ASSERT(NULL != free_frames_bitmap);


		//cautam la ce index de pagina fizica se afla adresa virtuala

	// HINT: Compute the frame_index for the kernel_va, see frame_free
	// HINT: struct supl_pte * spte = frame_table[frame_idx].spte;

	// swap the frame out, mark the spte as swapped out. (trebuie luat de aici si dus in swap partition)
	// mark the entry as free

	// HINT: Compute the frame_index for the kernel_va, see frame_free
	size_t idx = ((size_t) kernel_va - (size_t)user_frames)/PGSIZE;
	// HINT: struct supl_pte * spte = frame_table[frame_idx].spte;
 	struct supl_pte * spte = frame_table[idx].spte;
	// swap the frame out, mark the spte as swapped out.
	spte->swap_idx = swap_out(kernel_va);
	spte->swapped_out = true;

	// mark the entry as free
	bitmap_set(free_frames_bitmap, idx, FRAME_FREE);
}

void * frame_swap_in(struct supl_pte* spte)
{
	ASSERT(NULL != frame_table);
	ASSERT(NULL != free_frames_bitmap);

	// find the first free frame and mark it as used
	size_t free_idx = bitmap_scan_and_flip(free_frames_bitmap, 0, 1, FRAME_FREE);

	// swap in
	swap_in(spte->swap_idx, user_frames + PGSIZE * free_idx);

	return (char *)user_frames + PGSIZE * free_idx;
}

void frame_free(void *frame_addr)
{
	ASSERT(frame_addr >=  user_frames);
	ASSERT(NULL != frame_table);
	ASSERT(NULL != free_frames_bitmap);

	size_t idx = ((size_t) frame_addr - (size_t)user_frames)/PGSIZE;

	//printf("[frame_table] Free frame with index = %d\n", idx);
	bitmap_set(free_frames_bitmap, idx, FRAME_FREE);
}
