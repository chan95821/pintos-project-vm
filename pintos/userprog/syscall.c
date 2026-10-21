#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "threads/init.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "threads/malloc.h"
#include "filesys/filesys.h"
#include "devices/input.h"
#include "threads/palloc.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);



/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */
#define PAGE_SIZE 4096

static bool is_invaild_filename(char * filename);
static struct fd_info * seek_fd_info (struct thread *curr, int fd);

static void sys_halt (struct intr_frame *f);
static void sys_exit (struct intr_frame *f);
static void sys_fork (struct intr_frame *f);
static void sys_exec (struct intr_frame *f);
static void sys_wait (struct intr_frame *f);
static void sys_create (struct intr_frame *f);
static void sys_remove (struct intr_frame *f);
static void sys_open (struct intr_frame *f);
static void sys_filesize (struct intr_frame *f);
static void sys_read (struct intr_frame *f);
static void sys_write (struct intr_frame *f);
static void sys_seek (struct intr_frame *f);
static void sys_tell (struct intr_frame *f);
static void sys_close (struct intr_frame *f);
static void sys_dup2(struct intr_frame *f); 

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.

	uint64_t syscall_num = f->R.rax;
	int result;
	switch (syscall_num){
		case SYS_HALT:
			sys_halt(f);
			break;
		case SYS_EXIT:
			sys_exit(f);
			break;
		case SYS_FORK:
			sys_fork(f);
			break;
		case SYS_EXEC:
			sys_exec(f);
			break;
		case SYS_WAIT:
			sys_wait(f);
			break;
		case SYS_CREATE:
			sys_create(f);
			break;
		case SYS_REMOVE:
			sys_remove(f);
			break;
		case SYS_OPEN:
			sys_open(f);
			break;
		case SYS_READ:
			sys_read(f);
			break;
		case SYS_FILESIZE:
			sys_filesize(f);
			break;
		case SYS_WRITE:
			sys_write(f);
			break;
		case SYS_SEEK:
			sys_seek(f);
			break;
		case SYS_TELL:
			sys_tell(f);
			break;
		case SYS_CLOSE:
			sys_close(f);
			break;
		case SYS_DUP2:
			sys_dup2(f);
			break;
		default:
			printf ("system call!\n");
			thread_exit ();
	}
		
}

static void
sys_halt (struct intr_frame *f UNUSED) {
	power_off();
}

static void
sys_exit (struct intr_frame *f) {
	thread_current()->exit_status = f->R.rdi;
	thread_exit();
}

static void 
sys_fork (struct intr_frame *f) {
	char *fname = (void *)f->R.rdi;	
	
	if(fname == NULL || is_kernel_vaddr(fname)) {
		thread_current()->exit_status = -1;
		thread_exit();
	}
	
	if (is_invaild_filename(fname)) {
		f->R.rax = -1;
		return;
	}

	tid_t child_pid = process_fork(fname, f);
	f->R.rax = child_pid;
}

static void 
sys_exec (struct intr_frame *f) {
	char *fname = (void *)f->R.rdi;
	char *buffer = palloc_get_page(0);

	if (buffer == NULL){
		f->R.rax = -1;
		return;
	}		

	if ((strlcpy(buffer, (const char *)fname, PAGE_SIZE+1)) == PAGE_SIZE+1){
		f->R.rax = -1;
		return;
	}

	int reslt = process_exec(buffer);
	
	if (reslt == -1){
		f->R.rax = -1;
		return;
	}
}


static void
sys_wait (struct intr_frame *f) {
	f->R.rax = process_wait(f->R.rdi);
}

static void
sys_create (struct intr_frame *f) {
	char *fname = (char *)f->R.rdi;
	unsigned int initsize = f->R.rsi;
	
	if (fname == '\0'  || is_kernel_vaddr(fname)) {
		thread_current()->exit_status = -1;
		thread_exit();
	}

	if (is_invaild_filename(fname) || *fname == NULL){
		f->R.rax = 0;
		return;
	}
	
	lock_acquire(&file_lock);
	f->R.rax = filesys_create(fname, initsize);
	lock_release(&file_lock);
}

