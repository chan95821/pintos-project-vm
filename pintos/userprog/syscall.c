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
		case SYS_EXIT:{
			thread_current()->exit_status = f->R.rdi;
			thread_exit();
			break;	
		}
		case SYS_WRITE:{
			void *  buffer = (void *)f->R.rsi;
			int fd = f->R.rdi;
			uint64_t size = f->R.rdx;

			if( !is_user_vaddr(buffer) || !is_user_vaddr((char *)buffer + size) || buffer == NULL){
				thread_current()->exit_status = -1;
				thread_exit(); // 접근하면 안 되는 포인터를 주면 프로세스 종료 
			}
			if(fd == 1){
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
		case SYS_WAIT:{
			// %rdi == child process id 
			int pid = f->R.rdi;
			

		}
		
		default:
			printf ("system call!\n");
			thread_exit ();
	}
		
}
