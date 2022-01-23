#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/buffer-cache.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t blocks[NO_OF_BLOCKS];
    block_sector_t dir_parent;
    bool is_dir;

    int direct_count; 
    int indirect_count;
    int double_indirect_count; 

    block_sector_t start;               /* First data sector. */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    uint32_t unused[105];               /* Not used. */
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    
    struct lock inode_lock; // 

    struct inode_disk data;             /* Inode content. */
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
  {
    uint32_t idx;
    block_sector_t blocks[INDIRECT_BLOCK_ENTRIES];

    // direct blocks
    if (pos < DIRECT_BLOCKS * BLOCK_SECTOR_SIZE) {
      return inode->data.blocks[pos / BLOCK_SECTOR_SIZE];
    }
   
    if (pos < (DIRECT_BLOCKS + INDIRECT_BLOCKS * INDIRECT_BLOCK_ENTRIES) * BLOCK_SECTOR_SIZE) //indirect blocks
    {
      
      pos -= DIRECT_BLOCKS * BLOCK_SECTOR_SIZE;
      idx = pos / (INDIRECT_BLOCK_ENTRIES * BLOCK_SECTOR_SIZE) + DIRECT_BLOCKS;
      block_read(fs_device, inode->data.blocks[idx], &blocks);

      pos = pos % (INDIRECT_BLOCK_ENTRIES * BLOCK_SECTOR_SIZE);
      return blocks[pos / BLOCK_SECTOR_SIZE];
    }

    // double indirect blocks
    block_read(fs_device, inode->data.blocks[NO_OF_BLOCKS - 1], &blocks);

    pos -= (DIRECT_BLOCKS + INDIRECT_BLOCKS * INDIRECT_BLOCK_ENTRIES) * BLOCK_SECTOR_SIZE;
    idx = pos / (INDIRECT_BLOCK_ENTRIES * BLOCK_SECTOR_SIZE);
    block_read(fs_device, blocks[idx], &blocks);

    pos = pos % (INDIRECT_BLOCK_ENTRIES * BLOCK_SECTOR_SIZE);
    return blocks[pos / BLOCK_SECTOR_SIZE];
  
  }
  else
    return -1;
}

