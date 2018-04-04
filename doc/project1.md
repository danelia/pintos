Design Document for Project 1: Threads
======================================

## Group Members

* Giorgi Danelia <gdane14@freeuni.edu.ge>
* Luka Kiria <lkiri15@freeuni.edu.ge>
* Saba Tatanashvili <stata14@freeuni.edu.ge>
* Irakli Qantaria <ikant14@freeuni.edu.ge>

## Task 1: Efficient Alarm Clock

#### Data Structures

###### In timer.h

```C
struct sleep_list_elem
{
	struct list_elem list_el;  // For inserting into list

	struct semaphore* sem;	// Semaphore for unblocking sleeping thread
	struct thread* curr_thread; // Sleeping thread

	int64_t tick; // Pre-determined time for wake up
};
```

###### In timer.c

```C
struct list sleep_list; // Sleeping threads are stored
struct lock sleep_list_lock; // Locks sleep_list for synchronization
```

#### Algorithm

In `timer_sleep(int64_t ticks)` instead of yielding and busy waiting, a thread blocks on a semaphore. We calculate pre-determined time, when current thread should awake and store it in the variable `tick` along with semaphore and thread itself in a structure `sleep_list_elem` described above. The structure is inserted into `sleep_list` in increasing sorted manner by `tick` variable.

In `timer_interrupt(struct intr_frame *args UNUSED)` at each tick we traverse `sleep_list` and if any thread needs to be awake at current time, we realese the thread semaphore and take it out of `sleep_list`.

#### Synchronization

Because, pintos lists are not threadsafe, we utilize a lock `sleep_list_lock` and every time we have to insert into `sleep_list` we acquire a lock and release after we are done.

#### Rationale

We decided to insert elements in list in sorted manner, because removing first element from list is more efficient than traversing the entire list.
