#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "filesys/file.h"
#include "filesys/filesys.h"


#define MAX_ARGS 3
#define ERROR_CODE -1
#define USER_BOTTOM_ADDR ((void *) 0x08048000)
static void syscall_handler (struct intr_frame *);


void check_address(const void *addr);
void check_page_fault(const void *addr);
/* Get arguments from stack provided */
void get_arguments(struct intr_frame *f, int *arg, int count);
/* Check if the buffer is in user memory */
void check_buffer(void *buf, unsigned size);
void check_string(const void *str);



void halt(void);
void exit(int status);
int exec(const char *cmd_line);
int wait(int pid);
bool create(const char *file_name, unsigned size);
bool remove(const char *file_name);
int open(const char *file_name);
void close(int fd);
int filesize(int fd);
int read(int fd, void *buf, unsigned size);
int write(int fd, const void *buf, unsigned size);
unsigned tell(int fd);
void seek(int fd, unsigned pos);

void
syscall_init (void) 
{
  lock_init(&file_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  int *arg = malloc(sizeof(int) * MAX_ARGS);
  void *esp = f->esp;
  int syscall_no = *(int *) esp;

  switch (syscall_no) {
  case SYS_HALT:
    halt();
    break;

  case SYS_EXIT:
    /* Get status */
    get_arguments(f, arg, 1);
    exit(arg[0]);
    break;

  case SYS_EXEC:
    /* Get cmd line */
    get_arguments(f, arg, 1);
    check_page_fault((const void*) arg[0]);
    check_string((const void *) arg[0]);
    f->eax = exec((const char *) arg[0]);
    break;

  case SYS_WAIT:
    /* Get PID */
    get_arguments(f, arg, 1);
    f->eax = wait(arg[0]);
    break;
    
  case SYS_CREATE:
    /* Get file name, size */
    get_arguments(f, arg, 2);
    check_page_fault((const void*) arg[0]);
    check_string((const void *) arg[0]);
    f->eax = create((const char *) arg[0], (unsigned) arg[1]);
    break;

  case SYS_REMOVE:
    /* Get file name */
    get_arguments(f, arg, 1);
    check_page_fault((const void*) arg[0]);    
    check_string((const void *) arg[0]);
    f->eax = remove((const char *) arg[0]);
    break;

  case SYS_OPEN:
    /* Get file name */
    get_arguments(f, arg, 1);
    check_page_fault((const void*) arg[0]);
    check_string((const void *) arg[0]);
    f->eax = open((const char *) arg[0]);
    break;
  case SYS_CLOSE:
    /* Get file descriptor */
    get_arguments(f, arg, 1);
    close(arg[0]);
    break;


  case SYS_READ:
    /* Get file descriptor, buffer, and size */
    get_arguments(f, arg, 3);
    check_page_fault((const void *) arg[1]);

    /* Check if this buffer is in user memory */
    check_buffer((void *) arg[1], (unsigned) arg[2]);
    f->eax = read(arg[0], (void *) arg[1], (unsigned) arg[2]);
    break;
  case SYS_WRITE:
    /* Get file descriptor, buffer, and size */
    get_arguments(f, arg, 3);
    check_page_fault((const void *) arg[1]);
    check_buffer((void *) arg[1], (unsigned) arg[2]);
    f->eax = write(arg[0], (const void *) arg[1], (unsigned) arg[2]);
    break;

  case SYS_FILESIZE:
    /* Get file name */
    get_arguments(f, arg, 1);
    f->eax = filesize(arg[0]);
    break;


  case SYS_SEEK:
    /* Get file descriptor and position */
    get_arguments(f, arg, 2);
    seek(arg[0], (unsigned) arg[1]);
    break;

  case SYS_TELL:
    /* Get file descriptor */
    get_arguments(f, arg, 1);
    f->eax = tell(arg[0]);
    break;
    
    
  }
  
  /* 
     Holy moly! This makes me crazy for 3 traight days
     Remove this or all tests will fail
     Leave it here to remind me my nightmare
  */
  //thread_exit ();
}

void halt(void)
{
  shutdown_power_off();
}

void exit(int status)
{
  struct thread *t = thread_current();
  
  /* Notify parent of this prcess about its exit status */
  if (parent_alive(t->parent) && t->child_process)
    t->child_process->exit_status = status;

  printf("%s: exit(%d)\n", t->name, status);
  thread_exit();
}

int exec(const char *cmd_line)
{
  int pid = process_execute(cmd_line);
  struct child_process *child_process = get_child_process(pid);
  
  if (!child_process)
    return ERROR_CODE;
  /* Wait for child process to be allocated memory and load */
  if (child_process->load_status == NOT_LOADED)
    sema_down(&child_process->load_sema);
  
  if (child_process->load_status == LOAD_FAIL) {
    remove_child_process(child_process);
    return ERROR_CODE;
  }

  return pid;
}

int wait(int pid)
{
  return process_wait(pid);
}

bool create(const char *file_name, unsigned size)
{
  bool result = false;

  lock_acquire(&file_lock);
  result = filesys_create(file_name, size);
  lock_release(&file_lock);
  
  return result;
}

bool remove(const char *file_name)
{
  bool result = false;

  lock_acquire(&file_lock);
  result = filesys_remove(file_name);
  lock_acquire(&file_lock);
  
  return result;
}

int open(const char *file_name)
{
  int result = -1;
  
  lock_acquire(&file_lock);
  struct file *f = filesys_open(file_name);
  
  /* If error, return -1 */
  if (!f) {
    lock_release(&file_lock);
    return ERROR_CODE;
  }
  
  /* If success, add to process's file descriptor table */
  result = process_add_file(f);
  lock_release(&file_lock);

  return result;
}

void close(int fd)
{
  lock_acquire(&file_lock);
  process_close_file(fd);
  lock_release(&file_lock);
}


int filesize(int fd)
{
  int result = -1;

  lock_acquire(&file_lock);
  struct file *f = process_get_file(fd);
  
  if (!f) {
    lock_release(&file_lock);
    return ERROR_CODE;
  }
  
  result = file_length(f);
  lock_release(&file_lock);

  return result;
}

int read(int fd, void *buf, unsigned size)
{
  int result = -1;

  /* If input is from keyboard */
  if (fd == STDIN_FILENO) {
    /* Read byte by byte */
    uint8_t* buf_tmp = (uint8_t *) buf;

    int i = 0;
    for (i = 0; i < size; i++)
      buf_tmp[i] = input_getc();

    result = size;

    return result;
  }

  lock_acquire(&file_lock);
  struct file *f = process_get_file(fd);
  if (!f) {
    lock_release(&file_lock);
    return ERROR_CODE;
  }

  result = file_read(f, buf, size);
  lock_release(&file_lock);

  return result;
}

int write(int fd, const void *buf, unsigned size)
{
  int result = -1;

  if (fd == STDOUT_FILENO) {
    putbuf(buf, size);
    result = size;
    return result;
  }

  lock_acquire(&file_lock);
  struct file *f = process_get_file(fd);
  if (!f) {
    lock_release(&file_lock);
    return ERROR_CODE;
  }

  result = file_write(f, buf, size);
  lock_release(&file_lock);
  
  return result;
}

unsigned tell(int fd)
{
  unsigned result = 0;

  lock_acquire(&file_lock);
  struct file *f = process_get_file(fd);
  
  if (!f) {
    lock_release(&file_lock);
    return ERROR_CODE;
  }
  
  result = file_tell(f);
  lock_release(&file_lock);
  
  return result;
}

void seek(int fd, unsigned pos)
{
  lock_acquire(&file_lock);
  struct file *f = process_get_file(fd);

  if (!f) {
    lock_release(&file_lock);
    return;
  }

  file_seek(f, pos);
  lock_release(&file_lock);
}

void check_address(const void *addr)
{
  if (!is_user_vaddr(addr) ||
      addr < USER_BOTTOM_ADDR) 
    exit(ERROR_CODE);
}

void check_page_fault(const void *addr)
{
  struct thread *t = thread_current();

  check_address(addr);
  void *ptr = pagedir_get_page(t->pagedir, addr);
  
  if (!ptr)
    exit(ERROR_CODE);
}

/* Get arguments from stack provided */
void get_arguments(struct intr_frame *f, int *arg, int count)
{
  int i = 0;
  int *ptr = NULL;

  /* Ignore system call num */
  for (i = 0; i < count; i++) {
    ptr = (int *) f->esp + i + 1;
    check_address((const void *) ptr);
    arg[i] = *ptr;
  }
}

/* Check if the buffer is in user memory */
void check_buffer(void *buf, unsigned size)
{
  int i = 0;
  char *buf_tmp = (char *) buf;
  
  for (i = 0; i < size; i++) {
    check_address((const void *) buf_tmp);
    buf_tmp++;
  }
}

/* 
   Check 2 things:
   1. Check if it is in user memory
   2. Check if it is mapped to phys memory
*/
void check_string(const void *str)
{
  
}
