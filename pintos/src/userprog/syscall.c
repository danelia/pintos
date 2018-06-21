#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "devices/shutdown.h"
#include "userprog/process.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include <string.h>
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "devices/input.h"
#include "syscall.h"
#ifdef VM
#include "vm/page.h"
#include "vm/frame.h"
#endif

static void syscall_handler (struct intr_frame *);
static bool validate_ptr(char * args,void* esp);
static void validate(char * args, int size,void* esp);
static void validate_char(char * args,void* esp);
static bool validate_page(char* fault_addr, void* esp);
static void validate_buffer(char* addr, int size, bool write);
static int mmap(int fd, void * addr);
struct file_ * get_file(int fd);


void
syscall_init (void)
{
	intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
	lock_init(&synch);
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
	uint32_t* args = ((uint32_t*) f->esp);
	validate((char *)args, sizeof(uint32_t),f->esp);
	if (args[0] == SYS_EXIT) {
		validate((char *)args, 1 * sizeof(uint32_t),f->esp);
		struct thread *t = thread_current();
		t->self->status = args[1];
		f->eax = args[1];
		thread_exit();
	}
	else if (args[0] == SYS_WRITE)
	{
		validate((char *)args[2], args[3],f->esp);
		validate((char *)args, 3 * sizeof(uint32_t),f->esp);
		if(args[1] == 1)
		{
			putbuf((char *)args[2], args[3]);
			f->eax = args[3];
		}
		else
		{
			struct file_* file_ = get_file(args[1]);
			if(file_)
				f->eax = file_write (file_->file, (void *) args[2], args[3]);
			else
				f->eax = -1;
		}
	}
	else if (args[0] == SYS_PRACTICE)
	{
		validate((char *)args, 1 * sizeof(uint32_t),f->esp);
		f->eax = args[1] + 1;
	}
	else if (args[0] == SYS_HALT)
	{
		shutdown_power_off();
	}
	else if (args[0] == SYS_EXEC)
	{
		validate_char((char *)args[1], f->esp);
		validate((char *)args, 1 * sizeof(uint32_t),f->esp);
		f->eax = process_execute((char *)args[1]);
	}
	else if (args[0] == SYS_WAIT)
	{
		validate((char *)args, 1 * sizeof(uint32_t),f->esp);
		f->eax = process_wait(args[1]);
	}
	else if (args[0] == SYS_REMOVE)
	{
		validate_char((char *)args[1],f->esp);
		validate((char *)args, 1 * sizeof(uint32_t),f->esp);
		lock_acquire (&synch);
		f->eax = filesys_remove((char *)args[1]);
		lock_release (&synch);
	}
	else if (args[0] == SYS_CREATE)
	{
		validate_char((char *)args[1], f->esp);
		validate((char *)args, 2 * sizeof(uint32_t), f->esp);
		lock_acquire (&synch);
		f->eax = filesys_create((char *)args[1], args[2]);
		lock_release (&synch);
	}
	else if (args[0] == SYS_OPEN)
	{
		validate_char((char *)args[1],f->esp);
		validate((char *)args, 1 * sizeof(uint32_t),f->esp);

		lock_acquire (&synch);
		struct file * file = filesys_open((char *)args[1]);
		lock_release (&synch);
		if(!file)
			f->eax = -1;
		else{

			struct thread *curr = thread_current();
			struct file_ *file_ = malloc(sizeof(struct file_));

			file_->file = file;
			file_->fd = curr->fd_count++;
			list_push_back(&curr->file_list, &file_->_file_elem);
			f->eax = file_->fd;
	}
	}
	else if (args[0] == SYS_CLOSE)
	{
		validate((char *)args, 1 * sizeof(uint32_t),f->esp);
		struct file_ *file_ = get_file(args[1]);
	if(file_){
			file_close(file_->file);
			list_remove(&file_->_file_elem);
			free(file_);
	}
	}
	else if (args[0] == SYS_READ)
	{
		validate((char *)args[2], args[3],f->esp);
		validate((char *)args, 3 * sizeof(uint32_t),f->esp);
		validate_buffer((char *)args[2],  args[3], true);
		if(args[1] == 0){
			uint8_t *buff = (uint8_t *) args[2];
			size_t res = 0;
			for(; res < args[3];){
				buff[res] = input_getc();
				if(buff[res++] == '\n')
					break;
			}
			f->eax = res;
		}
		else
		{
			struct file_* file_ = get_file(args[1]);
			if(file_)
				f->eax = file_read (file_->file, (void *) args[2], args[3]);
			else
				f->eax = -1;
		}
	}
	else if (args[0] == SYS_FILESIZE)
	{
		validate((char *)args, 1 * sizeof(uint32_t),f->esp);
		struct file_* file_ = get_file(args[1]);
		if(file_)
			f->eax = file_length (file_->file);
		else
			f->eax = -1;
	}
	else if (args[0] == SYS_SEEK)
	{
		validate((char *)args, 2 * sizeof(uint32_t),f->esp);
		struct file_* file_ = get_file(args[1]);
		if(file_)
			file_seek (file_->file, args[2]);
	}
	else if (args[0] == SYS_TELL)
	{
		validate((char *)args, 1 * sizeof(uint32_t),f->esp);
		struct file_* file_ = get_file(args[1]);
		if(file_)
			f->eax = file_tell (file_->file);
		else
			f->eax = -1;
	}
#ifdef VM
  else if (args[0] == SYS_MMAP)
  {
    int fd = (int) args[1];
    void * addr = (void *) args[2];
    f->eax = mmap(fd, addr);
  }
  else if(args[0] == SYS_MUNMAP)
  {
    munmap(args[1]);
  }
#endif
}

