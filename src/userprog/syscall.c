#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
// #include "threads/thread.h"
#include "threads/malloc.h"
#include "process.h"


static void syscall_handler (struct intr_frame *);

struct child_process* get_child(tid_t tid, struct list *child_process_list);

struct file_struct* get_file_struct_for_fd(int fd){
  struct list_elem *e;
  for (e = list_begin (&thread_current()->file_struct_list); e != list_end (&thread_current()->file_struct_list); e = list_next (e)) {
    struct file_struct *fd_elem = list_entry (e, struct file_struct, element);
    if(fd_elem->fd == fd){
      return fd_elem;
    }
  }
  return NULL;
}

void is_valid_addr (const void *address)
{
    if (!is_user_vaddr(address) || pagedir_get_page(thread_current()->pagedir, address) == NULL)
    {
        exit(-1);
    }
}

int allocate_fd(){
  struct list_elem *e;
  if(list_empty(&thread_current()->file_struct_list)){
    return 2;  
  }

  struct file_struct *fd_elem = list_entry (list_back(&thread_current()->file_struct_list), struct file_struct, element);
  return fd_elem->fd+1;
}

bool 
is_valid_ptr(const void *ptr) 
{
  if (ptr == NULL || !is_user_vaddr(ptr) || pagedir_get_page(thread_current()->pagedir, ptr) == NULL)
    return false;

  //!= NULL, user virtual memory address and mapped memory
  return true;
}

bool is_full_valid_ptr(const void *ptr, unsigned size){
  //practic parcurg fiecare octet al pointerului de dimensiune cunoscuta si verific daca este mapat/valid
  for(int i=0;i<size;i++){
    if(!is_valid_ptr(ptr+i)){
      return false;
    }
  }
  return true;
}

int 
is_valid_filename(const void *file)
{
  //poate ar fi mai bine sa mergi octet cu octet si sa te uiti daca exista 0 pe acel octet, verificand daca e si valid ptr!!!
  //while pana dai de 0
  char* file_name = file;
  bool stop = false;
  int len = 0;
  do{
    if(!is_user_vaddr(file_name+len) || pagedir_get_page(thread_current()->pagedir, file_name+len) == NULL){
      return -1;
    }

    if(*(file_name+len) == '\0'){
      stop=true;
    }else{
      len++;
    }

    if(len>MAX_FILENAME_LENGTH){
      return -2;
    }
  }while(!stop);

  if (len >= MIN_FILENAME_LENGTH && len <= MAX_FILENAME_LENGTH)
    return len;
}

void exit(int status){
  struct thread *cur = thread_current();
  printf ("%s: exit(%d)\n", cur -> name, status);

  // set status of the current child elem
  struct child_process * child = get_child (cur->tid, &cur->parent->child_processes);
  child -> exit_status = status;
  // set the current status of the child
  child -> cur_status = status == -1? CHILD_KILLED : CHILD_EXITED ;
  // actually exit thread
  thread_exit ();
}


bool k_create (const char *file_name, unsigned initial_size){
  bool status=false;

  int value = is_valid_filename(file_name);

  if(value == -2){
    return false;
  }

  if(value <= 0){
    exit(-1);
  }

  lock_acquire(&file_lock);

  status = filesys_create(file_name, initial_size);

  //printf("ok %d", status);

  lock_release(&file_lock);

  return status;
}

bool k_remove (const char *file_name){
  bool status = false;
  if(is_valid_filename(file_name) <= 0){
    return status;
  }

  lock_acquire(&file_lock);
  status = filesys_remove (file_name);
  lock_release(&file_lock);
  return status;
}

int k_open (const char *file_name){
  //aici nu cunosti ce dimensiune are buffer-ul, stii doar unde incepe + nu poti verifica cu strlen ca poate nu exista \0
  if (is_valid_filename(file_name) <= 0)
    return -1;

  //printf("FD: %d ",allocate_fd());

  lock_acquire(&file_lock);

  struct file *file = filesys_open(file_name);
  if (file == NULL){
    lock_release(&file_lock);
    return -1;
  }
  struct file_struct *file_struct = (struct file_struct*)malloc(sizeof(struct file_struct));

  file_struct->file = file;
  file_struct->fd = allocate_fd();

  list_push_back(&thread_current()->file_struct_list, &file_struct->element);

  lock_release(&file_lock);
  return file_struct->fd;
}

