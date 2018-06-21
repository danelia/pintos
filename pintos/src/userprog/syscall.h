#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

/* Lock for synchronization */
struct lock synch;

void munmap(int mmap_id);

#endif /* userprog/syscall.h */