static int
mmap(int fd, void * addr)
{
  if(fd < 2 || addr == NULL || !is_user_vaddr(addr) 
    || (void *)addr <= (void *) 0x08048000 || ((uint32_t)addr % PGSIZE) != 0)
    return -1;

  struct file_ * file_ = get_file(fd);
  if(!file_)
    return -1;
  
  struct thread * t = thread_current();
  struct file * file = file_reopen(file_->file);
  if(file)
  {
    int len = file_length (file);


    int i;
    for(i = 0; i < len / PGSIZE + 1; i++)
      if(get_page(addr + PGSIZE * i) != NULL) 
        return -1;

    uint32_t offs = 0;
    while(len > 0)
    {
      uint32_t read_bytes = (len < PGSIZE) ? len : PGSIZE;
      uint32_t zero_bytes = PGSIZE - read_bytes;

      if(!mmap_into_page_tables(file, offs, addr,
        read_bytes, zero_bytes, true))
      {
        munmap(t->map_id_count);
        return -1;
      }

      len -= read_bytes;
      offs += read_bytes;
      addr += PGSIZE;
    }

    t->map_id_count++;
    return t->map_id_count - 1;
  } 
  
  return -1;
}

void 
munmap(int mmap_id)
{
  if(mmap_id < 0)
    return;

  struct thread * t = thread_current();
  struct list_elem *e = list_begin (&t->mmap_list);
  //int closed = 0;
  while (e != list_end (&t->mmap_list))
    {
      struct mmap_ *mmap = list_entry (e, struct mmap_, elem);
      if(mmap->map_id == mmap_id)
      {
        struct supp_page_entry *p = mmap->p;
        file_seek(p->file, 0);

        if ( pagedir_is_dirty(t->pagedir, p->upage)) 
          file_write_at(p->file, p->upage, p->read_bytes, p->offs);

        /*if(!closed)
        {
          if(p->file)
            file_close(p->file);
          closed = 1;
        }*/

        hash_delete (&t->supp_page_table, &p->elem);

        uint8_t * kpage = pagedir_get_page(t->pagedir, p->upage);
        pagedir_clear_page (t->pagedir, p->upage);
        free_frame(kpage);
        free (p);

        e = list_remove(&mmap->elem);
        free(mmap);
        continue; 
      }

      e = list_next (e);
    }
    thread_current() -> map_id_count--;
}

static bool 
validate_page(char* fault_addr, void* esp)
{
	uint8_t * rounded = pg_round_down(fault_addr);
	struct supp_page_entry stub;
	stub.upage = rounded;

	if((void *)fault_addr <= (void *) 0x08048000)
		return false;

	struct hash_elem* e = hash_find(&thread_current()->supp_page_table, &stub.elem);

	if(e == NULL)
	{
		if (((void *)fault_addr >= esp - 32) && stack_grow(fault_addr))
			return true;

		goto fail;
	}

		struct supp_page_entry* p = hash_entry(e,struct supp_page_entry, elem);

		enum palloc_flags flags= PAL_USER;
		if (p->zero_bytes == PGSIZE)
			flags |= PAL_ZERO;

		void * kpage =alloc_frame(flags,p);
		if(kpage == NULL)
			goto fail;

		lock_acquire (&synch);
		if (file_read_at (p->file, kpage, p->read_bytes, p->offs) != (int) p->read_bytes)
		{
			free_frame(kpage);
			lock_release(&synch);
			goto fail;
		}
		
		lock_release(&synch);
		memset (kpage + p -> read_bytes, 0, p -> zero_bytes);

		struct thread *t = thread_current();
		if(!(pagedir_get_page (t->pagedir, p->upage) == NULL
					&& pagedir_set_page (t->pagedir, p->upage, kpage, p->writable)))
		{
			free_frame(kpage);
			goto fail;
		}

		return true;
		fail:
			return false;
}

static void 
validate_buffer(char* addr, int size, bool write)
{
	int i =0;
	for(; i < size; i++)
  {
		uint8_t * rounded = pg_round_down(addr + i);
		struct supp_page_entry stub;
		stub.upage = rounded;
		struct hash_elem* e = hash_find(&thread_current() -> supp_page_table, &stub.elem);
		if (e == NULL)
			thread_exit();

		struct supp_page_entry* p = hash_entry(e,struct supp_page_entry, elem);
		if(write && !p->writable)
			thread_exit();
	}
}

static
void validate_char(char * args,void* esp)
{
	validate(args, 0, esp);
	int size = strlen(args) + 1; 
	if(!is_user_vaddr(args + size) || pagedir_get_page(thread_current()->pagedir, args + size) == NULL)
		thread_exit();
}

static bool 
validate_ptr(char * uaddr, void* esp)
{
	if(uaddr == NULL || !is_user_vaddr(uaddr))
		return false;
  if(pagedir_get_page(thread_current()->pagedir, uaddr) == NULL)
#ifdef VM
	  return validate_page(uaddr,esp);
#else
	 	return false;
#endif
	 return true;
}

void
validate(char * args, int size, void* esp)
{
	if(!validate_ptr(args, esp) || !validate_ptr(args + size, esp))
		thread_exit();
#ifndef VM
	 if(pagedir_get_page(thread_current()->pagedir, args) == NULL || 
	 	     (pagedir_get_page(thread_current()->pagedir, args + size) == NULL))
	 	thread_exit();
#endif
}
	 


struct file_ *
get_file(int fd)
{
	struct list_elem *e;
	struct thread * t = thread_current();

	for (e = list_begin (&t->file_list); e != list_end (&t->file_list);
			 e = list_next (e))
		{
			struct file_ *f = list_entry (e, struct file_, _file_elem);
			if(f->fd == fd)
				return f;
		}

	return NULL;
}