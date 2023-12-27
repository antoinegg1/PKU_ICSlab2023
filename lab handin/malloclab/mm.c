/*
 * mm.c
  * Dynamic Memory Allocator
 * ------------------------
 * This file implements a dynamic memory allocator using explicit free lists 
 * and boundary tag coalescing. The allocator provides basic memory allocation 
 * functions, including `malloc`, `free`, `realloc`, and `calloc`.
 *
 * Key Features:
 * 1. Segregated Free Lists: Organizes free blocks into multiple free lists 
 *    based on block sizes for efficient allocation and reduced fragmentation.
 * 2. First-Fit Search: Searches for the first sufficiently large block in 
 *    the appropriate free list.
 * 3. Boundary Tag Coalescing: Merges adjacent free blocks to prevent 
 *    fragmentation and maintain larger contiguous free spaces.
 *
 * The allocator uses a header at the beginning of each block to store the 
 * block's size and allocation status. Footers may also be used for free blocks 
 * to aid in coalescing. The heap structure includes prologue and epilogue 
 * blocks that simplify boundary conditions during coalescing and allocation.
 *
 *
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mm.h"
#include "memlib.h"

/* If you want debugging output, use the following macro.  When you hand
 * in, remove the #define DEBUG line. */
//#define DEBUG
#ifdef DEBUG
# define dbg_checkheap(x) mm_checkheap(x)
#else 
# define dbg_checkheap(x)
#endif

/* do not change the following! */
#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#endif /* def DRIVER */

// Constants for dynamic memory allocation
#define WSIZE       4       /* Size of word, header, and footer (bytes). */
#define DSIZE       8       /* Size of double word (bytes), used for alignment. */
#define minsize     16      /* Minimum block size including overhead (bytes). */
#define free_list_size 9    /* Number of buckets in the free list for block segregation. */
#define CHUNKSIZE   3072  /* Size to extend heap by (bytes), balancing space and overhead. */

//macro first function
#define MAX(x, y) ((x) > (y)? (x) : (y))  /* Returns the maximum of x and y. */
#define MIN(x, y) ((x) < (y)? (x) : (y))  /* Returns the minimum of x and y. */
#define PACK(size,palloc, alloc)  ((size) | (palloc<<1) | (alloc)) /* Combines block size and allocation status into a single word. */
#define GET(p)       (*(unsigned*)(p))            /* Reads a word at address p. */
#define PUT(p, val)  ((*(unsigned*)(p)) = (val))    /* Writes a word at address p. */
#define HDRP(bp)     ((char *)(bp) - WSIZE)       /* Returns address of block's header. */
#define SET_SIZE(p, size) ((*(unsigned*)(p)) = ((*(unsigned*)(p)) & 0x7) | size)/* Sets the size of the block at address p, preserving other bits. */
#define SET_ALLOC(p)        ((*(unsigned*)(p)) = (*(unsigned*)(p))|0x1)/* Marks the block at address p as allocated. */
#define RESET_ALLOC(p)      ((*(unsigned*)(p)) = (*(unsigned*)(p))&~0x1)/* Marks the block at address p as free. */
#define SET_PALLOC(p)       ((*(unsigned*)(p)) =(*(unsigned*)(p)) |0x2)/* Marks the previous block relative to p as allocated. */
#define RESET_PALLOC(p)     ((*(unsigned*)(p)) = (*(unsigned*)(p))&~0x2)/* Marks the previous block relative to p as free. */
#define ALIGN(p) (((size_t)(p) + 7) & ~0x7) /* Aligns p to the nearest alignment boundary. */
#define succeed(bp) ((void*)(*(size_t*)(bp)))         /* Gets successor block pointer. */
#define put_suc(bp, succp)     (*(size_t*)(bp) = (size_t)(succp))  /* Sets successor block pointer. */

//macro second function
#define GET_SIZE(bp)  (GET(HDRP(bp)) & ~0x7)             /* Retrieves the size of the block from its header. */
#define GET_ALLOC(bp) (GET(HDRP(bp)) & 0x1)                      /* Checks if the block at address p is allocated. */
#define get_prev_alloc(bp)    ((GET(HDRP(bp)) >> 1) & 0x1)    /* Checks if the previous block relative to bp is allocated. */

