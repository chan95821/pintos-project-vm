#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <list.h>

void syscall_init (void);

struct fd_info{
	struct file *my_file;
	struct list_elem elem;
	int fd;
};

#endif /* userprog/syscall.h */
