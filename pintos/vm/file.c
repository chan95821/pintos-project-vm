/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

#define PGSIZE 4096

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

static bool
lazy_load_mmap (struct page *page, void *aux) {
	ASSERT(page->frame != NULL);

	struct file_src *file_meta = aux;
	uint8_t *kva = page->frame->kva;
	uint32_t read_bytes = file_meta->read_bytes;
	uint32_t zero_bytes = PGSIZE - read_bytes;

	bool lock_prev_status = false;
	if(lock_prev_status = lock_held_by_current_thread(&file_lock)) {
		lock_release(&file_lock);
	}

	lock_acquire(&file_lock);
	if(file_read_at(page->file, kva, read_bytes, file_meta->offset) != read_bytes) {
		return false;
	}
	lock_release(&file_lock);

	if(lock_prev_status) {
		lock_acquire(&file_lock);
	}

	memset(kva + read_bytes, 0, zero_bytes); 

	page->file_page->file_src = file_meta;

	return true;
}

static void 
mmap_clean_up_for_error (void){
	return;
}

static void *
mmap_alloc_pages (void *addr, size_t length, int writable,
	struct file *file, off_t offset) {

	for (size_t step = 0; step < length; step += PGSIZE) {
		struct file_src *aux = malloc(sizeof(struct file_src));
		if (!aux) {
			return NULL;
		}
		aux->offset = (offset + step);
		aux->read_bytes = (length - step) < PGSIZE ? (length - step) : PGSIZE;
		aux->file = file;

		if (!vm_alloc_page_with_initializer(VM_FILE, (addr + step), writable, lazy_load_mmap, aux)) {
			free(aux);
			mmap_clean_up_for_error();
			return NULL;
		}
	}
	return addr;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	
	struct file * new_file = file_reopen(file);
	
	if(!mmap_alloc_pages(addr, length, writable, file, offset)) {
		// -> 내부 실패시 cleanup mmap 새로 만들기
		file_close(new_file);
		return NULL;
	}

	return addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
}