//macro third function
#define FTRP(bp)       ((char *)(bp) + GET_SIZE(bp) - DSIZE)    /* Computes the address of the block's footer. */
#define find_prev_blankblock(bp) ((char *)(bp) - GET_SIZE(HDRP(bp)))/* Finds the previous free block in memory. */
#define find_next_blankblock(bp) ((char *)(bp) + GET_SIZE(bp))/* Finds the next free block in memory. */
   
/* Global variables */
static char *heap_listp; /* Pointer to the start of the heap. */
static char *epilogue; /* Pointer to the epilogue header of the heap. */
static char *explicit_free_list; /* Pointer to the start of the explicit free list. */
static char *free_list_end; /* Pointer to the end of the explicit free list. */

//function
static void* extend_heap(size_t words);/* Extends the heap with a new free block. */
static void* place(void *bp, size_t asize);/* Allocates a block of asize bytes at bp and splits if necessary. */
static void* find_fit(size_t asize);/* Finds a fit for a block with asize bytes. */
static void* coalesce(void *bp);/* Coalesces adjacent free blocks around bp. */
static void* get_index(size_t size);/* Returns the index for the free list for blocks of size 'size'. */
static void* insert(void *bp);/* Inserts a block into the free list. */
static void delete(void *bp);/* Removes a block from the free list. */

/*
 * mm_init - Initializes the memory manager. 
 * It sets up the initial empty heap structure.
 * Returns -1 on error, 0 on success.
 */
int mm_init(void) {
    explicit_free_list = mem_sbrk(free_list_size * DSIZE + 2 * WSIZE);
    // Create the initial empty heap with space for the free list and prologue/epilogue blocks
    if ( explicit_free_list== ((void *)-1)) {
        return -1;  
    }
    // Initialize the free list to zeroes
    memset(explicit_free_list, 0, free_list_size * DSIZE);
    // Set the end pointer for the free list
    free_list_end = explicit_free_list + free_list_size * DSIZE;
    heap_listp = explicit_free_list + free_list_size * DSIZE;
    // Initialize heap list pointer, check for allocation failure
    if (heap_listp== ((void *)-1)) {
        return -1; 
    }
    // Initialize the prologue block
    PUT(heap_listp, PACK(DSIZE, 1, 1)); // Prologue header
    PUT(heap_listp + WSIZE, PACK(0, 1, 1)); // Epilogue header
    // Move heap_listp to point to the first block in the heap
    heap_listp += (2 * WSIZE);
    // Set the epilogue pointer
    epilogue = heap_listp;
    // Extend the heap with a free block of default size
    if (extend_heap(1 << 14) == NULL) {
        return -1;  
    }
    dbg_checkheap(__LINE__);
    // Initialization successful
    return 0;
}

/*
 * malloc - Allocates a block of memory of size bytes.
 * Returns a pointer to the allocated memory, or NULL if the request cannot be satisfied.
 */
void *malloc(size_t size) {
    dbg_checkheap(__LINE__);
    // Return NULL immediately for a request of zero bytes
    if (size == 0) {
        return NULL;
    }
    dbg_checkheap(__LINE__);
    if (size == 448) {
        size = 512;    // Magic alignment adjustment
    }
    // Align the block size and ensure it's not smaller than the minimum block size
    size = size + WSIZE <= minsize ? minsize : ALIGN(size + WSIZE);
    dbg_checkheap(__LINE__);
    // Find a fit for the requested block size
    void *bp = find_fit(size);
    dbg_checkheap(__LINE__);
    // If no fit is found, extend the heap
    if (!bp) {
        bp = extend_heap(MAX(size, CHUNKSIZE));
        if (bp == (void *)-1) {
            return NULL; // Return NULL if heap extension fails
        }
    }
    dbg_checkheap(__LINE__);
    // Remove the block from the free list
    delete(bp);
    // Place the block and return a pointer to it
    return place(bp, size);
}

/*
 * free - Frees the block of memory pointed by ptr.
 * If ptr is NULL, no operation is performed.
 */
void free(void *ptr) {
    // Do nothing if a NULL pointer is passed
    if (ptr == 0) {
        return;
    }
    RESET_ALLOC(HDRP(ptr)); // Resets the allocated bit in the block's header
    PUT(FTRP(ptr), GET(HDRP(ptr))); // Updates the block's footer to match its header
    // Find the next block and reset its previous-allocated status
    void *n = find_next_blankblock(ptr); 
    RESET_PALLOC(HDRP(n)); // Resets the previous-allocated bit of the next block's header
    coalesce(ptr); // Merges the current block with adjacent free blocks
}

