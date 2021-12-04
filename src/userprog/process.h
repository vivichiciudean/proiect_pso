#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#ifdef VM
#include "vm/spte.h"
#endif

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

bool load_page_for_address(uint8_t *upage);
bool lazy_loading_page_for_address(	struct supl_pte *spte, void *upage);

#endif /* userprog/process.h */
