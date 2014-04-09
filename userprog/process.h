#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

struct process_file 
{
  struct file *file;
  int fd;
  struct list_elem elem;
};

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

struct child_process * add_child_process(int pid);
struct child_process * get_child_process(int pid);
void remove_child_process(struct child_process *cp);
void remove_all_child(void);

#endif /* userprog/process.h */
