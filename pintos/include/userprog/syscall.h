#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <list.h>

void syscall_init (void);

struct fd_info{
	struct file_info *file_info;
	struct list_elem elem;
	int fd;
};

struct file_info {
	struct file *my_file;
	int reference;
};
#endif /* userprog/syscall.h */
