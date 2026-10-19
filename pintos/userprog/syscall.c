#include "userprog/syscall.h"
#include <stdio.h>
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
		case SYS_HALT:{
			power_off();
			break;
		}
		case SYS_EXIT:{
			thread_current()->exit_status = f->R.rdi;
			thread_exit();
			break;	
		}
		case SYS_WAIT:{
			f->R.rax = process_wait(f->R.rdi);
			break;
		}
		case SYS_CREATE: {
			char *fname = (char *)f->R.rdi;
			unsigned int initsize = f->R.rsi;
			
			if (fname == NULL  || is_kernel_vaddr(fname)) {
				thread_current()->exit_status = -1;
				thread_exit();
			}

			if (is_invaild_filename(fname) || *fname == NULL){
				f->R.rax = 0;
				break;
			}
			
			f->R.rax = filesys_create(fname, initsize);

			break;
		}
		case SYS_OPEN:{
			char *fname = (char *)f->R.rdi;
			

			struct file *file;

			if(fname == NULL || is_kernel_vaddr(fname)) {
				thread_current()->exit_status = -1;
				thread_exit();
			}
			
			if ( is_invaild_filename(fname)|| (file = filesys_open(fname)) == NULL) {
				f->R.rax = -1;
				break;
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

			break;
		}
		case SYS_WRITE:{
			void *  buffer = (void *)f->R.rsi;
			int fd = f->R.rdi;
			uint64_t size = f->R.rdx;

			if (buffer == NULL || is_kernel_vaddr((char *)buffer + size)) {
				thread_current()->exit_status = -1;
				thread_exit();
			}
			if (fd == 1) {
				if(size == 0){
					f->R.rax = 0;
				}
				else{
					putbuf(buffer, size);
					f->R.rax = size;
				}
			}
			else{
				f->R.rax = -1; // write에 실패한 경우 (fd를 찾을 수 없음)
			}
			break;
		}
		case SYS_CLOSE:{
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
			break;


		}
		default:
			printf ("system call!\n");
			thread_exit ();
	}
		
}

static bool is_invaild_filename(char * filename){
	size_t size;
	size = strnlen(filename, NAME_MAX+1);
	return !((size > 0) && (size <= (NAME_MAX)));

}