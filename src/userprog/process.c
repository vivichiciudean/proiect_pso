#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#ifdef VM
#include "vm/frame.h"
#endif

//Added by Alex
#ifdef VM
static unsigned page_hash (const struct hash_elem *p_, void *aux UNUSED);
static bool page_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);
struct supl_pte* page_lookup (const void *address);
#endif

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  tid_t tid;

//Cristi
  char *name;
  char *save_ptr;
  name = malloc(strlen(file_name)+1);
  strlcpy (name, file_name, strlen(file_name)+1);
  name = strtok_r (name," ",&save_ptr);
//Cristi

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (name, PRI_DEFAULT, start_process, fn_copy);
  free(name);
  if (tid == TID_ERROR)
    palloc_free_page (fn_copy); 
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;
//Added by Alex
#ifdef VM
  hash_init(&thread_current()->supl_pt, page_hash, page_less, NULL);
  //printf("Suplimental page (hash) table initialized\n");

  thread_current()->exec_file = NULL;
#endif

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);

  //Sema up for the parent waiting for this new process to be loaded
  //Alex
  if(thread_current()->parent != NULL){
    struct child_process *child = get_child(thread_current()->tid, &thread_current()->parent->child_processes);
    child->is_loaded = success;
  }
  sema_up(&thread_current()->sema_proc_exec);
  
  //Alex
  /* If load failed, quit. */
  palloc_free_page (file_name);
  if (!success) 
    thread_exit ();

//added by Alex
#ifdef VM
  thread_current()->open_files = malloc(MAX_OPEN_FILES*sizeof(struct file*));
  if (thread_current()->open_files == NULL) {
	  success = 0;
	  thread_exit();
  } else {
	  int fd;
	  for (fd=0; fd<MAX_OPEN_FILES; fd++)
		  thread_current()->open_files[fd] = NULL;
  }