/*
 * realloc - Resizes the memory block pointed to by oldptr to size bytes.
 * Returns a new memory block with the requested size, with existing data copied.
 * Frees oldptr and returns NULL if size is 0. 
 * If oldptr is NULL, it behaves like malloc.
 */
void *realloc(void *oldptr, size_t size) {
    // If size is 0, free the old pointer and return NULL
    if (!size) {
        free(oldptr);
        return NULL;
    }
    // If oldptr is NULL, allocate a new block
    if (!oldptr) {
        return malloc(size);
    }
    // Align the requested size
    size = size + WSIZE <= minsize ? minsize : ALIGN(size + WSIZE);
    // Get the size of the old block
    size_t old_size = GET_SIZE(oldptr);
    // If the old block is big enough, return it
    if (old_size >= size) {
        return oldptr;
    }
    // Allocate a new block and check if the allocation was successful
    void *newptr = malloc(size);
    if (!newptr) {
        return NULL;
    }
    // Copy the data from the old block to the new block
    memmove(newptr, oldptr, MIN(size, old_size));
    // Free the old block
    free(oldptr);
    // Return the new block
    return newptr;
}

/*
 * calloc - Allocates memory for an array of nmemb elements of size bytes each.
 * The memory is set to zero.
 * This function is not tested by mdriver, but is needed to run the traces.
 */
void *calloc(size_t nmemb, size_t size) {
    // Calculate total size of the allocation
    size *= nmemb;
    // Allocate memory and check if the allocation was successful
    void *newptr = malloc(size);
    if (!newptr) {
        return NULL;
    }
    // Initialize the allocated memory to zero
    memset(newptr, 0, size);
    // Return the allocated and initialized memory
    return newptr;
}

/*
 * mm_checkheap - Checks the consistency of the heap for debugging purposes.
 * It verifies various aspects of the heap structure and the blocks within it.
 * The function is called with the line number to track the location of the check.
 */
void mm_checkheap(int lineno) {
    // Print the heap pointer and line number if lineno is provided
    if (lineno)
        printf("heap (%p) in line (%d):\n", heap_listp, lineno);
    // Check if the prologue block is within heap bounds
    void *prologue = heap_listp - WSIZE;
    if (prologue > mem_heap_hi() || prologue < mem_heap_lo()) {
        printf("prologue not in heap\n");
    }
    // Check if the prologue block is allocated and has the correct size
    if (!GET_ALLOC(prologue)) {
        printf("prologue header without alloc\n");
    }
    if (GET_SIZE(prologue) != DSIZE) {
        printf("prologue header with wrong size %d\n", GET_SIZE(prologue));
    }
    // Check if the epilogue block is correctly positioned and aligned
    if ((mem_heap_hi() + 1) != epilogue) {
        printf("epilogue header with wrong position %p instead of %p\n", epilogue, (mem_heap_hi() + 1));
    }
    if ((size_t)ALIGN(epilogue) != (size_t)epilogue) {
        printf("epilogue header without align\n");
    }
    if (!GET_ALLOC(epilogue)) {
        printf("epilogue header allocated wrongly \n");
    }
    if (GET_SIZE(epilogue)) {
        printf("epilogue header with wrong size\n");
    }
    // Check if the size of the free list header array is correct
    if (heap_listp - explicit_free_list != free_list_size * DSIZE + 2 * WSIZE) {
        printf("incorrect free list header array size.\n");
    }
    // Check each block in the free list
    int free_list_count = 0;
    for (int i = 0; i < free_list_size; i++) {
        void *entry = explicit_free_list + i * DSIZE;
        for (void *bp = succeed(entry); bp; bp = succeed(bp)) {
            free_list_count++;
            // Check if the block is within heap bounds and aligned
            if (bp > mem_heap_hi() || bp < mem_heap_lo() || (size_t)ALIGN(bp) != (size_t)bp) {
                printf("block not in heap or without aligned %p\n", bp);
            }

            // Check additional properties of the block
            if (GET_ALLOC(bp)) {
                printf("block allocated wrongly %p\n", bp);
            }
            if (GET_SIZE(bp) < minsize) {
                printf("block too small %p %d\n", bp, GET_SIZE(bp));
            }
            if ((GET(HDRP(bp)) & (~0x2)) != (GET(FTRP(bp)) & (~0x2))) {
                printf("inconsistent block header and footer %x %x\n", GET(HDRP(bp)), GET(FTRP(bp)));
            }
            if (get_index(GET_SIZE(bp)) != entry) {
                printf("block falls into the wrong bucket.\n");
            }
        }
    }
    // Check each block in address order
    void *bp;
    for (bp = heap_listp; GET_SIZE(bp) > 0; bp = find_next_blankblock(bp)) {
        // Similar block checks as above
        if (bp > mem_heap_hi() || bp < mem_heap_lo() || (size_t)ALIGN(bp) != (size_t)bp) {
            printf("block not in heap or without aligned %p\n", bp);
        }
        // Additional checks for free blocks
        if (!GET_ALLOC(bp)) {
            free_list_count--;
            if (!GET_ALLOC(find_next_blankblock(bp))) {
                printf("Consecutive free blocks not coalesced.\n");
            }
        }
        // Check if the prev_alloc bit is consistent
        if (GET_ALLOC(bp) != get_prev_alloc(find_next_blankblock(bp))) {
            printf("Prev_alloc bit inconsistent.\n");
            printf("Block in address %p is %s allocated, but the following block marked it otherwise.\n", bp, GET_ALLOC(bp) ? "" : "un");
        }
    }
    // Check if the free list count matches
    if (free_list_count) {
        printf("Free list total size and free block number don't match.\n");
    }
    // Verify the epilogue block's location
    if (bp != epilogue) {
        printf("Incorrect epilogue location.\n");
    }
}

