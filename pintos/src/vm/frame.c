#include "frame.h"
#include "debug.h"
#include <stdio.h>
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"

struct list frame_table;
struct lock frame_lock;

void 
frame_init(void)
{
	lock_init(&frame_lock);
	list_init(&frame_table);
}
 
uint8_t *
alloc_frame(enum palloc_flags flags, struct supp_page_entry * p)
{
	uint8_t * kpage = palloc_get_page(flags);
	ASSERT(kpage != NULL);
	if(kpage != NULL){	
		struct frame * f = malloc(sizeof(struct frame));
		f -> kpage = kpage;
		f -> upage = p->upage;
		f -> t = thread_current();
		lock_acquire(&frame_lock);
		list_push_back(&frame_table, &f -> frame_elem);
		lock_release(&frame_lock);
	}

	return kpage;
}

void
free_frame(void * kpage)
{
	struct list_elem *e;
	lock_acquire(&frame_lock);
	for (e = list_begin (&frame_table); e != list_end (&frame_table);
       e = list_next (e))
    {
	  struct frame *f = list_entry (e, struct frame, frame_elem);
      if(f->kpage == kpage){
	    list_remove(e);
	    free(f);
      	break;
      }
    }
    lock_release(&frame_lock);
}