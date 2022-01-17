#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
//#include "filesys/buffer-cache.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/*
  Return the directory associated with the path specified as parameter
*/
struct dir*
dir_from_path(const char* path)
{
  uint32_t length = strlen(path);
  char path_copy[length + 1];
  memcpy(path_copy, path, length + 1);
  path_copy[length] = 0;

  struct dir* dir; //next, we chose the start dir
  if(path_copy[0] == '/' || !thread_current()->current_dir) //path absolut
    dir = dir_open_root();
  else
    dir = dir_reopen(thread_current()->current_dir); //path relativ + do a copy!
  
  char *cur, *ptr, *prev;
  prev = strtok_r(path_copy, "/", &ptr);
  cur = strtok_r(NULL, "/", &ptr);
  while(cur != NULL)
  {
    struct inode* inode;
    if(strcmp(prev, ".") == 0) 
      continue;
    
    if(strcmp(prev, "..") == 0) {
      inode = get_dir_parent_inode(dir);
      if(inode == NULL) 
        return NULL;
    }
    else if(dir_lookup(dir, prev, &inode) == false) { //if it doesn`t exist
      //inode_close(inode);
      return NULL;
    }

    if(inode_is_dir(inode)) {
      dir_close(dir);
      dir = dir_open(inode); // actualizarea directorului in care suntem
    }
    else
      inode_close(inode); // eliberezi resursele fara a mai deschide altceva

    prev = cur;
    cur = strtok_r(NULL, "/", &ptr);
  }

  return dir;
}

/*
  Take the last name before the /
*/
char*
file_name_from_path(const char* path)
{
  int length = strlen(path);  
  char path_copy[length + 1];
  memcpy(path_copy, path, length + 1);
  path_copy[length] = 0;

  char *cur, *ptr, *prev = "";
  cur = strtok_r(path_copy, "/", &ptr);
  while (cur != NULL) { //iterate the path, until last name
    prev = cur;
    cur = strtok_r(NULL, "/", &ptr);
  }
  char* name = malloc(strlen(prev) + 1);
  memcpy(name, prev, strlen(prev) + 1);
  name[strlen(prev)] = 0;
  return name;
}




/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");


  cache_init ();   //Added
  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();

}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  cache_writeback(true);
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size, bool is_dir) 
{
  block_sector_t inode_sector = 0;
  struct dir *dir = dir_from_path(name);
  char* file_name = file_name_from_path(name);

  //printf("Din dir add: %s %s\n", file_name, name);
  
  bool success = false;
  if (strcmp(file_name, ".") != 0 && strcmp(file_name, "..") != 0) {
    success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, is_dir)
                  && dir_add (dir, file_name, inode_sector));
  }

  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);
  free(file_name);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  struct dir* dir = dir_from_path(name);
  char* file_name = file_name_from_path(name);
  struct inode *inode = NULL;

  if(dir != NULL) {
    if (dir_is_root(dir) && strlen(file_name) == 0){
  	  return (struct file *) dir; //support the filesys open for directories
    }
    else if (strcmp(file_name, ".") == 0) {
  	  free(file_name);
  	  return (struct file *) dir; //support the filesys open for directories
  	}
    else if(strcmp(file_name, "..") == 0) {
      inode = get_dir_parent_inode(dir);
  	  if(!inode) {
  	    free(file_name);
  	    return NULL;
  	  }
  	}
    else
  	  dir_lookup (dir, name, &inode);
  }
  dir_close (dir);
  free(file_name);

  if (!inode)
    return NULL;

  if (inode_is_dir(inode))
    return (struct file *) dir_open(inode); //support the filesys open for directories

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{

  struct dir* dir = dir_from_path(name);
  char* file_name = file_name_from_path(name);
  
  bool success = dir != NULL && dir_remove (dir, file_name);
  dir_close (dir); 
  free(file_name);

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

/*
  Used to change the dir of a file
*/
bool
filesys_chdir(const char* new_path) {
  struct dir *dir = dir_from_path(new_path);
  char* file_name = file_name_from_path(new_path);
  struct inode *inode = NULL;

  if(dir == NULL) //if the dir doesn`t exists
  {
    free(file_name);
    return false;
  }

  if(strcmp(file_name, ".") == 0) { //no changes needed
    thread_current()->current_dir = dir;
    free(file_name);
    return true;
  }

  if(strlen(file_name) == 0 && dir_is_root(dir)) { //change the dir to root dir
    thread_current()->current_dir = dir;
    free(file_name);
    return true;
  }

  if(strcmp(file_name, "..") == 0) { //go to parent
    inode = get_dir_parent_inode(dir);
    if(inode == NULL)
    {
      free(file_name);
      return false;
    }
  }else{
    dir_lookup(dir, file_name, &inode);
  }

  dir_close(dir); //inchidem inode-ul dir-ului deschis inainte

  dir = dir_open(inode); //si deschidem noul dir

  if(dir == NULL) 
  {
    free(file_name);
    return false;
  }

  dir_close(thread_current()->current_dir);
  thread_current()->current_dir = dir;
  free(file_name);
  return true;
}