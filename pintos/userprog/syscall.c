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

static bool is_invaild_filename(char * filename);
static struct fd_info * seek_fd_info (struct thread *curr, int fd);

static void sys_halt (struct intr_frame *f);
static void sys_exit (struct intr_frame *f);
static void sys_wait (struct intr_frame *f);
static void sys_create (struct intr_frame *f);
static void sys_open (struct intr_frame *f);
static void sys_read (struct intr_frame *f);
static void sys_write (struct intr_frame *f);
static void sys_close (struct intr_frame *f);
static void sys_filesize (struct intr_frame *f);


struct fd_info{
	struct file *my_file;
	struct list_elem elem;
	int fd;
};

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
		case SYS_WAIT:
			sys_wait(f);
			break;
		case SYS_CREATE:
			sys_create(f);
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
		case SYS_CLOSE:
			sys_close(f);
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
	
	f->R.rax = filesys_create(fname, initsize);
}

static void
sys_open (struct intr_frame *f) {
	char *fname = (char *)f->R.rdi;
	

	struct file *file;

	if(fname == NULL || is_kernel_vaddr(fname)) {
		thread_current()->exit_status = -1;
		thread_exit();
	}
	
	if ( is_invaild_filename(fname)|| (file = filesys_open(fname)) == NULL) {
		f->R.rax = -1;
		return;
	}
	
	struct thread *curr = thread_current();
	int fd;
	if (list_empty(&curr->file_list)) {
		fd = 2;
	}
	else {
		fd = (list_entry(list_begin(&curr->file_list), struct fd_info, elem)->fd) + 1;
	}

	struct fd_info *fd_info = malloc(sizeof(struct fd_info));
	fd_info->fd = fd;
	fd_info->my_file = file;
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
	f->R.rax = file_length(fd_info->my_file);
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
	f->R.rax = file_read(fd_info->my_file, buffer, size);
	lock_release(&file_lock);



}

static void
sys_write (struct intr_frame *f) {
	void *  buffer = (void *)f->R.rsi;
	int fd = f->R.rdi;
	uint64_t size = f->R.rdx;

	if (buffer == NULL || is_kernel_vaddr((char *)buffer + size)) {
		thread_current()->exit_status = -1;
		thread_exit(); // 접근하면 안 되는 포인터를 주면 프로세스 종료 
	}
	if (fd == 1) {
		if(size == 0){
			f->R.rax = 0;
		}
		else{
			lock_acquire(&file_lock);
			putbuf(buffer, size);
			f->R.rax = size;
			lock_release(&file_lock);
		}
	}
	else{
		f->R.rax = -1; // write에 실패한 경우 (fd를 찾을 수 없음)
	}
}

static void
sys_close (struct intr_frame *f) {
	int fd = f->R.rdi;

	if ( fd == NULL ) {
		thread_current()->exit_status = -1;
		thread_exit();
	}

	struct thread *curr = thread_current();
	struct list_elem *ptr;
	struct list *list = &curr->file_list;

	for(ptr = list_begin(list); ptr != list_end(list); ptr = list_next(ptr)){
		if(list_entry(ptr, struct fd_info, elem)-> fd == fd){
			struct fd_info *fd_info = list_entry(ptr, struct fd_info, elem);
			list_remove(ptr);
			file_close(fd_info->my_file);
			free(fd_info);
			break;
		}
	}
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