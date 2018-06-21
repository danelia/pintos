#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include <stdio.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "filesys/directory.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define NUM_DIRECT 10

#define  INDEX_PER_SECTOR 128
/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {

    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    uint32_t unused[107];               /* Not used. */
    
    uint32_t dir_index;
    uint32_t indir_index;
    uint32_t double_indir_index;

    off_t offs;
    bool isdir;
    block_sector_t parent;
    uint32_t file_num;

    block_sector_t ptr[12];
  };

static int inode_allocate(struct inode_disk* inode_disk, off_t init_length);
static off_t inode_indirect_allocate(struct inode_disk* inode_disk, size_t sectors);
static off_t inode_double_indirect_allocate(struct inode_disk* inode_disk, size_t new_sectors);
static off_t inode_double_indirect_allocate_helper(struct inode_disk* inode_disk, block_sector_t *ptr, size_t sectors);
struct lock lock;
/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
    struct lock lock;
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  // ASSERT (inode != NULL);
  // if (pos < inode->data.length)
  //   return inode->data.start + pos / BLOCK_SECTOR_SIZE;
  // else
  //   return -1;
  ASSERT(inode != NULL)
  int index = pos/BLOCK_SECTOR_SIZE;
  if(index < NUM_DIRECT)
    return inode->data.ptr[index];

  if(index < NUM_DIRECT + INDEX_PER_SECTOR)
  {
    block_sector_t buff[INDEX_PER_SECTOR];
    block_read(fs_device, inode->data.ptr[10], buff);
    return buff[index - NUM_DIRECT];
  }

  if(index < NUM_DIRECT + INDEX_PER_SECTOR + INDEX_PER_SECTOR*INDEX_PER_SECTOR)
  {
   // printf("===========reading last bytes===========\n");
    int cur_index = index - NUM_DIRECT - INDEX_PER_SECTOR;
    //printf("==============curent %d===========\n", cur_index);
    int first_ptr = cur_index/INDEX_PER_SECTOR;
    int second_ptr = cur_index%INDEX_PER_SECTOR;

    block_sector_t buff[INDEX_PER_SECTOR];
    block_read(fs_device, inode->data.ptr[11], buff);
    block_sector_t second_lvl_sector = buff[first_ptr];
    block_read(fs_device, second_lvl_sector, buff);
    //printf("===========reading last bytes %d===========\n",buff[second_ptr]);
   // printf("===========reading last bytes %d===========\n",second_lvl_sector);
   // printf("===========reading last bytes %d===========\n",inode->data.ptr[11]);
    return buff[second_ptr];
  }
  return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
  thread_current()->cwd = inode_open(ROOT_DIR_SECTOR);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool isdir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  //lock_acquire(&lock);
  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      // size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->dir_index = 0;
      disk_inode->indir_index = 0;
      disk_inode->double_indir_index = 0;
      disk_inode->parent = sector;
      disk_inode->isdir = isdir;
      disk_inode->file_num = 0;
      /*if (free_map_allocate (sectors, &disk_inode->start))
        {
          block_write (fs_device, sector, disk_inode);
          if (sectors > 0)
            {
              static char zeros[BLOCK_SECTOR_SIZE];
              size_t i;

              for (i = 0; i < sectors; i++)
                block_write (fs_device, disk_inode->start + i, zeros);
            }
          success = true;
        }*/
      if(inode_allocate(disk_inode, 0)){
        block_write(fs_device, sector, disk_inode);
        success = true;
      }
      free (disk_inode);
    }
    //lock_release(&lock);
  return success;
}