/*
  
*/
bool inode_grow(struct inode_disk* inode, off_t length) {
  uint8_t zeros[BLOCK_SECTOR_SIZE] = { 0 };

  size_t grow_sectors = bytes_to_sectors(length) - bytes_to_sectors(inode->length);
 
  inode->length = length;

  if (grow_sectors == 0)
  {
    return true;
  }

  // direct blocks (index < 13)
  while (inode->direct_count < DIRECT_BLOCKS && grow_sectors != 0)
  {
    free_map_allocate (1, &inode->blocks[inode->direct_count]);
    block_write(fs_device, inode->blocks[inode->direct_count], zeros);
    inode->direct_count++;
    grow_sectors--;
  }
 
  if(grow_sectors != 0) {
    block_sector_t blocks[128];

    // read from the first level of indirect blocks
    if (inode->indirect_count == 0)
      free_map_allocate(1, &inode->blocks[NO_OF_BLOCKS - 2]);
    else
      block_read(fs_device, inode->blocks[NO_OF_BLOCKS - 2], &blocks);

    // expand
    while (inode->indirect_count < INDIRECT_BLOCK_ENTRIES && grow_sectors != 0)
    {
      free_map_allocate(1, &blocks[inode->indirect_count]);
      block_write(fs_device, blocks[inode->indirect_count], zeros);
      inode->indirect_count++;
      grow_sectors--;
    }
    
    // write back expanded blocks
    block_write(fs_device, inode->blocks[NO_OF_BLOCKS - 2], &blocks);
  }

  // double indirect blocks
  if (grow_sectors != 0) {
    block_sector_t level_one[128];
    block_sector_t level_two[128];

    // read the first level block
    if (inode->double_indirect_count == 0)
      free_map_allocate(1, &inode->blocks[NO_OF_BLOCKS - 1]);
    else
      block_read(fs_device, inode->blocks[NO_OF_BLOCKS - 1], &level_one);

    // expand
    while (inode->double_indirect_count < DOUBLE_INDIRECT_BLOCK_ENTRIES && grow_sectors != 0)
    {
     
      uint32_t level_one_idx = inode->double_indirect_count / 128;
      uint32_t level_two_idx = 0;

      // read the second level block
      if (inode->double_indirect_count % 128 == 0)
        free_map_allocate(1, &level_one[level_one_idx]);
      else
        block_read(fs_device, level_one[level_one_idx], &level_two);

      // expand 
      while (level_two_idx < 128 && grow_sectors != 0) {
        free_map_allocate(1, &level_two[level_two_idx]);
        block_write(fs_device, level_two[level_two_idx], zeros);
        inode->double_indirect_count++;
        level_two_idx++;
        grow_sectors--;
      }

      // write back level two blocks
      block_write(fs_device, level_one[level_one_idx], &level_two);
    }

    block_write(fs_device, inode->blocks[NO_OF_BLOCKS - 1], &level_one);
  }
  
  return true;
}
/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool is_dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      
      disk_inode->length = 0;
      disk_inode->is_dir = is_dir;
      disk_inode->dir_parent = ROOT_DIR_SECTOR;
      disk_inode->magic = INODE_MAGIC;
      if (inode_grow(disk_inode, length)) {
        block_write (fs_device, sector, disk_inode);
        success = true; 
      } 
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init(&inode->inode_lock);
  block_read (fs_device, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

void
free_inode (struct inode *inode)
{
  size_t sector_count = bytes_to_sectors(inode->data.length);
  size_t idx = 0;
  
  if(sector_count == 0)
  {
    return;
  }

  // free direct blocks
  while (idx < DIRECT_BLOCKS && sector_count > 0)
  {
    free_map_release (inode->data.blocks[idx], 1);
    sector_count--;
    idx++;
  }

  // free indirect blocks
  if (sector_count > 0)
  {
    size_t blocks_to_free = inode->data.indirect_count;
    
    block_sector_t block[128];
    block_read(fs_device, inode->data.blocks[NO_OF_BLOCKS - 2], &block);

    for (int i = 0; i < blocks_to_free; i++)
    {
      free_map_release(block[i], 1);
      sector_count--;
    }

    free_map_release(inode->data.blocks[NO_OF_BLOCKS - 2], 1);
  }

  //ASSERT(sector_count != inode->data.double_indirect_count);

  // free double indirect blocks
  if (sector_count > 0)
  {
    block_sector_t level_one[128];
    block_sector_t level_two[128];

    // read the first level block
    block_read(fs_device, inode->data.blocks[NO_OF_BLOCKS - 1], &level_one);

    //v1  (inode->data.double_indirect_count / 128) + (inode->data.double_indirect_count % 128 == 0 ? 0 : 1)
    //v2  (inode->data.double_indirect_count + 127) / 128  round up
    for (int i = 0; i < (inode->data.double_indirect_count / 128) ; i++) //freeing the complete blocks
    {
      uint32_t level_one_idx = i;
      
      block_read(fs_device, level_one[level_one_idx], &level_two);

      for (int j = 0; j < 128; j++)
      {
        // free the sectors
        free_map_release(level_two[j], 1);
        sector_count--;
      }

      // free the second level block
      free_map_release(level_one[i], 1);
    }

    if(sector_count > 0) {
      block_read(fs_device, level_one[(inode->data.double_indirect_count / 128)], &level_two);

      for (int i = 0; i < (inode->data.double_indirect_count % 128) ; i++) //freeing the last block (incomplete)
      {          
        free_map_release(level_two[i], 1);
        sector_count--;
      }

      free_map_release(level_one[(inode->data.double_indirect_count / 128)], 1);
    }
    // finally, free the first level block
    free_map_release(inode->data.blocks[NO_OF_BLOCKS - 1], 1);
  }
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) {
        free_map_release (inode->sector, 1);
        // free_map_release (inode->data.start,
        //                   bytes_to_sectors (inode->data.length)); 
        free_inode(inode);
      }
      else { //writing back
        block_write(fs_device, inode->sector, &(inode->data));
      }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  //uint8_t *bounce = NULL; //Replace BB with the new cache

  // if(offset + size >= inode->data.length) // make sure we won`t read past the file`s end
  //   return 0;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      // if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
      //   {
      //     /* Read full sector directly into caller's buffer. */
      //     block_read (fs_device, sector_idx, buffer + bytes_read);
      //   }
      // else 
      //   {
      //     /* Read sector into bounce buffer, then partially copy
      //        into caller's buffer. */
      //     if (bounce == NULL) 
      //       {
      //         bounce = malloc (BLOCK_SECTOR_SIZE);
      //         if (bounce == NULL)
      //           break;
      //       }
      //     block_read (fs_device, sector_idx, bounce);
      //     memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
      //   }

      cache_read(sector_idx, buffer + bytes_read, sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  //free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  //uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  //printf("inceput de functie: %d %d %d %d \n", bytes_written, offset + size, inode->data.length, size);

  // for extandable files (directories do not require a locking mechanism)
  if(offset + size > inode->data.length) {
    //printf("da 1 \n");
    if(!inode->data.is_dir)
      lock_acquire(&inode->inode_lock);

    inode_grow(&(inode->data), offset + size);

    if(!inode->data.is_dir)
      lock_release(&inode->inode_lock);
  }


  while (size > 0) 
    {
      //printf("da 2 \n");
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      // if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
      //   {
      //     /* Write full sector directly to disk. */
      //     block_write (fs_device, sector_idx, buffer + bytes_written);
      //   }
      // else 
      //   {
      //     /* We need a bounce buffer. */
      //     if (bounce == NULL) 
      //       {
      //         bounce = malloc (BLOCK_SECTOR_SIZE);
      //         if (bounce == NULL)
      //           break;
      //       }

      //     /* If the sector contains data before or after the chunk
      //        we're writing, then we need to read in the sector
      //        first.  Otherwise we start with a sector of all zeros. */
      //     if (sector_ofs > 0 || chunk_size < sector_left) 
      //       block_read (fs_device, sector_idx, bounce);
      //     else
      //       memset (bounce, 0, BLOCK_SECTOR_SIZE);
      //     memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
      //     block_write (fs_device, sector_idx, bounce);
      //   }

      cache_write(sector_idx, buffer + bytes_written, sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  //free (bounce);
  //printf("final functie: %d %d %d \n", bytes_written, inode->data.length, size);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

bool
inode_is_dir (const struct inode *inode)
{
  return inode->data.is_dir;
}

int
inode_get_open_cnt (const struct inode *inode)
{
  return inode->open_cnt;
}

void inode_lock_acquire(const struct inode *inode)
{
  lock_acquire(&((struct inode *)inode)->inode_lock);
}

void inode_lock_release (const struct inode *inode)
{
  lock_release(&((struct inode *) inode)->inode_lock);
}

block_sector_t
inode_get_dir_parent (const struct inode *inode) {
  return inode->data.dir_parent;
}

bool
inode_set_dir_parent (block_sector_t parent, block_sector_t child) {
  struct inode* inode = inode_open(child);

  if (!inode)
    return false;

  inode->data.dir_parent = parent;
  inode_close(inode);
  return true;
}