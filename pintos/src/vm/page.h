#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "threads/thread.h"
#include <hash.h>
#include "threads/palloc.h"
#include "filesys/file.h"
#include <stdint.h>

struct supp_page_entry
{
	struct hash_elem elem;

	void * upage;
	void * kpage;

	struct file* file;
	off_t offs;
	uint32_t read_bytes;
	uint32_t zero_bytes;
	bool writable;
};

void init_supp_page_table(struct hash* table);
void destroy_supp_page_table(struct hash* table);
bool insert_into_page_tables(struct file *file, off_t ofs, uint8_t *upage,// uint8_t *kpage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable);
bool stack_grow(void * fault_addr);
bool mmap_into_page_tables(struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable);
struct supp_page_entry * get_page(void * upage);

#endif