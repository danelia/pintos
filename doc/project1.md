Design Document for Project 1: Threads
======================================

## Group Members

* Giorgi Danelia <gdane14@freeuni.edu.ge>
* Luka Kiria <lkiri15@freeuni.edu.ge>
* Saba Tatanashvili <stata14@freeuni.edu.ge>
* Irakli Qantaria <ikant14@freeuni.edu.ge>

## Task 1: Efficient Alarm Clock

### Data Structures

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

### Algorithm

In `timer_sleep()` instead of yielding and busy waiting, a thread blocks on a semaphore. We calculate pre-determined time, when current thread should awake and store it in the variable `tick` along with semaphore and thread itself in a structure `sleep_list_elem` described above. The structure is inserted into `sleep_list` in increasing sorted manner by `tick` variable.

In `timer_interrupt()` at each tick we traverse `sleep_list` and if any thread needs to be awake at current time, we realese the thread semaphore and take it out of `sleep_list`.

### Synchronization

Because, pintos lists are not threadsafe, we utilize a lock `sleep_list_lock` and every time we have to insert into `sleep_list` we acquire a lock and release after we are done.

### Rationale

We decided to insert elements in list in sorted manner, because removing first element from list is more efficient than traversing the entire list.

## Task 2: Priority Scheduler

### Data Structures

###### In thread.h

```C
struct thread
  {
    ...

    struct list lock_list;             /* locks that current thread has acquired */
    struct lock *thread_lock;          /* pointer on lock that thread could be locked on */
    int donation_priority;             /* priority other thread has donated */
  }
```
###### In synch.h

```C
struct lock
  {
    ...
    
    int priority;             /* max prioirty */
    struct list_elem lock_list_elem;   /* list element for lock list */
  };
```

###### In synch.c

```C
/* reccursive helper method to donate priority */
static void
donate_helper(struct lock *lock, int priority)
{
    ...
}
```

### Algorithm

#### Scheduling with Priorities and Donated Priorities Overview

Because of priority donation, priorities of threads can be altered even after they have been inserted in the ready list. Therefore, using ordered insertion into list and then popping front will no longer work correctly for the new scheduling algorithm. What we do now, is we insert into the ready queue using `list_push_back()`, done In `O(1)`, and then use `list_max()` to schedule a new thread with maximum priority. 

Another nuance to consider, is that a thread now had two priorities, its original priority and a priority donated from another thread. Both are kept in thread structure separately, so whenever we utilize thread’s priority for unblocking or scheduling, instead of always using one or the other, we always use whichever priority is higher.

#### Priority donations

Altering already existing code for Priority donations, other than above mentioned scheduling changes happens in mainly two places: in `lock_acquire()` and `lock_release()`.

#### Blocking on a lock

Whenever a thread successfully acquires a lock, no alterations are done to the previous algorithm. All the changes happen when a thread cannot acquire a lock and must therefore wait on it and donate its priority to the lock holder if this priority is higher than that of the holder. We must also consider the case of nested and chain donations, when a thread must donate to the threads holding the lock that the current lock’s holder is waiting on and so forth.

To do this, as we’ve mentioned above each thread keeps a pointer to the lock its currently waiting on. Using this to our advantage, we utilize a recursive helper function `donate_helper()` which takes a pointer to a lock, and a new priority, and sets the lock holder’s priority to the new one if its higher than its old priority. Then it recursively calls itself for the same donation priority and the lock that the current lock holder is waiting on. This goes on until the lock holder is no longer waiting on a lock.

#### Releasing a lock

A key moment to consider with priority donations and lock releases, is that a lock holder, which most likely has a donated priority, could be a holder of several locks, and its priority will always be the highest of the priorities of each of these locks’ waiter donations. So whenever a thread releases a lock, it could still be holding other locks and thus its priority must be set to the highest of the remaining locks’ waiters. In order to implement this we have to somehow keep track of all the priorities a thread could possibly take on for all the remaining locks. We do this by adding priority variable to the lock structure, which would represent the highest priority of all the threads waiting on it and would be updated if necessary along with its locks holder’s donated priority during the recursive `donate_helper()`.