static int 
inode_allocate(struct inode_disk* inode_disk, off_t init_length){
  size_t new_sectors = bytes_to_sectors(inode_disk->length) - bytes_to_sectors(init_length);
  size_t i = 0;
  block_sector_t buff[INDEX_PER_SECTOR];
  memset(buff, 0, INDEX_PER_SECTOR * 4);

  if(new_sectors == 0)
    return 1;

  //lock_acquire(&lock);
  if(inode_disk->dir_index < NUM_DIRECT)
  {
    size_t temp = NUM_DIRECT - inode_disk->dir_index;
    temp = new_sectors < temp ? new_sectors : temp;
    for(; i < temp; i++)
    {
      free_map_allocate(1, &inode_disk->ptr[inode_disk->dir_index]);
      block_write(fs_device, inode_disk->ptr[inode_disk->dir_index], buff);
     // printf("=======wrote zeros dir %d========\n",i+1);
      new_sectors--;
      inode_disk->dir_index++;
    }
  }

  if (new_sectors == 0){
    //lock_release(&lock);
    return 1; 
  }

  if(inode_disk->dir_index < 11)
  {
    new_sectors -= inode_indirect_allocate(inode_disk, new_sectors);
    if(inode_disk->indir_index == 0)
      inode_disk->dir_index++;
  }

  if (new_sectors == 0){
    //lock_release(&lock);
    return 1;
  }
  
  if(inode_disk->dir_index < 12)
  {
    new_sectors -= inode_double_indirect_allocate(inode_disk, new_sectors);
  }

  //lock_release(&lock);
  if (new_sectors == 0)
    return 1;
  
  return 0;
}

static off_t
inode_indirect_allocate(struct inode_disk* inode_disk, size_t new_sectors)
{
  off_t result = 0;
  block_sector_t buff[INDEX_PER_SECTOR];
  char zeros[BLOCK_SECTOR_SIZE];
  memset(buff, 0, BLOCK_SECTOR_SIZE);
  memset(zeros, 0, BLOCK_SECTOR_SIZE);
  if(inode_disk->indir_index == 0)
  {
    free_map_allocate(1, &inode_disk->ptr[10]);
    //block_write(fs_device, inode_disk->ptr[10], buff);
  }else
  {
    block_read(fs_device, inode_disk->ptr[10], buff);
  }

  size_t i = 0;

  size_t temp = new_sectors < INDEX_PER_SECTOR - inode_disk->indir_index ? new_sectors : INDEX_PER_SECTOR - inode_disk->indir_index;
  for(; i < temp; i++)
  {
    free_map_allocate(1,&buff[inode_disk->indir_index]);
    block_write(fs_device, buff[inode_disk->indir_index], zeros);
   // printf("============wrote zeros indir %d============\n", result+1);
    result++;
    inode_disk->indir_index++;
    inode_disk->indir_index %= INDEX_PER_SECTOR;
  }
  block_write(fs_device, inode_disk->ptr[10], buff);
  return result;
}

static off_t
inode_double_indirect_allocate(struct inode_disk* inode_disk, size_t new_sectors)
{
  off_t result = 0;
  block_sector_t buff[INDEX_PER_SECTOR];
  memset(buff, 0, BLOCK_SECTOR_SIZE);
  if(inode_disk->ptr[11] == 0)
  {
    free_map_allocate(1,&inode_disk->ptr[11]);
   //printf("============alloc sec No:%d===========",inode_disk->ptr[11]);
    //block_write(fs_device, inode_disk->ptr[11], buff);
  }else
  {
    block_read(fs_device, inode_disk->ptr[11], buff);
   // printf("============getting ot sec No:%d===========",inode_disk->ptr[11]);
  }
  //size_t i = 0;
  // size_t first_lvl_secs = DIV_ROUND_UP(new_sectors, INDEX_PER_SECTOR);
  size_t sectors = new_sectors;
  while(sectors > 0){
    int alloced = inode_double_indirect_allocate_helper(inode_disk, &buff[inode_disk->indir_index], sectors);
    sectors -= alloced;
    result += alloced;
    if(inode_disk->double_indir_index == 0)
      inode_disk->indir_index++;
  }
  block_write(fs_device, inode_disk->ptr[11], buff);
  return result;

}

static off_t
inode_double_indirect_allocate_helper(struct inode_disk* inode_disk, block_sector_t *ptr, size_t sectors)
{
  off_t result = 0;
  block_sector_t buff[INDEX_PER_SECTOR];
  char zeros[BLOCK_SECTOR_SIZE];
  memset(buff, 0, BLOCK_SECTOR_SIZE);
  memset(zeros, 0, BLOCK_SECTOR_SIZE);
  if(inode_disk->double_indir_index == 0)
  {
    free_map_allocate(1, ptr);
    //block_write(fs_device, *ptr, buff);
  }else
  {
    block_read(fs_device, *ptr, buff);
  }
  size_t i = 0;
  size_t temp = sectors < INDEX_PER_SECTOR - inode_disk->double_indir_index ? sectors
                                       : INDEX_PER_SECTOR - inode_disk->double_indir_index;
  for(; i < temp; i++)
  {
    free_map_allocate(1, &buff[inode_disk->double_indir_index]);
    block_write(fs_device, buff[inode_disk->double_indir_index], zeros);
   // printf("============wrote zeros double_indir %d============\n", buff[inode_disk->double_indir_index]);
    result++;
    inode_disk->double_indir_index++;
    inode_disk->double_indir_index %= INDEX_PER_SECTOR;
  }
  block_write(fs_device, *ptr, buff);

  return result;
}