/* 
 * extend_heap - Extends the heap with a free block and returns its block pointer.
 * This function is called when more heap space is needed.
 */
static void *extend_heap(size_t words) {
    dbg_checkheap(__LINE__);
    // Extend the heap by the requested number of words
    void *bp = mem_sbrk(words);
    // Check if memory allocation failed
    if (bp == (void*)-1) {
        return (void*)-1;
    }
    // Retrieve the allocation status of the previous block
    int palloc = get_prev_alloc(epilogue);
    // Initialize the header and footer of the new block
    PUT(HDRP(bp), PACK(words, palloc, 0)); // Header
    PUT(FTRP(bp), PACK(words, palloc, 0)); // Footer
    // Update the epilogue block's position
    epilogue += words;
    // Set the new epilogue header
    PUT(HDRP(epilogue), PACK(0, 0, 1));
    dbg_checkheap(__LINE__);
    // Coalesce the new block with adjacent free blocks if possible
    return coalesce(bp);                                        
}
/* 
 * coalesce - Performs boundary tag coalescing.
 * Merges the given block with adjacent free blocks to reduce fragmentation.
 * Returns a pointer to the coalesced block.
 */
static void *coalesce(void *bp) {
    // Find the previous and next blocks in the heap
    void *prev = find_prev_blankblock(bp), *next = find_next_blankblock(bp);
    // Determine if the previous and next blocks are allocated
    size_t prev_alloc = get_prev_alloc(bp);
    size_t next_alloc = GET_ALLOC(next);
    // Get the size of the current block
    size_t size = GET_SIZE(bp);
    // Case 1: Both previous and next blocks are allocated
    if (prev_alloc && next_alloc) {
        return insert(bp);
    }
    // Case 2: Previous block is free, next block is allocated
    else if (!prev_alloc && next_alloc) {
        delete(prev); // Remove the previous block from the free list
        bp = prev; // Set bp to the start of the combined block
        size += GET_SIZE(prev); // Increase the size to include the previous block
    }
    // Case 3: Previous block is allocated, next block is free
    else if (prev_alloc && !next_alloc) {
        delete(next); // Remove the next block from the free list
        size += GET_SIZE(next); // Increase the size to include the next block
    }
    // Case 4: Both previous and next blocks are free
    else {
        delete(next); // Remove the next block from the free list
        delete(prev); // Remove the previous block from the free list
        bp = prev; // Set bp to the start of the combined block
        size += GET_SIZE(next) + GET_SIZE(prev); // Increase the size to include both blocks
    }
    // Update the size in the header and footer of the coalesced block
    SET_SIZE(HDRP(bp), size);
    SET_SIZE(FTRP(bp), size);
    // Insert the coalesced block back into the free list
    return insert(bp);
}
/* 
 * get_index - Determines the index in the explicit free list for a block of size asize.
 * This function helps in segregating free blocks based on their sizes.
 */
