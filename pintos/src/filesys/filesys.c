#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);
static int get_next_part (char part[NAME_MAX + 1], const char **srcp);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format)
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();
  cache_init();

  if (format)
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void)
{
  free_map_close ();
  cache_destroy ();
}

/* Extracts a file name part from *SRCP into PART, and updates *SRCP so that the
next call will return the next file name part. Returns 1 if successful, 0 at
end of string, -1 for a too-long file name part. */
static int
get_next_part (char part[NAME_MAX + 1], const char **srcp) {
    const char *src = *srcp;
    char *dst = part;
    
    /* Skip leading slashes. If it’s all slashes, we’re done. */
    while (*src == '/')
        src++;
    if (*src == '\0')
        return 0;

    /* Copy up to NAME_MAX character from SRC to DST. Add null terminator. */
    while (*src != '/' && *src != '\0') {
        if (dst < part + NAME_MAX)
            *dst++ = *src;
        else
            return -1;
        src++;
    }
    *dst = '\0';

    /* Advance source pointer. */
    *srcp = src;
    return 1;
}

/* -1: Invalid Path
 *  0: Not Found, but it is last part
 *  1: Found, and it is last part
 */
static int
find_file (const char *src, char* filename, struct dir **cwd, struct inode **next_inode) {
  int result = -1;
  *next_inode = NULL;
  block_sector_t cwd_sector = thread_current()->cwd_sector;
  /* Check if given name corresponds to relative
      or absolute path. */
  if (src[0] == '/') {
    *cwd = dir_open_root ();
  } else {
    /* If the path is absolute we need to start from root directory. */
    *cwd = dir_open (inode_open (cwd_sector));
  }
  
  filename[0] = '\0';
  
  /* Iterate over directories until we hit a file or the end. */
  while (true) 
  { 
    int part_result = get_next_part (filename, &src);
    if (part_result == -1) /* Too Big Name. */
      break;
    if (part_result == 0) /* Empty */
      break;

    bool found = dir_lookup (*cwd, filename, next_inode);

    if (found) {
      if (*src == '\0') {
        result = 1;
        break;
      }
    } else {
      if (*src == '\0')
        result = 0;
      break;
    }

    dir_close(*cwd);
    *cwd = dir_open (*next_inode);
  }
  return result;
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size, bool is_dir)
{ 
  char filename[NAME_MAX + 1];
  struct dir *parent_dir = 0;
  struct inode *inode;
  bool res = find_file(name, filename, &parent_dir, &inode);
  if (res != 0) {
    dir_close(parent_dir);
    return false;
  }
  
  block_sector_t inode_sector = 0;
  if (parent_dir == NULL || !free_map_allocate (1, &inode_sector))
    return false;
  if (!inode_create (inode_sector, initial_size, is_dir)) {
    free_map_release (inode_sector, 1);
    return false;
  }
  bool success = true;
  if (is_dir) {
    struct dir* new_dir = dir_open(inode_open (inode_sector));
    success &= (dir_add (new_dir, ".", inode_sector)
                    && dir_add (new_dir, "..", inode_get_inumber(dir_get_inode(parent_dir))));
    dir_close (new_dir);
  }
  success &= dir_add (parent_dir, filename, inode_sector);
  dir_close (parent_dir);
  if (!success)
    free_map_release (inode_sector, 1);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  struct dir *dir;
  struct inode *inode;

  char filename[NAME_MAX + 1];
  int res = find_file (name, filename, &dir, &inode);
  dir_close (dir);
  if (res == 1) {
    if (inode_is_dir(inode)) return dir_open (inode);
    else return file_open (inode);
  }
  inode_close(inode);
  return NULL;
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name)
{
  struct dir *dir;
  struct inode *inode;

  char filename[NAME_MAX + 1];
  int res = find_file (name, filename, &dir, &inode);
  bool success = false;
  if (res == 1 && inode_get_inumber(inode) != thread_current()->cwd_sector) {
    success = dir != NULL && dir_remove (dir, name);
  }
  dir_close (dir);
  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

bool
filesys_change_dir (const char* path) {
  struct dir *dir;
  struct inode *inode;
  char filename[NAME_MAX + 1];
  int res = find_file (path, filename, &dir, &inode);
  dir_close(dir);
  bool success = false;
  if (res == 1) {
    if (inode_is_dir(inode)) {
      thread_current()->cwd_sector = inode_get_inumber(inode);
      success = true;
    }
  }
  inode_close(inode);
  return success;
}