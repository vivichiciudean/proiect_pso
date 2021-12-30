#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
//Added by Alex
#ifdef VM
#include "vm/spte.h"
#endif

//Cristi
#ifdef VM
struct mmap_struct{
  int id;
  struct list_elem elem;
  struct file* file;
  void *addr;   // user virtual address
  size_t size;  // file size
};
#endif
//Cristi

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

bool load_page_for_address(uint8_t *upage);
bool lazy_loading_page_for_address(	struct supl_pte *spte, void *upage);

#endif /* userprog/process.h */