int k_filesize (int fd){
  lock_acquire(&file_lock);
  //verificare ca fd-ul e corect?
  //verificare ca fisierul e deschis?
  struct file *file = get_file_struct_for_fd(fd)->file;
  if (!file)
  {
    lock_release(&file_lock);
    return -1;
  }
  int filesize = file_length(file);
  lock_release(&file_lock);
  return filesize;
}


int k_write (int fd, const void *buffer, unsigned size){
  int status = 0;
  if(fd <= 0)
    return status;

  //trebuie verificat size-ul separat?

  //teoretic trebuie validat TOT bufferul
  //merge cu for sau cu paginare
  if (!is_full_valid_ptr(buffer, size)){
    exit(-1);
  }

  lock_acquire(&file_lock);
	if (fd == 1) {
		putbuf(buffer, size);
    lock_release(&file_lock);
    return size;
	}
  
  struct file_struct *file_struct = get_file_struct_for_fd(fd);
  // e posibil sa fi nevoit sa scrii cate o pagina pe rand!
  if (file_struct != NULL){
    status = file_write(file_struct->file, buffer, size);
  }
  lock_release(&file_lock);
  return status;
}


int k_read (int fd, void *buffer, unsigned size){
  int ret = -1;
  if(fd == 1 || fd < 0){
    exit(-1);
  }
  if (!is_full_valid_ptr(buffer, size)){
    exit(-1);
  }
  if(fd == 0)
  {
    //vrem sau nu vrem sincronizare la acest nivel?
    uint8_t *local_buf = (uint8_t *) buffer;
    for (int i = 0;i < size; i++)
    {
      local_buf[i] = input_getc();
    }
    return size;
  }
  
  struct file_struct *file_struct = get_file_struct_for_fd(fd);
  if(file_struct == NULL || buffer == NULL){
    return -1;
  }

  struct file *file = file_struct->file;
  if(file == NULL){
    return -1;
  }
  lock_acquire(&file_lock);
  ret = file_read(file, buffer, size); 
  lock_release(&file_lock);

  return ret;
}

void k_seek (int fd, unsigned pos){
  struct file *file;
  lock_acquire(&file_lock); //nu ar trebui neaparat sincronizarea, nu?
  file = get_file_struct_for_fd(fd)->file;
  if (file == NULL){
    lock_release(&file_lock);
    return;
  }

  file_seek (file, pos);
  lock_release(&file_lock);
  return;
}

unsigned k_tell (int fd){
  lock_acquire(&file_lock);
  struct file_struct *file_struct = get_file_struct_for_fd(fd);
  if(file_struct == NULL){
    lock_release(&file_lock);
    return -1;
  }
  struct file *file = file_struct->file;
  unsigned ret = file_tell(file);
  lock_release(&file_lock);
  return ret;
}

void k_close (int fd){
  struct file_struct *file_struct = get_file_struct_for_fd(fd);
  if(file_struct == NULL){
      return;
  }
  struct file *file = file_struct->file;
  lock_acquire(&file_lock);
  file_close(file);
  //trebuie sa stergi din lista dupa ce inchizi!
  list_remove(&file_struct->element);
  lock_release(&file_lock);
}

