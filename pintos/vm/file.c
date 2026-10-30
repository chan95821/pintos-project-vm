/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "lib/string.h"
#include "threads/mmu.h"
#include "filesys/filesys.h"

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

	*file_page = (struct file_page) {
		.file_src = NULL
	};

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
	struct file_page *file_page = &page->file;

	if(page->frame == NULL) {
		// nothing to do.
	} else {
		free(page->frame);
	}
	
	free(file_page->file_src);
}
static bool
lazy_load_mmap (struct page *page, void *aux) {

	struct file_src *file_meta = aux;
	uint8_t *kva = page->frame->kva;
	uint32_t read_bytes = file_meta->read_bytes;
	off_t read;

	bool lock_prev_status = false;
	if(lock_prev_status = lock_held_by_current_thread(&file_lock)) {
		lock_release(&file_lock);
	}

	lock_acquire(&file_lock);
	read = file_read_at(file_meta->file, kva, read_bytes, file_meta->offset);
	lock_release(&file_lock);

	if(lock_prev_status) {
		lock_acquire(&file_lock);
	}

	memset(kva + read, 0, PGSIZE - read); 

	page->file.file_src = file_meta;

	return true;
}

static void 
mmap_clean_up_for_error (int idx, void *addr){
	struct supplemental_page_table *spt = &thread_current ()->spt;
	for(int i = 0; i < idx; i++) {
		spt_remove_page(spt, spt_find_page(spt, addr + i * PGSIZE));
	}
}

static void *
mmap_alloc_pages (void *addr, size_t length, int writable,
	struct file *file, off_t offset) {
	
	uint64_t idx = 0;	
	for (size_t step = 0; step < length; step += PGSIZE, idx++) {
		struct file_src *aux = malloc(sizeof(struct file_src));
		if (!aux) {
			mmap_clean_up_for_error(idx, addr);
			return NULL;
		}
		aux->offset = (offset + step);
		aux->read_bytes = (length - step) < PGSIZE ? (length - step) : PGSIZE;
		aux->file = file;

		if (!vm_alloc_page_with_initializer(VM_FILE, (addr + step), writable, lazy_load_mmap, aux)) {
			free(aux);
			mmap_clean_up_for_error(idx, addr);
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
	if(new_file == NULL) {
		return NULL;
	}
	
	if(!mmap_alloc_pages(addr, length, writable, new_file, offset)) {
		file_close(new_file);
		return NULL;
	}

	// set_mmap_info(struct thread *, struct file *, void *addr, size_t length);
	return addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	// struct mmap_info * find_mmap_info(thread, addr);

	// destroy_mmap(struct mmap_info *);
}


destroy_mmap (mmap_info)  {
	// mmap_info가 뭘 알려주나 - file 구조체 ref., mmap 등록한 addr, length

	    // mmap_info가 가리키는 모든 페이지 => 
        // {
        //     spt_remove_page()
        // 해당 페이지 cleanup
        //     페이지 지우기 : pml4 매핑 없애고, frame도 palloc free, 구조체 자체 - frame, page … etc
        //         => destroy(page) 재활용 할 수 있을듯 
        // }
}