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
#include "filesys/directory.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "devices/input.h"

static void syscall_handler (struct intr_frame *);
static void validate(char * args, int size);
static void validate_char(char * args);
struct file_ * get_file(int fd);
/* Lock for synchronization */
struct lock synch;

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
  //printf("System call number: %d\n", args[0]);

  	validate((char *)args, 0);
  if (args[0] == SYS_EXIT) {
  	validate((char *)args, 1 * sizeof(uint32_t));
  	struct thread *t = thread_current();
  	t->self->status = args[1];
    f->eax = args[1];
    thread_exit();
  }
  else if (args[0] == SYS_WRITE)
  {
  	validate((char *)args[2], args[3]);
  	validate((char *)args, 3 * sizeof(uint32_t));
  	if(args[1] == 1)
  	{
  		putbuf((char *)args[2], args[3]);
  		f->eax = args[3];
  	}
  	else
  	{
  		struct file_* file_ = get_file(args[1]);
  		if(file_ && !file_isdir(file_->file))
  			f->eax = file_write (file_->file, (void *) args[2], args[3]);
  		else
  			f->eax = -1;
  	}
  }
  else if (args[0] == SYS_PRACTICE)
  {
  	validate((char *)args, 1 * sizeof(uint32_t));
  	f->eax = args[1] + 1;
  }
  else if (args[0] == SYS_HALT)
  {
  	shutdown_power_off();
  }
  else if (args[0] == SYS_EXEC)
  {
  	validate_char((char *)args[1]);
  	validate((char *)args, 1 * sizeof(uint32_t));
  	f->eax = process_execute((char *)args[1]);
  }
  else if (args[0] == SYS_WAIT)
  {
  	validate((char *)args, 1 * sizeof(uint32_t));
  	f->eax = process_wait(args[1]);
  }
  else if (args[0] == SYS_REMOVE)
  {
    validate_char((char *)args[1]);
    validate((char *)args, 1 * sizeof(uint32_t));
    lock_acquire (&synch);
    f->eax = filesys_remove((char *)args[1]);
  	lock_release (&synch);
  }
  else if (args[0] == SYS_CREATE)
  {
  	validate_char((char *)args[1]);
  	validate((char *)args, 2 * sizeof(uint32_t));
  	lock_acquire (&synch);
  	f->eax = filesys_create((char *)args[1], args[2], false);
  	lock_release (&synch);
  }
  else if (args[0] == SYS_OPEN)
  {
	validate_char((char *)args[1]);
  	validate((char *)args, 1 * sizeof(uint32_t));

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
  	validate((char *)args, 1 * sizeof(uint32_t));
  	struct file_ *file_ = get_file(args[1]);
	if(file_){
	  	file_close(file_->file);
	  	list_remove(&file_->_file_elem);
	  	free(file_);
	}
  }
  else if (args[0] == SYS_READ)
  {
  	validate((char *)args[2], args[3]);
  	validate((char *)args, 3 * sizeof(uint32_t));
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
  	validate((char *)args, 1 * sizeof(uint32_t));
  	struct file_* file_ = get_file(args[1]);
  	if(file_)
  		f->eax = file_length (file_->file);
  	else
  		f->eax = -1;
  }
  else if (args[0] == SYS_SEEK)
  {
  	validate((char *)args, 2 * sizeof(uint32_t));
  	struct file_* file_ = get_file(args[1]);
  	if(file_)
  		file_seek (file_->file, args[2]);
  }
  else if (args[0] == SYS_TELL)
  {
  	validate((char *)args, 1 * sizeof(uint32_t));
  	struct file_* file_ = get_file(args[1]);
  	if(file_)
  		f->eax = file_tell (file_->file);
  	else
  		f->eax = -1;
  }
  // filesys
  else if(args[0] == SYS_MKDIR)
  {
    f->eax = filesys_create((char *) args[1], 0, true);
  }
  else if(args[0] == SYS_CHDIR)
  {
    f->eax = filesys_chdir((char *) args[1]);
  }
  else if(args[0] == SYS_READDIR)
  {
    struct file_* file_ = get_file(args[1]);
    if(file_)
      f->eax = dir_readdir ((struct dir *)file_->file, (char *) args[2]);
    else
      f->eax = -1;
  }
  else if(args[0] == SYS_ISDIR)
  {
    struct file_* file_ = get_file(args[1]);
    if(file_)
      f->eax = file_isdir (file_->file);
    else
      f->eax = -1;
  }
  else if(args[0] == SYS_INUMBER)
  {
    struct file_* file_ = get_file(args[1]);
    if(file_)
      f->eax = file_get_inumber (file_->file);
    else
      f->eax = -1;
  }
}

void
validate_char(char * args)
{
	validate(args, 0);
	int size = strlen(args) + 1; 
	if(!is_user_vaddr(args + size) || pagedir_get_page(thread_current()->pagedir, args + size) == NULL)
		thread_exit();
}

void
validate(char * args, int size)
{
	if(args + size == NULL || !is_user_vaddr(args + size) || pagedir_get_page(thread_current()->pagedir, args + size) == NULL)
		thread_exit();
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