void
syscall_init (void) 
{
  lock_init(&file_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void create_args(struct intr_frame *f, int *fd, char** buffer, int *size){
  *fd = ((int*)f->esp)[1];
  *buffer = (char*)((int*)f->esp)[2];
  *size = ((int*)f->esp)[3];
  is_valid_addr((const void*) (void *) ((int*)f->esp)[2]);
  is_valid_addr(((void*) ((int*)f->esp)[2]) + (unsigned) ((int*)f->esp)[3]);
}

static void
syscall_handler (struct intr_frame *f) 
{
  int fd;
  char* buffer;
  int size;
  is_valid_addr(&((int *)f->esp)[0]);
  is_valid_addr(&((int *)f->esp)[1]);
  int syscall = ((int*)f->esp)[0];

  void* mapping_addr;
  int mapping_id;

  char *exec_buffer;
  int exit_status;
  tid_t child_tid;
  char* open_name;
  char* create_name;
  int seek_fd;
  int position;
  int exec_pid;
	switch(syscall){
		case SYS_EXEC: 
      exec_buffer = (char*)((int*)f->esp)[1];
      is_valid_addr((const void*) ((int*)f->esp)[1]);
      exec_pid = exec(exec_buffer);
      // printf("exec %s returned %d\n",exec_buffer, exec_pid);
      f->eax = exec_pid;
      break;
		case SYS_EXIT: 
      exit_status = ((int*)f->esp)[1];
      exit(exit_status);
      break;
		case SYS_WAIT:
      child_tid = ((int*)f->esp)[1];
      f->eax = process_wait(child_tid);
      break;
		case SYS_OPEN: 
      is_valid_addr((const void*) ((int*)f->esp)[1]);
      open_name = (char*)((int*)f->esp)[1];
      f->eax = k_open(open_name);
      break;
		case SYS_READ: 
      create_args(f, &fd, &buffer, &size);
      f->eax = k_read(fd, buffer, size);
			break;
		case SYS_WRITE: 
      create_args(f, &fd, &buffer, &size);
			f->eax = k_write(fd, buffer, size);
			break;
		case SYS_CREATE:
			create_name = (char*)((int*)f->esp)[1];
      size = ((int*)f->esp)[2];
      f->eax = (int)k_create(create_name,size);
      break;
    case SYS_SEEK:
      seek_fd = ((int*)f->esp)[1];
      position = ((int*)f->esp)[2];
      k_seek(seek_fd, position);
      break;
		case SYS_REMOVE: 
      f->eax = k_remove((char*) ((int*)f->esp)[1]);
      break;
		case SYS_HALT: 
      shutdown_power_off();
      break;
		case SYS_TELL: 
      f->eax = k_tell(((int*)f->esp)[1]);
      break;
		case SYS_CLOSE:
      k_close(((int*)f->esp)[1]);
      break;
		case SYS_FILESIZE: 
      f->eax = k_filesize(((int*)f->esp)[1]);
      break;
    // Cristi
    case SYS_MMAP:
			fd = ((int*)f->esp)[1];
			mapping_addr = (void*) ((int*)f->esp)[2];

			// Lazy mapping of the file. Similar to lazy loading the contents of an executable file.
			// Calculate the total number of virtual pages needed to map the entire file.
			// Take care that the last page could not be entirely used, so the trailing bytes should be zeroed and not written back in the file.
			// Keep track for each mapped virtual page the offset in file it must be loaded from.
			// Use supplemental page table to store this information.
			// TO DO

      mapping_id = sys_mmap (fd, mapping_addr);
			f->eax = mapping_id;
			return;
		case SYS_MUNMAP:

			// Remove from the supplemental page table the elements corresponding to the unmapped pages.
			// TO DO
      mapping_id = ((int*)f->esp)[1];

      sys_munmap(mapping_id);

			f->eax = 0;
			return;
    // Cristi
		default: 
      thread_exit ();
      break;
	}
}

int sys_mmap (int fd,void *mapping_addr)
{
  if (mapping_addr == NULL || pg_ofs(mapping_addr) != 0) return -1;
  if (fd <= 1) return -1;

  struct thread *curr = thread_current();

  struct file *f = NULL;

  lock_acquire(&file_lock);
  struct file_struct *file_struct = get_file_struct_for_fd(fd);
  lock_release(&file_lock);

  if(file_struct == NULL) exit(-1);
 
  if(file_struct && file_struct->file) {
    // reopen file so that it doesn't interfere with process itself
    f = file_reopen (file_struct->file);
  } else {
    return -1;
  }

  if(f == NULL) return -1;

  size_t file_size = file_length(f);
  
  if(file_size == 0) 
  {
    exit(-1);
    return -1;
  }

  size_t offset;
  for (offset = 0; offset < file_size; offset += PGSIZE) {
    void *addr = mapping_addr + offset;
    if (spte_lookup(addr) != NULL)
    {
      return -1;
    }
  }

  int map_id;
  if (! list_empty(&curr->mmap_list)) {
    map_id = list_entry(list_back(&curr->mmap_list), struct mmap_struct, elem)->id + 1;
  }
  else map_id = 1;

  for (offset = 0; offset < file_size; offset += PGSIZE) {
    void *addr = mapping_addr + offset;

    size_t read_bytes = (offset + PGSIZE < file_size ? PGSIZE : file_size - offset);
    size_t zero_bytes = PGSIZE - read_bytes;

    struct supl_pte *spte;
    spte = malloc(sizeof(struct supl_pte));
    spte->virt_page_addr = addr;
    spte->virt_page_no = ((unsigned int) addr)/PGSIZE;
    spte->ofs = offset;
    spte->page_read_bytes = read_bytes;
    spte->page_zero_bytes = zero_bytes;
    spte->writable = true;
    spte->swapped_out = false;
    //Cristi
    spte->map_id = map_id;
    spte->frame_addr = NULL;
    spte->dirty = false;
    //Cristi
    struct hash_elem *elem = hash_insert (&thread_current()->supl_pt, &spte->he);

    if (elem != NULL) {
      return -1;
      PANIC("Duplicated");
    }   
  }

  struct mmap_struct *mmap_str = (struct mmap_struct*) malloc(sizeof(struct mmap_struct));
  mmap_str->id = map_id;
  mmap_str->file = f;
  mmap_str->addr = mapping_addr;
  mmap_str->size = file_size;
  list_push_back (&curr->mmap_list, &mmap_str->elem);

  return map_id;

}

struct mmap_struct *
mmap_list_lookup(struct thread *t, int map_id)
{
  ASSERT (t != NULL);

  struct list_elem *e;

  if (! list_empty(&t->mmap_list)) {
    for(e = list_begin(&t->mmap_list);
        e != list_end(&t->mmap_list); e = list_next(e))
    {
      struct mmap_struct *elem = list_entry(e, struct mmap_struct, elem);
      if(elem->id == map_id) {
        return elem;
      }
    }
  }

  return NULL;
}

void sys_munmap(int map_id)
{
  struct thread *curr = thread_current();
  struct mmap_struct *mmap_str = mmap_list_lookup(thread_current(), map_id);

  if(mmap_str == NULL) { 
    exit(-1);
  }

  size_t offset, file_size = mmap_str->size;
  for(offset = 0; offset < file_size; offset += PGSIZE) {
    void *addr = mmap_str->addr + offset;
    size_t no_bytes = (offset + PGSIZE < file_size ? PGSIZE : file_size - offset);
    struct supl_pte * spte = spte_lookup (addr);
    if(spte != NULL) {
      if(spte->swapped_out)
      {
        bool is_dirty =  pagedir_is_dirty(curr->pagedir, spte->virt_page_addr);
        if (is_dirty) {
          void *aux_page = palloc_get_page(0);
          swap_in(spte->swap_idx, aux_page);
          file_write_at (mmap_str->file, aux_page, no_bytes, offset);
        }
      } else {
        bool is_dirty = false;
        is_dirty = is_dirty || pagedir_is_dirty(curr->pagedir, spte->virt_page_addr);
        
        if(is_dirty ) {
          file_write_at (mmap_str->file, spte->virt_page_addr, no_bytes, offset);   
        } else {
          // free swap
          bitmap_set(swap_table, spte->swap_idx, SWAP_FREE);
        }

        if(spte->frame_addr != NULL)
        {
          frame_free(spte->frame_addr);
        }
        pagedir_clear_page (curr->pagedir, spte->virt_page_addr);
      }

    hash_delete(&thread_current()->supl_pt, &spte->he);
    }
  }


  list_remove(& mmap_str->elem);
  file_close(mmap_str->file);
  free(mmap_str);
}

struct child_process *get_child(tid_t pid, struct list *child_process_list){
  struct list_elem* e;
  for (e = list_begin ( child_process_list); e != list_end ( child_process_list); e = list_next (e))
  {
      struct child_process *child = list_entry (e, struct child_process, proc_list_elem);
      if(child -> child_pid == pid)
      {
          return child;
      }
  }
  return NULL;
}

tid_t
exec (const char *cmd_line)
{
    
    tid_t pid = -1;
    // create child process to execute cmd
    pid = process_execute(cmd_line);
    if(pid == TID_ERROR){
      exit(-1);
    }
    // get the created child
    struct child_process *child = get_child(pid, &thread_current()->child_processes);
    // wait this child until load
    sema_down(&child->child_proc_thread->sema_proc_exec);
    // after wake up check if child load successfully
    if(child -> is_loaded)
    {
        return pid;
    }
    return -1;
}