static void 
sys_remove (struct intr_frame *f) {
	char *fname = (char *)f->R.rdi;

	if(fname == NULL || is_kernel_vaddr(fname)) {
		thread_current()->exit_status = -1;
		thread_exit();
	}

	if (is_invaild_filename(fname) || *fname == NULL){
		f->R.rax = 0;
		return;
	}

	lock_acquire(&file_lock);
	f->R.rax = filesys_remove(fname);
	lock_release(&file_lock);
}

static void
sys_open (struct intr_frame *f) {
	char *fname = (char *)f->R.rdi;
	

	struct file *file;

	if(fname == NULL || is_kernel_vaddr(fname)) {
		thread_current()->exit_status = -1;
		thread_exit();
	}
	
	lock_acquire(&file_lock);
	if ( is_invaild_filename(fname)|| (file = filesys_open(fname)) == NULL) {
		f->R.rax = -1;
		lock_release(&file_lock);            
		return;
	}
	lock_release(&file_lock);
	
	struct thread *curr = thread_current();
	int fd;
	if (list_empty(&curr->file_list)) {
		fd = 2;
	}
	else {
		fd = (list_entry(list_begin(&curr->file_list), struct fd_info, elem)->fd) + 1;
	}

	struct fd_info *fd_info = malloc(sizeof(struct fd_info));
	struct file_info *file_info = malloc(sizeof(struct file_info));

	if (fd_info == NULL) {
		free(file_info);
		f->R.rax = -1;
		return;
	}

	if (file_info == NULL) {
		free(fd_info);
		f->R.rax = -1;
		return;
	}

	fd_info->fd = fd;
	file_info->reference = 1;
	fd_info->file_info = file_info;
	fd_info->file_info->my_file = file;
	
	list_push_front(&curr->file_list, &fd_info->elem);
	
	f->R.rax = fd;
}

static void
sys_filesize(struct intr_frame *f) {
	int fd = f->R.rdi;
	struct fd_info *fd_info;

	if((fd_info = seek_fd_info(thread_current() , fd)) == NULL){
		f->R.rax = -1;
		return;
	}

	lock_acquire(&file_lock);
	f->R.rax = file_length(fd_info->file_info->my_file);
	lock_release(&file_lock);

}

static void
sys_read (struct intr_frame *f) {

	//lock 만들어주기
	//lock 을 맨 앞에 만들지 파일시스템 관련된 것만 락을 걸지 생각  

	int fd = f->R.rdi;
	char *buffer = (char *)f->R.rsi;
	unsigned size = f->R.rdx;

	if(buffer == '\0' || is_kernel_vaddr(buffer + size)){
		thread_current()->exit_status = -1;
		thread_exit();
	}

	if(fd == 0){
		for(int i= 0; i < size; i++){
			buffer[i] = input_getc();
		}
		f->R.rax = size;
		return;
	}

	if(fd == 1){
		f->R.rax = -1;
		return;
	}

	if(size == 0){
		f->R.rax = 0;
		return;
	}

	struct fd_info *fd_info;

	if ((fd_info = seek_fd_info(thread_current() , fd)) == NULL) {
		f->R.rax = -1;
		return;
	}

	lock_acquire(&file_lock);
	f->R.rax = file_read(fd_info->file_info->my_file, buffer, size);
	lock_release(&file_lock);

}

static void
sys_write (struct intr_frame *f) {
	void *  buffer = (void *)f->R.rsi;
	int fd = f->R.rdi;
	uint64_t size = f->R.rdx;

	if (buffer == NULL || is_kernel_vaddr(buffer + size)) {
		thread_current()->exit_status = -1;
		thread_exit(); // 접근하면 안 되는 포인터를 주면 프로세스 종료 
	}
	if (fd == 1) {
		if(size == 0){
			f->R.rax = 0;
		}
		else{
			//putbuf는 자기가 락을 따로 잡음
			putbuf(buffer, size);
			f->R.rax = size;
		}
	}
	else{
		struct fd_info *fd_info = seek_fd_info(thread_current(), fd);
		if(fd_info == NULL){
			f->R.rax = -1;
			return;
		}
		lock_acquire(&file_lock);
		f->R.rax = file_write(fd_info->file_info->my_file, buffer, size);
		lock_release(&file_lock);
		
	}
}