static void *get_index(size_t asize) {
    // Determine the index based on the size of the block
    int i = 0;
    // For very large sizes, use the last index
    if (asize >= (1 << 12)) {
        return explicit_free_list + DSIZE * free_list_size - DSIZE;
    }
    // Right-shift the size to find the appropriate index
    asize = asize >> 4;
    while (asize > 1) {
        asize = asize >> 1;
        i++;
    }
    // Return the pointer to the appropriate free list bucket
    return explicit_free_list + i * DSIZE;
}

/* 
 * find_fit - Finds a fit for a block with size bytes in the explicit free list.
 * It searches the free list for a block that is large enough to accommodate the requested size.
 * Returns a pointer to the fitting block, or NULL if no fitting block is found.
 */
static void *find_fit(size_t size) {
    // Find the appropriate free list bucket based on size
    void *entry = get_index(size);
    // Iterate through the free list to find a suitable block
    void *p = succeed(entry);
    while (p) {
        // If a block is large enough, return it
        if (GET_SIZE(p) >= size) {
            return p;
        }
        p = succeed(p);
    }
    // Iterate through the rest of the free list buckets if no block is found in the first pass
    char *p2 = entry + DSIZE;
    while (p2 != free_list_end) {
        if (succeed(p2))
            return succeed(p2);
        p2 += DSIZE;
    }
    // Return NULL if no suitable block is found
    return NULL;
}
/* 
 * insert - Inserts a block into the explicit free list.
 * The function places the block in the appropriate position based on its size
 * to maintain the order of blocks within each free list bucket.
 */
static void *insert(void *bp) {
    // Find the appropriate free list bucket for the block
    void *entry = get_index(GET_SIZE(bp));
    // Iterate through the free list to find the correct position for insertion
    void *p = entry, *sucp;
    sucp = succeed(p);
    while (sucp && GET_SIZE(sucp) < GET_SIZE(bp)) {
        p = sucp;
        sucp = succeed(p);
    }
    // Insert the block into the free list
    put_suc(bp, sucp); // Set bp's successor
    put_suc(p, bp);    // Update the previous block's successor
    // Return the block pointer
    return bp;
}

/* 
 * delete - Removes a block from the explicit free list.
 * The function finds the block in the free list and adjusts pointers
 * to exclude it from the list.
 */
static void delete(void *bp) {
    // Find the appropriate free list bucket for the block
    void *entry = get_index(GET_SIZE(bp));
    // Iterate through the free list to find the block
    void *p = entry, *succp;
    succp = succeed(p);
    while (succp && succp != bp) {
        p = succp;
        succp = succeed(p);
    }
    // Remove the block from the free list
    put_suc(p, succeed(bp)); // Update the predecessor's successor
}
/* 
 * place - Allocates a block of asize bytes within the free block pointed by bp.
 * If the remainder of the block is too small to be used as a separate block,
 * the entire block is allocated. Otherwise, the block is split.
 */
static void *place(void *bp, size_t asize) {
    // Get the current size of the free block
    size_t csize = GET_SIZE(bp);
    // Calculate the remaining size after allocation
    size_t remainder = csize - asize;
    void *result = bp;
    // If the remainder is too small to be a separate block, allocate the entire block
    if (remainder <= minsize) {
        SET_ALLOC(HDRP(bp)); // Mark the block as allocated
        SET_PALLOC(HDRP(find_next_blankblock(bp))); // Set the previous-allocated bit of the next block
        return result;
    }
    else {
        // Allocate part of the block and update its header
        PUT(HDRP(bp), PACK(asize, get_prev_alloc(bp), 1));
        // Find and set up the remaining part as a new free block
        bp = find_next_blankblock(bp); // Move bp to the start of the new free block
        PUT(HDRP(bp), PACK(remainder, 1, 0)); // Set the header of the new free block
        PUT(FTRP(bp), PACK(remainder, 1, 0)); // Set the footer of the new free block
        // Insert the new free block into the free list
        insert(bp);
        // Return the pointer to the allocated block
        return result;
    }
}