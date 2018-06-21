#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/thread.h"
#include "lib/kernel/list.h"
#include "threads/palloc.h"
#include <stdint.h>
#include "page.h"

struct frame
{
	void * kpage;
	void * upage;

	struct list_elem frame_elem;

	struct thread * t;
};

void frame_init(void);
uint8_t * alloc_frame(enum palloc_flags flags, struct supp_page_entry * p);
void free_frame(void * kpage);


#endif