In `lock_release()` a thread goes over all the locks it has acquired, kept inside the locks list we’ve added to the thread structure and updates its own donation priority to the highest of these locks’ priorities (kept inside the priority variable). As for the lock that the thread is currently releasing, it simply removes it from the locks list and sets its priority to a minimum to guarantee that priorities can be donated to it in the future after some other threads acquire and wait on it.

#### Frequent yielding

Because of the fact that with our new priority scheduler, priorities of threads are not set in stone and can be altered anytime, we not only yield after `thread_set_priority()`, but also after `sema_up()`, to immediately make way for a thread with higher priority than ours to be scheduled.

#### Conditional variables

After current thread has yielded the cpu to another thread in `cond_signal()`, to make sure that the highest priority thread ends up waking up, we apply the same logic we used with regular scheduling. As each conditional variable keeps a list of waiting semaphores, to make sure that the semaphore that gets taken out of this list has a thread with the highest priority waiting on it, we first sort the list and then simply pop the front element of it. For this we needed to implement a compare function for the `semaphore_element`.

### Synchronization

Traversing lists and other such structures that many threads can access at the same time atomically, with turning interrupts on and off have already been standardly used in the code. We use the same technique with acquiring locks while recursively donating priorities and also with lock releases while traversing a list of all the acquired locks.

### Rationale

One trade off we have from our previous code, is that with ready queue insertion into the ready list happened ordered so when taking the next thread our all we had to do was pop the front of the list to get the maximum priority thread. Removing was done in `O(1)` time. Now removing is done in `O(n)`. However, inserting into the list is done in `O(1)`.

Because We’ve added several variables to already established structures such as adding priority to the lock structure we gain quick access to information that would otherwise require us to go over many structures if implemented differently.

## Task 3: Multi-level Feedback Queue Scheduler (MLFQS)

### Data Structures

###### In thread.h

```C
struct thread{

    ...
    fixed_point_t nice;                /* thread nice value */
    fixed_point_t recent_cpu;          /* thread recent_cpu value */
  };
```

###### In thread.c

```C
struct list mlfqs_list[PRI_MAX + 1];    // mlfqs list array
fixed_point_t load_avg;                 // load avg value
int ready_threads;                      // amount of ready_threads

/* shortcut to put thread in mlfqs list array */
#define put_in_mlfqs(T) list_push_back(&mlfqs_list[T->priority], &T->elem)
```

### Algorithm

#### Inserting into Multi-level Feedback Queue

After calculating thread's priority, we insert it into the list with matching index number to it (priority). Our mlfq has 64 indices, therefore they completely match any value of thread priority.

#### Scheduling

In order to choose next thread to run, we have to choose one from a non-empty list at the highest index of the array. We use `list_pop_front()` to take thread out of the list. If no such non-empty list exists we schedule idle thread.

#### Calculating thread and system variables

In `thread_tick()` for particular time frames, we update thread priorities, recent cpus and load avarege accordingly: at every tick we increment running thread's recent cpu by one, at every forth tick we recalculate running thread priority and at every second we recalculate load avarege and recent cpus for all ready threads.

### Synchronization

Because most of our code is run either during interupt or within functions where interupts are disabled, there is not much need for synchronization.

### Rationale

Inserting into the mlfq takes `O(1)` time, because we are using `list_push_back()` since all threads in that particular list have same priority. Taking the thread out of the list requires traversal of the array and since size of array is fixed and we use `list_pop_back()` the overall time is `O(1)`.

We alse made slight modifications to the algorithm described in the assigment description. Instead of updating every threads priority every 4th tick, we only update that of currently running, since only it's recent cpu has updated in the last 4 ticks.