/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init(&inode->lock);
  block_read (fs_device, inode->sector, &inode->data);
  //printf("=======opened %d===\n",inode->data.length); 
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  //lock_acquire(&lock);
  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          free_map_release (inode->sector, 1);
          // free_map_release (inode->data.start,
          //                   bytes_to_sectors (inode->data.length));
          // DESTROY INODE_DISK PROPERTY
        }else
        {
          block_write(fs_device, inode->sector, &inode->data);
        }
     // printf("=======closing %d===\n",inode->data.length);  
      free (inode);
    }
  //lock_release(&lock);
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode,offset);
      //printf("===============secNo:%d============", sector_idx);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else
        {
          /* Read sector into bounce buffer, then partially copy 
             into caller's buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  if(offset + size > inode_length(inode)){
    size_t old_len = inode_length(inode);
    inode->data.length = offset+size;
    inode_allocate(&inode->data, old_len);
  }
  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          //lock_acquire(&lock);
          block_write (fs_device, sector_idx, buffer + bytes_written);
          //lock_release(&lock);
        }
      else
        {
          /* We need a bounce buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          // If the sector contains data before or after the chunk
            // we're writing, then we need to read in the sector
             //first.  Otherwise we start with a sector of all zeros. 
          if (sector_ofs > 0 || chunk_size < sector_left)
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          //lock_acquire(&lock);
          block_write (fs_device, sector_idx, bounce);
          //lock_release(&lock);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

bool
isdir(struct inode * inode)
{
  struct inode_disk * disk = &inode->data;
  return disk->isdir;
}

block_sector_t
get_inumber(struct inode * inode)
{
  return inode->sector;
}

off_t 
inode_offset(struct inode * inode)
{
  struct inode_disk * disk = &inode->data;
  return disk->offs;
}

uint32_t 
inode_file_num(struct inode * inode)
{
  struct inode_disk * disk = &inode->data;
  return disk->file_num;
}

struct inode *
inode_open_parent (struct inode * inode)
{
  if(inode != NULL)
  {
    struct inode_disk * disk = &inode->data;
    inode = inode_open(disk->parent);
  }

  return inode;
}

int 
get_opn_cnt (struct inode * inode)
{
  return inode->open_cnt;
}

bool
get_removed(struct inode * inode)
{
  return inode->removed;
}

void
inode_lock(struct inode * inode)
{
  lock_acquire(&inode->lock);
}

void
inode_unlock(struct inode * inode)
{
  lock_release(&inode->lock);
}

bool
inode_add (struct inode *parent, block_sector_t child_sector, off_t offs)
{
  //struct inode *child = inode_open(child_sector);
  struct inode_disk disk;// = &child->data;

  /*if (!isdir (parent))
    return false;*/

  //lock_acquire(&lock);
  block_read(fs_device, child_sector, &disk);
  disk.parent = parent->sector;
  disk.offs = offs;
  block_write(fs_device, child_sector, &disk);

  block_read(fs_device, child_sector, &disk);
  disk.file_num += 1;
  block_write(fs_device, child_sector, &disk);
  //lock_release(&lock);

  return true;
}

bool
inode_remove_f (struct inode *inode)
{
  /*if (!isdir (inode))
    return false;*/
  struct inode_disk disk ;

  /*if (!isdir (inode))
    return false;*/

  //lock_acquire(&lock);
  block_read(fs_device, inode->sector, &disk);
  //disk->parent = parent->sector;
  //disk->offs = offs;
  //block_write(fs_device, child_sector, disk);

  //block_read(fs_device, child_sector, disk);
  struct inode * parent = inode_open(disk.parent);
  parent->data.file_num -= 1;
  inode_close(parent);
  //block_write(fs_device, child_sector, disk);
  //inode_remove(inode);
  block_write(fs_device, inode->sector, &disk);
  //lock_release(&lock);

  return true;
}