#endif

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid UNUSED) 
{
  // UTCN
  
  // we should first of all get the child_process
  // printf("Getting child with pid %d from %s\n", child_tid, thread_current()->name);
  struct child_process * child =get_child(child_tid, &thread_current()->child_processes);
   
  // account for case 1
  if( child == NULL ){
    // printf("Not my child\n");
    return -1;
  }
  // account for case 2
  if (! child -> is_awaited ){
    child -> is_awaited = true;
    if(child -> cur_status == CHILD_ALIVE ){
      // block the current process ( make it wait until the child calls sema_up )
      // printf("sema down %s waiting for %s\n", thread_current()->name, child->child_proc_thread->name);
      sema_down ( &(child->child_proc_thread->sema_proc_wait) );
    }
    // if everything went well return the exit status
    return child -> exit_status ;
  }
  // printf("Already awaited\n");
  // error
  return -1;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  //Alex
  // if this thread has a parent
  if(thread_current()->parent != NULL)
  {
      // get this thread as a child
      struct child_process *child = get_child(thread_current() -> tid, &thread_current()->parent->child_processes);
      // if exit was not called on this process, aka it is still alive set the proper values
      list_remove(&child->proc_list_elem);
      if(child -> cur_status == CHILD_ALIVE)
      {
        child -> cur_status = CHILD_KILLED;
        child -> exit_status = -1;
      }
  }
  // printf("sema up %s is done\n", thread_current()->name);
  // wake up the parent waiting for this lock
  sema_up(&(thread_current()->sema_proc_wait));
  //Alex

  //Added by Alex
#ifdef VM
  if (cur->exec_file != NULL)
	  file_close (cur->exec_file);
#endif

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;
  
  //Cristi
  char *fn_copy;
  char *save_ptr;
  //Cristi

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();
  
  //Cristi
  int name_length = strlen (file_name)+1;
    fn_copy = malloc (name_length);
    strlcpy(fn_copy, file_name, name_length);
    fn_copy = strtok_r (fn_copy, " ", &save_ptr);
  //Cristi

  /* Open executable file. */
  file = filesys_open (fn_copy);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

//Added by Alex
#ifdef VM
  struct hash_iterator hash_iter;
  //printf("[load] The supplemental page table contents:\n");
  hash_first (&hash_iter, &thread_current()->supl_pt);
  while (hash_next (&hash_iter))
  {
	  struct supl_pte *spte = hash_entry (hash_cur (&hash_iter), struct supl_pte, he);
	  //printf("[load] spte->virt_page_addr=0x%x, spte->virt_page_no=%d, spte->ofs=%d, spte->page_read_bytes=%d, spte->page_zero_bytes=%d, spte->writable=%d\n", spte->virt_page_addr, spte->virt_page_no, spte->ofs, spte->page_read_bytes, spte->page_zero_bytes, spte->writable);
  }

  //printf("\n\n[load] Setup the STACK segment\n");
#endif

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;
    
    //Cristi
    //setup stack arguments
    char *token = fn_copy;
    void *stack_pointer = *esp;
    int argc = 0;

    //parsing
    while (token != NULL)
    {
        int arg_length = strlen(token);
        stack_pointer -= (arg_length + 1);
        memcpy(stack_pointer, token, arg_length);
        argc++;
        token = strtok_r(NULL, " ", &save_ptr);
    }

    char *args_pointer = (char *) stack_pointer;

    //adding null
    stack_pointer -= sizeof(char *);
    memset(stack_pointer, 0, 1);

    //arguments address
    for(int i=0; i<argc;i++)
    {
        stack_pointer -= sizeof(char *);
        *((char **) stack_pointer) = args_pointer;
        args_pointer += (strlen(args_pointer) + 1);
    }

    //adding argv array address
    stack_pointer -= sizeof(char **);
    *((char ***) stack_pointer) = stack_pointer + sizeof(char **);

    //adding argc
    stack_pointer -= sizeof(int);
    *(int *) (stack_pointer) = argc;

    //adding fake 0 return address
    stack_pointer -= sizeof(int*);
    *(int *) (stack_pointer) = 0;
    *esp = stack_pointer;

    free(fn_copy);
    //Cristi
    

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;
  
  file_deny_write(file);

 done:
  /* We arrive here whether the load is successful or not. */
//Added by Alex
#ifdef VM
 if (success == true)
	thread_current()->exec_file = file;
#else
	file_close (file);
#endif
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  struct supl_pte *spte;

  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  off_t crt_ofs = ofs;

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

#ifdef VM
      spte = malloc(sizeof(struct supl_pte));
      spte->virt_page_addr = upage;
      spte->virt_page_no = ((unsigned int) upage)/PGSIZE;
      spte->ofs = crt_ofs;
      spte->page_read_bytes = page_read_bytes;
      spte->page_zero_bytes = page_zero_bytes;
      spte->writable = writable;
      spte->swapped_out = false;
      hash_insert (&thread_current()->supl_pt, &spte->he);

      //printf("[load_segment] spte->virt_page_addr=0x%x, spte->virt_page_no=%d, spte->ofs=%d, spte->page_read_bytes=%d, spte->page_zero_bytes=%d, spte->writable=%d\n", spte->virt_page_addr, spte->virt_page_no, spte->ofs, spte->page_read_bytes, spte->page_zero_bytes, spte->writable);

      crt_ofs += page_read_bytes;
#else
      /* Get a page of memory. */
#ifdef VM
      uint8_t *kpage = frame_alloc(PAL_USER, spte);
#else
      uint8_t *kpage = palloc_get_page(PAL_USER);
#endif

      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable))
        {
          palloc_free_page (kpage);
          return false; 
        }
#endif

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;

//Added by Alex and Vivi
#ifdef VM
  struct supl_pte *spte;
  spte = malloc(sizeof(struct supl_pte));
  spte->virt_page_addr = ((uint8_t *) PHYS_BASE) - PGSIZE;
  spte->virt_page_no = ((unsigned int) spte->virt_page_addr)/PGSIZE;
  spte->ofs = -1;
  spte->page_read_bytes = 0;
  spte->page_zero_bytes = 0;
  spte->writable = true;
  spte->swapped_out = false;
  hash_insert (&thread_current()->supl_pt, &spte->he);
  //printf("[setup_stack] spte->virt_page_addr=0x%x, spte->virt_page_no=%d, spte->ofs=%d, spte->page_read_bytes=%d, spte->page_zero_bytes=%d, spte->writable=%d\n", spte->virt_page_addr, spte->virt_page_no, spte->ofs, spte->page_read_bytes, spte->page_zero_bytes, spte->writable);

  kpage = frame_alloc (PAL_USER | PAL_ZERO, spte);
#else
  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
#endif
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success) {
	// UTCN
        //original
        *esp = PHYS_BASE;
      } else
#ifdef VM
    	frame_free(kpage);
