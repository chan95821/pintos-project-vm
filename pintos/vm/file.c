/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/mmu.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;
	struct file_page *file_page = &page->file;
	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Do the mmap */
static bool lazy_load_file(struct page *page, void *aux){
	struct file_src *src = (struct file_src *)aux;
	off_t read = file_read_at(src->file, page->frame->kva, src->read_bytes, src->offset);
	memset(page->frame->kva + read, 0, PGSIZE - read);
	return true;
}

void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	
	struct file * new_file = file_reopen(file);
	struct file_src *file_src_thread = malloc(sizeof(struct file_src));
	// 실패 대응

	for(int read = 0; length > read; read += PGSIZE){
		struct file_src *src = malloc(sizeof(struct file_src));
		if(!src){
			//reopen된 경우는 file 닫기, 
			//그동안 for를 거치면서 만들어진 
			// 1. src free 하기
		}
		src->file = new_file;
		src->offset = offset + read;
		src->read_bytes = length-read>PGSIZE?PGSIZE:length-read;
		src->addr = addr;
		vm_alloc_page_with_initializer(VM_FILE, addr + read, writable, lazy_load_file, src);
	}
	file_src_thread->file = new_file;
	file_src_thread->offset = offset;
	file_src_thread->read_bytes = length;
	file_src_thread->addr = addr;
	list_push_back(&thread_current()->file_src_list, &file_src_thread->element);
	return addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	struct file_src *src = NULL;
	for(struct list_elem *element = list_begin(&thread_current()->file_src_list);
		element != list_tail(&thread_current()->file_src_list);
		element = list_next(element)){
		if(list_entry(element,struct file_src, element)->addr == addr){
			src = list_entry(element,struct file_src, element);
			break;
		}
	}
	ASSERT(src);
	for(int read = 0; src->read_bytes > read; read += PGSIZE){
		struct page *page = spt_find_page(&thread_current()->spt, addr+read);
		if(!page){
			break;
		}
		if(page->frame){
			palloc_free_page(page->frame->kva);
			free(page->frame);
			page->frame = NULL;
		}
		spt_remove_page(&thread_current()->spt, page);
	}
}
