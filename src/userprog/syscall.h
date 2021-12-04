#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <list.h>
#include <stdbool.h>
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/synch.h"
#include "threads/vaddr.h"


#define MIN_FILENAME_LENGTH 1
#define MAX_FILENAME_LENGTH 14

struct lock file_lock;       //pentru sincronizarea fisierelor

struct file_struct
{
    int fd;                        //file descriptorul fisierului
    struct file *file;           //fisierul in sine
    struct list_elem element;      //pentru a-l putea pune in lista
};

// struct child_process* get_child(tid_t tid, struct list *child_process_list);

void syscall_init (void);

#endif /* userprog/syscall.h */