static void
sys_seek (struct intr_frame *f) {
	int fd = f->R.rdi;
	unsigned position = f->R.rsi;

	if(position > INT32_MAX){
		return;
	}

	struct fd_info *fd_info;

	if ((fd_info = seek_fd_info(thread_current() , fd)) == NULL) {
		return;
	}

	lock_acquire(&file_lock);
	file_seek(fd_info->file_info->my_file, position);
	lock_release(&file_lock);
}

static void 
sys_tell (struct intr_frame *f) {
	int fd = f->R.rdi;
	struct fd_info *fd_info = seek_fd_info(thread_current(), fd);

	if (fd_info == NULL){
		f->R.rax = -1;
		return;
	}
	
	lock_acquire(&file_lock);
	f->R.rax = file_tell(fd_info->file_info->my_file);
	lock_release(&file_lock);
	
}

static void
sys_close (struct intr_frame *f) {
	int fd = f->R.rdi;

	// if ( fd == NULL ) {
	// 	thread_current()->exit_status = -1;
	// 	thread_exit();
	// } dup2 를 위해 지움

	struct thread *curr = thread_current();
	struct list_elem *ptr;
	struct list *list = &curr->file_list;

	for(ptr = list_begin(list); ptr != list_end(list); ptr = list_next(ptr)){
		if(list_entry(ptr, struct fd_info, elem)-> fd == fd){
			struct fd_info *fd_info = list_entry(ptr, struct fd_info, elem);
			if (fd_info->file_info->reference != 0){
				fd_info->file_info->reference--;
			}
			if (fd_info->file_info->reference == 0) {
				lock_acquire(&file_lock);
				file_close(fd_info->file_info->my_file);
				lock_release(&file_lock);
				free(fd_info->file_info);
			}
			list_remove(ptr);
			free(fd_info);
			break;
		}
	}
}

static void
sys_dup2(struct intr_frame *f) {
	int oldfd = f->R.rdi;
	int newfd = f->R.rsi;

	struct fd_info *oldfd_info;
	if ((oldfd_info = seek_fd_info(thread_current(), oldfd)) == NULL) {
		f->R.rax = -1;
		return;
	}

	if (oldfd == newfd) {
		f->R.rax = newfd;
		return;
	}

	struct fd_info *newfd_info;
	if ((newfd_info = seek_fd_info(thread_current(), newfd)) != NULL) {
		newfd_info->file_info->reference--;
		if (newfd_info->file_info->reference == 0) {
			lock_acquire(&file_lock);
			file_close(newfd_info->file_info->my_file);
			lock_release(&file_lock);
			free(newfd_info->file_info);
		}

		newfd_info->file_info = oldfd_info->file_info;
		newfd_info->file_info->reference++;
	}
	else{
		if ((newfd_info = malloc(sizeof(struct fd_info))) == NULL){
			f->R.rax = -1;
			return;
		}

		newfd_info->file_info = oldfd_info->file_info;
		newfd_info->fd = newfd;
		newfd_info->file_info->reference++;

		list_push_back(&thread_current()->file_list, &newfd_info->elem);
	}
	
	f->R.rax = newfd;

}

static bool is_invaild_filename(char * filename){
	size_t size;
	size = strnlen(filename, NAME_MAX+1);
	return !((size > 0) && (size <= (NAME_MAX)));

}

static struct fd_info * seek_fd_info (struct thread *curr, int fd) {
	struct list_elem *ptr;
	struct list *list = &curr->file_list;

	for(ptr = list_begin(list); ptr != list_end(list); ptr = list_next(ptr)){
		if(list_entry(ptr, struct fd_info, elem)-> fd == fd){
			return list_entry(ptr, struct fd_info, elem);
		}
	}

	return NULL;
}