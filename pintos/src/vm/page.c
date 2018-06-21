#include <stdio.h>
#include <debug.h>
#include <string.h>
#include "page.h"
#include "frame.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"

static unsigned hash_func(const struct hash_elem *e, void * aux UNUSED);
static bool hash_cmp_func(const struct hash_elem *a, const struct hash_elem *b, void * aux UNUSED);
static void hash_free_func(struct hash_elem *e, void *aux UNUSED);

void
init_supp_page_table(struct hash* table)
{
	hash_init(table, hash_func, hash_cmp_func, NULL);
}

bool 
insert_into_page_tables(struct file *file, off_t ofs, uint8_t *upage,// uint8_t *kpage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
	struct thread *t = thread_current ();

    struct supp_page_entry *p = malloc(sizeof(struct supp_page_entry));
    if(!p)
    	return false;
  	p -> upage = upage;
  	p -> file = file;
	p -> offs = ofs;
	p -> read_bytes = read_bytes;
	p -> zero_bytes = zero_bytes;
	p -> writable = writable;
	if (hash_insert(&t->supp_page_table, &p->elem) == NULL)
		return true;
	return false;
}

void
destroy_supp_page_table(struct hash* table){
	hash_destroy (table, hash_free_func);
}

bool
stack_grow(void * fault_addr)
{
	if((size_t) (PHYS_BASE - pg_round_down(fault_addr)) > (1<<23))
		return false;

	uint8_t *kpage;

	struct supp_page_entry * p = malloc(sizeof(struct supp_page_entry));
	p->upage = pg_round_down(fault_addr);
	p->writable = true;
	enum palloc_flags flags = PAL_USER;
	kpage = alloc_frame(flags, p);

	if(kpage == NULL)
	{
		free(p);
		return false;
	}

	struct thread *t = thread_current();
	if(!(pagedir_get_page (t->pagedir, p->upage) == NULL
      && pagedir_set_page (t->pagedir, p->upage, kpage, p->writable)))
	{
	  free(p);
	  free_frame(kpage);
	  return false;
	}
	
	if(hash_insert(&thread_current() -> supp_page_table, &p -> elem) != NULL)
	{
		free(p);
		free_frame(kpage);
		return false;
	}

	return true;
}

bool 
mmap_into_page_tables(struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  struct thread *t = thread_current ();

  struct supp_page_entry *p = malloc(sizeof(struct supp_page_entry));
  if(!p)
    return false;

  p -> upage = upage;
  p -> file = file;
  p -> offs = ofs;
  p -> read_bytes = read_bytes;
  p -> zero_bytes = zero_bytes;
  p -> writable = writable;

  struct mmap_ * mmap = malloc(sizeof(struct mmap_));
  if(!mmap){
    free(p);
    return false;
  }
  mmap->map_id = t->map_id_count;
  mmap->p = p;

  list_push_back(&t->mmap_list, &mmap->elem);

  if (hash_insert(&t->supp_page_table, &p->elem)){
    //free(p);
    return false;
  }

  return true;
}

struct supp_page_entry * 
get_page(void * upage) 
{
	struct thread * t = thread_current();

	upage = pg_round_down(upage);
	struct supp_page_entry stub;
	stub.upage = upage;

	struct hash_elem * e = hash_find(&t->supp_page_table, &stub.elem);

	if(e != NULL)
		return hash_entry(e, struct supp_page_entry, elem);

	return NULL;
	
}

static void 
hash_free_func(struct hash_elem *e, void *aux UNUSED)
{
	struct supp_page_entry *p = hash_entry(e, struct supp_page_entry, elem);
	pagedir_clear_page(thread_current()->pagedir, p->upage);
	free(p);
}	

static unsigned
hash_func(const struct hash_elem *e, void * aux UNUSED)
{
	struct supp_page_entry *p = hash_entry(e, struct supp_page_entry, elem);

	return hash_int((int) p->upage);
}

static bool
hash_cmp_func(const struct hash_elem *a, const struct hash_elem *b, void * aux UNUSED)
{
	if (hash_entry(a, struct supp_page_entry, elem) -> upage <
			hash_entry(b, struct supp_page_entry, elem) -> upage)
		return true;

	return false;
}