#else
      palloc_free_page (kpage);
#endif
    }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

#ifdef VM
static unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED)
{
	const struct supl_pte *p = hash_entry (p_, struct supl_pte, he);
	return hash_int (p->virt_page_no);
}

/* Returns true if page a precedes page b. */
static bool
page_less (const struct hash_elem *a_, const struct hash_elem *b_,
void *aux UNUSED)
{
	const struct supl_pte *a = hash_entry (a_, struct supl_pte, he);
	const struct supl_pte *b = hash_entry (b_, struct supl_pte, he);

	return a->virt_page_no < b->virt_page_no;
}


/*
 * Returns the supplemental page table entry, containing the given virtual address,
 * or a null pointer if no such page exists.
*/
struct supl_pte *
page_lookup (const void *pg_no)
{
	struct supl_pte spte;
	struct hash_elem *e;

	spte.virt_page_no = pg_no;
	e = hash_find (&thread_current()->supl_pt, &spte.he);

	return e != NULL ? hash_entry (e, struct supl_pte, he) : NULL;
}

bool load_page_for_address(uint8_t *upage)
{
	struct supl_pte *spte;
	uint32_t pg_no = ((uint32_t) upage) / PGSIZE;

	//printf("[load_page] Looking for page no %d (address 0x%x) in the supplemental hash table\n", pg_no, upage);

	spte = page_lookup(pg_no);

	if (spte == NULL) {
		//printf("[load_page] Needed page was not found in the supplemental page table\n");
		return false;
	}

	//printf("[load_page] Needed page was found in the supplemental page table\n");

#ifdef VM
	if(spte->swapped_out)
	{
	//	printf("[load_page] Page for 0x%X was swapped out\n", upage);
		void *kpage =  frame_swap_in(spte);
		if(NULL == kpage)
		{
			return false;
		}
		if (!install_page (spte->virt_page_addr, kpage, spte->writable))
		{
			frame_free(kpage);
			return false;
		}
		//printf("[load_page] Swapped page back in\n");
		return true;
	}
	else
	{
#endif
		// lazy load
		return lazy_loading_page_for_address(spte, upage);
#ifdef VM
	}
#endif
}

/* Allocate and load a new page from the executable */
bool lazy_loading_page_for_address(	struct supl_pte *spte, void *upage)
{
	struct thread *crt = thread_current();

    /* Get a page of memory. */
#ifdef VM
	uint8_t *kpage = frame_alloc (PAL_USER, spte);
#else
     uint8_t *kpage = palloc_get_page (PAL_USER);
#endif
     if (kpage == NULL)
       return false;

     // Establish the file offset the page must be read from
     file_seek (crt->exec_file, spte->ofs);

     // Added by Adrian Colesa - VM
     //printf("\n[lazy_load_segment] Virtual page %d: from offset %d in the executable file read %d bytes and zero the rest %d bytes\n", ((unsigned int) upage)/PGSIZE, file_tell(crt->exec_file), spte->page_read_bytes, spte->page_zero_bytes);

     /* Load this page. */
     if (file_read (crt->exec_file, kpage, spte->page_read_bytes) != (int) spte->page_read_bytes)
       {
         palloc_free_page (kpage);
         return false;
       }
     memset (kpage + spte->page_read_bytes, 0, spte->page_zero_bytes);

     // Added by Adrian Colesa - Userprog + VM
     //printf("[load_segment] The process virtual page %d starting at virtual address 0x%x will be mapped onto the kernel virtual page %d (physical frame %d) starting at kernel virtual address 0x%x (physical address 0x%x)\n", ((unsigned int) upage)/PGSIZE, upage, (unsigned int)kpage/PGSIZE, ((unsigned int)vtop(kpage))/PGSIZE, kpage, vtop(kpage));
     //printf("[lazy_load_segment] Virtual page %d (vaddr=0x%x): mapped onto the kernel virtual page %d (physical frame %d)\n", ((unsigned int) upage)/PGSIZE, upage, (unsigned int)kpage/PGSIZE, ((unsigned int)vtop(kpage))/PGSIZE);

     //frame_evict(kpage);

     /* Add the page to the process's address space. */
     if (!install_page (spte->virt_page_addr, kpage, spte->writable))
       {
#ifdef VM
         frame_free(kpage);
#else
         palloc_free_page (kpage);
#endif
         return false;
       }

       return true;
}

#endif
