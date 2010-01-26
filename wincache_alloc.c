/*
   +----------------------------------------------------------------------------------------------+
   | Windows Cache for PHP                                                                        |
   +----------------------------------------------------------------------------------------------+
   | Copyright (c) 2009, Microsoft Corporation. All rights reserved.                              |
   |                                                                                              |
   | Redistribution and use in source and binary forms, with or without modification, are         |
   | permitted provided that the following conditions are met:                                    |
   | - Redistributions of source code must retain the above copyright notice, this list of        |
   | conditions and the following disclaimer.                                                     |
   | - Redistributions in binary form must reproduce the above copyright notice, this list of     |
   | conditions and the following disclaimer in the documentation and/or other materials provided |
   | with the distribution.                                                                       |
   | - Neither the name of the Microsoft Corporation nor the names of its contributors may be     |
   | used to endorse or promote products derived from this software without specific prior written|
   | permission.                                                                                  |
   |                                                                                              |
   | THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS  |
   | OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF              |
   | MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE   |
   | COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,    |
   | EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE|
   | GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED   |
   | AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING    |
   | NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED |
   | OF THE POSSIBILITY OF SUCH DAMAGE.                                                           |
   +----------------------------------------------------------------------------------------------+
   | Module: wincache_alloc.c                                                                     |
   +----------------------------------------------------------------------------------------------+
   | Author: Kanwaljeet Singla <ksingla@microsoft.com>                                            |
   +----------------------------------------------------------------------------------------------+
*/

#include "precomp.h"

#define BLOCK_ISFREE_FIRST             (unsigned int)'iiii'
#define BLOCK_ISFREE_FREE              (unsigned int)'ffff'
#define BLOCK_ISFREE_USED              (unsigned int)'uuuu'
#define BLOCK_ISFREE_LAST              (unsigned int)'llll'

#define ALLOC_SEGMENT_HEADER_SIZE      (sizeof(alloc_segment_header))
#define ALLOC_FREE_BLOCK_OVERHEAD      (sizeof(alloc_free_header) + sizeof(size_t))
#define ALLOC_USED_BLOCK_OVERHEAD      (sizeof(alloc_used_header) + sizeof(size_t))

#define POINTER_OFFSET(baseaddr, p)    ((char *)p - (char *)baseaddr)
#define FREE_HEADER(baseaddr, offset)  (alloc_free_header *)((char *)baseaddr + offset)
#define USED_HEADER(baseaddr, offset)  (alloc_used_header *)((char *)baseaddr + offset)
#define MIN_FREE_BLOCK_SIZE            (ALLOC_FREE_BLOCK_OVERHEAD + 4)

#define POOL_ALLOCATION_TYPE_UNKNOWN   0
#define POOL_ALLOCATION_TYPE_SMALL     1
#define POOL_ALLOCATION_TYPE_MEDIUM    2
#define POOL_ALLOCATION_TYPE_LARGE     3
#define POOL_ALLOCATION_TYPE_HUGE      4

#define POOL_ALLOCATION_SMALL_MAX      128
#define POOL_ALLOCATION_MEDIUM_MAX     256
#define POOL_ALLOCATION_LARGE_MAX      1024

#define POOL_ALLOCATION_SMALL_BSIZE    512
#define POOL_ALLOCATION_MEDIUM_BSIZE   1024
#define POOL_ALLOCATION_LARGE_BSIZE    4096

/* Globals */
unsigned short gallocid = 1;

/* Private methods */
static int    allocate_memory(alloc_context * palloc, size_t size, void ** ppaddr);
static void   free_memory(alloc_context * palloc, void * paddr);
static size_t get_memory_size(alloc_context * palloc, void * addr);

static void * alloc_malloc(alloc_context * palloc, unsigned int type, size_t size);
static void * alloc_realloc(alloc_context * palloc, unsigned int type, void * addr, size_t size);
static char * alloc_strdup(alloc_context * palloc, unsigned int type, const char * str);
static void   alloc_free(alloc_context * palloc, unsigned int type, void * addr);

static int allocate_memory(alloc_context * palloc, size_t size, void ** ppaddr)
{
    int                    result  = NONFATAL;
    void *                 paddr   = NULL;
    void *                 segaddr = NULL;
    unsigned char          flock   = 0;

    size_t                 oldsize = 0;
    size_t                 oldprev = 0;
    size_t                 oldnext = 0;

    alloc_segment_header * header  = NULL;
    alloc_free_header *    freeh   = NULL;
    alloc_free_header *    pfree   = NULL;
    alloc_used_header *    usedh   = NULL;

    dprintdecorate("start allocate_memory");

    _ASSERT(palloc         != NULL);
    _ASSERT(palloc->rwlock != NULL);
    _ASSERT(palloc->header != NULL);
    _ASSERT(size           >= 0);

    /* If localheap should be used, use HeapAlloc */
    if(palloc->localheap == 1)
    {
        *ppaddr = HeapAlloc(GetProcessHeap(), 0, size);
        goto Finished;
    }

    /* Allocate memory from memory segment shared with other processes */
    segaddr = palloc->memaddr;
    header  = palloc->header;

    /* Allocate in sizes which are multiples of 4 */
    size = ALIGNDWORD(size);

    /* Acquire write lock to segment before doing anything */
    lock_writelock(palloc->rwlock);
    flock = 1;

    /* If available size is less than requested size, return NULL */
    if(header->free_size < size)
    {
        result = FATAL_ALLOC_NO_MEMORY;
        goto Finished;
    }

    /* Get pointer to start block and move to next block */
    freeh = FREE_HEADER(segaddr, sizeof(alloc_segment_header));
    _ASSERT(freeh->is_free == BLOCK_ISFREE_FIRST);

    freeh = FREE_HEADER(segaddr, freeh->next_free);
    _ASSERT(freeh->is_free == BLOCK_ISFREE_FREE || freeh->is_free == BLOCK_ISFREE_LAST);

    /* Find a free block which is large enough to satisfy the request */
    while(freeh->is_free != BLOCK_ISFREE_LAST)
    {
        _ASSERT(freeh->is_free == BLOCK_ISFREE_FREE);
        if(freeh->size >= size)
        {
            break;
        }

        freeh = FREE_HEADER(segaddr, freeh->next_free);
    }

    /* If we didn't find a block big enough, return error */
    if(freeh->is_free == BLOCK_ISFREE_LAST)
    {
        result = FATAL_ALLOC_NO_MEMORY;
        goto Finished;
    }

    _ASSERT(freeh->is_free == BLOCK_ISFREE_FREE);

    /* Got a free block with sufficient free memory. Now */
    /* see if block size is big enough to be broken into two */
    if(freeh->size >= size + MIN_FREE_BLOCK_SIZE)
    {
        oldsize = freeh->size;
        oldprev = freeh->prev_free;
        oldnext = freeh->next_free;

        usedh = (alloc_used_header *)freeh;
        usedh->size    = size;
        usedh->is_free = BLOCK_ISFREE_USED;
        usedh->dummy1  = 0;
        usedh->dummy2  = 0;

        /* Return pointer right after the used header */
        paddr = (void *)(usedh + 1);

        /* Put the size of allocated block in the end */
        *((size_t *)((char *)paddr + size)) = size;

        /* Create a new free header after the allocated block */
        freeh = (alloc_free_header *)((char *)usedh + size + ALLOC_USED_BLOCK_OVERHEAD);
        freeh->size      = oldsize - size - ALLOC_USED_BLOCK_OVERHEAD;
        freeh->is_free   = BLOCK_ISFREE_FREE;
        freeh->prev_free = oldprev;
        freeh->next_free = oldnext;
        *((size_t *)(((char *)(freeh + 1))+ freeh->size)) = freeh->size;

        pfree = FREE_HEADER(segaddr, freeh->prev_free);
        pfree->next_free = POINTER_OFFSET(segaddr, freeh);
        pfree = FREE_HEADER(segaddr, freeh->next_free);
        pfree->prev_free = POINTER_OFFSET(segaddr, freeh);

        /* Update free_size in segment header */
        header->free_size  -= (size + ALLOC_USED_BLOCK_OVERHEAD);

        /* Number of free blocks are not decreasing in this case */
        header->usedcount++;
    }
    else
    {
        /* Block is very small and should be used completely */
        pfree = FREE_HEADER(segaddr, freeh->prev_free);
        pfree->next_free = freeh->next_free;

        pfree = FREE_HEADER(segaddr, freeh->next_free);
        pfree->prev_free = freeh->prev_free;

        /* Update size to indicate number of bytes we are actually allocating */
        size = freeh->size;

        /* Set used header and last two size_t bytes */
        usedh = (alloc_used_header *)freeh;
        usedh->size    = size;
        usedh->is_free = BLOCK_ISFREE_USED;
        usedh->dummy1  = 0;
        usedh->dummy2  = 0;

        /* Return pointer right after the used header */
        paddr = (void *)(usedh + 1);

        /* Put the size of allocated block in the end */
        *((size_t *)((char *)paddr + size)) = size;

        header->free_size  -= size;

        /* One free block got converted to used block */
        header->freecount--;
        header->usedcount++;
    }

    *ppaddr = paddr;

Finished:

    if(flock)
    {
        lock_writeunlock(palloc->rwlock);
        flock = 0;
    }

    if(FAILED(result))
    {
        _ASSERT(*ppaddr == NULL);

        dprintimportant("failure %d in allocate_memory", result);
        _ASSERT(result > WARNING_COMMON_BASE);
    }

    dprintdecorate("end allocate_memory");

    return result;
}

static void free_memory(alloc_context * palloc, void * paddr)
{
    int             combined = 0;
    size_t          blocksiz = 0;
    size_t          lastsize = 0;
    size_t          coffset  = 0;
    void *          segaddr  = NULL;
    unsigned char   flock    = 0;

    alloc_segment_header * header = NULL;
    alloc_used_header *    usedh  = NULL;
    alloc_free_header *    freeh  = NULL;
    alloc_free_header *    pfree  = NULL;

    dprintdecorate("start free_memory");

    _ASSERT(palloc != NULL);
    _ASSERT(paddr  != NULL);

    /* If localheap is used, use HeapFree to free memory */
    if(palloc->localheap == 1)
    {
        HeapFree(GetProcessHeap(), 0, paddr);
        goto Finished;
    }

    /* Free the block from shared segment */
    lock_writelock(palloc->rwlock);
    flock = 1;

    header  = palloc->header;
    segaddr = palloc->memaddr;

    /* Increment freecount and decrement usedcount right away */
    header->usedcount--;
    header->freecount++;

    /* Get pointer to used_header for the block which is getting freed */
    usedh = (alloc_used_header *)((char *)paddr - sizeof(alloc_used_header));
    _ASSERT(usedh->is_free == BLOCK_ISFREE_USED);

    blocksiz = usedh->size;
    header->free_size += blocksiz;

    /* Check if next block is free */
    freeh = (alloc_free_header *)((char *)usedh + blocksiz + ALLOC_USED_BLOCK_OVERHEAD);
    if(freeh->is_free == BLOCK_ISFREE_FREE)
    {
        /* Block after this block is free. Need to combine */
        pfree = (alloc_free_header *)usedh;

        pfree->size      = freeh->size + ALLOC_FREE_BLOCK_OVERHEAD + blocksiz;
        pfree->is_free   = BLOCK_ISFREE_FREE;
        pfree->prev_free = freeh->prev_free;
        pfree->next_free = freeh->next_free;

        coffset = POINTER_OFFSET(segaddr, pfree);
        (FREE_HEADER(segaddr, pfree->prev_free))->next_free = coffset;
        (FREE_HEADER(segaddr, pfree->next_free))->prev_free = coffset;
        *((size_t *)(((char *)(pfree + 1)) + pfree->size)) = pfree->size;

        header->freecount--;
        header->free_size  += ALLOC_FREE_BLOCK_OVERHEAD;

        combined = 1;
    }

    lastsize = *((size_t *)((char *)paddr - sizeof(alloc_used_header) - sizeof(size_t)));
    freeh    = (alloc_free_header *)((char *)paddr - sizeof(alloc_used_header) - ALLOC_FREE_BLOCK_OVERHEAD - lastsize);

    _ASSERT(freeh->size == lastsize);
    if(freeh->is_free == BLOCK_ISFREE_FREE)
    {
        /* Block before block getting freed is free. Combine with that */
        if(combined == 0)
        {
            /* Just need to expand the size of this block */
            freeh->size += usedh->size + ALLOC_USED_BLOCK_OVERHEAD;
        }
        else
        {
            /* We already combined next block with block getting freed */
            pfree = (alloc_free_header *)usedh;
            freeh->size += pfree->size + ALLOC_FREE_BLOCK_OVERHEAD;

            freeh->next_free = pfree->next_free;
            pfree = FREE_HEADER(segaddr, pfree->next_free);
            pfree->prev_free = POINTER_OFFSET(segaddr, freeh);
        }

        *((size_t *)(((char *)(freeh + 1)) + freeh->size)) = freeh->size;

        header->freecount--;
        header->free_size  += ALLOC_FREE_BLOCK_OVERHEAD;

        combined = 1;
    }

    if(combined == 0)
    {
        /* New free block around two used blocks or around */
        /* used and first block or around used and last block */
        /* Find closest free block or first/last if closest */
        usedh = (alloc_used_header *)((char *)paddr - sizeof(alloc_used_header));
        freeh = (alloc_free_header *)((char *)usedh + usedh->size + ALLOC_USED_BLOCK_OVERHEAD);
        while(freeh->is_free == BLOCK_ISFREE_USED)
        {
            /* Keep moving ahead unless free or end block is not found */
            freeh = (alloc_free_header *)((char *)freeh + freeh->size + ALLOC_USED_BLOCK_OVERHEAD);
        }

        _ASSERT(freeh->is_free == BLOCK_ISFREE_FREE || freeh->is_free == BLOCK_ISFREE_LAST);

        pfree = (alloc_free_header *)usedh;
        pfree->is_free = BLOCK_ISFREE_FREE;
        pfree->next_free = POINTER_OFFSET(segaddr, freeh);
        pfree->prev_free = freeh->prev_free;

        freeh = FREE_HEADER(segaddr, pfree->prev_free);
        freeh->next_free = POINTER_OFFSET(segaddr, pfree);

        freeh = FREE_HEADER(segaddr, pfree->next_free);
        freeh->prev_free = POINTER_OFFSET(segaddr, pfree);
    }

Finished:

    if(flock)
    {
        lock_writeunlock(palloc->rwlock);
        flock = 0;
    }
    
    dprintdecorate("end free_memory");

    return;
}

static size_t get_memory_size(alloc_context * palloc, void * addr)
{
    size_t              size  = 0;
    alloc_used_header * usedh = NULL;

    dprintverbose("start get_memory_size");

    _ASSERT(palloc != NULL);
    _ASSERT(addr   != NULL);

    lock_readlock(palloc->rwlock);

    usedh = (alloc_used_header *)((char *)addr - sizeof(alloc_used_header));
    _ASSERT(usedh->is_free == BLOCK_ISFREE_USED);

    size = usedh->size;
    lock_readunlock(palloc->rwlock);

    dprintverbose("end get_memory_size");
    return size;
}

static void * alloc_malloc(alloc_context * palloc, unsigned int type, size_t size)
{
    void * result = NULL;

    _ASSERT(size >= 0);

    switch(type)
    {
        case ALLOC_TYPE_SHAREDMEM:
            if(FAILED(allocate_memory(palloc, size, &result)))
            {
                return NULL;
            }
            break;
        case ALLOC_TYPE_PROCESS:
            result = pemalloc(size, 0);
            break;
        case ALLOC_TYPE_PROCESS_PERSISTENT:
            result = pemalloc(size, 1);
            break;
        default:
            _ASSERT(FALSE);
            break;
    }

    return result;
}

static void * alloc_realloc(alloc_context * palloc, unsigned int type, void * addr, size_t size)
{
    void * result = NULL;
    size_t osize  = 0;

    _ASSERT(addr != NULL);
    _ASSERT(size >= 0);

    switch(type)
    {
        case ALLOC_TYPE_SHAREDMEM:
            if(FAILED(allocate_memory(palloc, size, &result)))
            {
                return NULL;
            }
            osize = get_memory_size(palloc, addr);
            if(size < osize)
            {
                osize = size;
            }
            memcpy_s(result, osize, addr, osize);
            free_memory(palloc, addr);
            break;
        case ALLOC_TYPE_PROCESS:
            result = perealloc(addr, size, 0);
            break;
        case ALLOC_TYPE_PROCESS_PERSISTENT:
            result = perealloc(addr, size, 1);
            break;
        default:
            _ASSERT(FALSE);
            break;
    }

    return result;
}

static char * alloc_strdup(alloc_context * palloc, unsigned int type, const char * str)
{
    char * result = NULL;
    int    strl   = 0;
    
    _ASSERT(str != NULL);
    strl = strlen(str) + 1;

    switch(type)
    {
        case ALLOC_TYPE_SHAREDMEM:
            if(FAILED(allocate_memory(palloc, strl, &result)))
            {
                return NULL;
            }
            memcpy_s(result, strl, str, strl);
            break;
        case ALLOC_TYPE_PROCESS:
            result = pestrdup(str, 0);
            break;
        case ALLOC_TYPE_PROCESS_PERSISTENT:
            result = pestrdup(str, 1);
            break;
        default:
            _ASSERT(FALSE);
            break;
    }

    return result;
}

static void alloc_free(alloc_context * palloc, unsigned int type, void * addr)
{
    _ASSERT(addr != NULL);

    switch(type)
    {
        case ALLOC_TYPE_SHAREDMEM:
            free_memory(palloc, addr);
            break;
        case ALLOC_TYPE_PROCESS:
            pefree(addr, 0);
            break;
        case ALLOC_TYPE_PROCESS_PERSISTENT:
            pefree(addr, 1);
            break;
        default:
            _ASSERT(FALSE);
            break;
    }
    
    return;
}

int alloc_create(alloc_context ** ppalloc)
{
    int             result = NONFATAL;
    alloc_context * palloc = NULL;

    dprintverbose("start alloc_create");
    _ASSERT(ppalloc != NULL);

    *ppalloc = NULL;

    /* Allocate memory for context */
    palloc = (alloc_context *)alloc_pemalloc(sizeof(alloc_context));
    if(palloc == NULL)
    {
        result = FATAL_OUT_OF_LMEMORY;
        goto Finished;
    }

    /* Initialize member variables */
    palloc->id        = gallocid++;
    palloc->islocal   = 0;
    palloc->hinitdone = NULL;
    palloc->memaddr   = NULL;
    palloc->size      = 0;
    palloc->rwlock    = NULL;
    palloc->header    = NULL;
    palloc->localheap = 0;

    *ppalloc = palloc;

Finished:

    if(FAILED(result))
    {
        dprintimportant("failure %d in alloc_create", result);
        _ASSERT(result > WARNING_COMMON_BASE);
    }

    dprintverbose("end alloc_create");
    return result;
}

void alloc_destroy(alloc_context * palloc)
{
    dprintverbose("start alloc_destroy");

    if(palloc != NULL)
    {
        alloc_pefree(palloc);
        palloc = NULL;
    }

    dprintverbose("end alloc_destroy");

    return;
}

int alloc_initialize(alloc_context * palloc, unsigned short islocal, char * name, void * staddr, size_t size TSRMLS_DC)
{
    int                    result   = NONFATAL;
    unsigned short         locktype = LOCK_TYPE_SHARED;
    alloc_segment_header * header   = NULL;
    alloc_free_header *    frestart = NULL;
    alloc_free_header *    fremid   = NULL;
    alloc_free_header *    freend   = NULL;
    unsigned char          isfirst  = 1;
    char                   evtname[   MAX_PATH];

    dprintverbose("start alloc_initialize");

    _ASSERT(palloc != NULL);
    _ASSERT(staddr != NULL);
    _ASSERT(size   >  0);

    palloc->localheap = WCG(localheap);
    palloc->memaddr   = staddr;
    palloc->size      = size;
    palloc->header    = (alloc_segment_header *)staddr;
    header            = palloc->header;

    /* Create shared, xread, xwrite lock as all operations */
    /* on allocator will end up changing segment information */
    result = lock_create(&palloc->rwlock);
    if(FAILED(result))
    {
        goto Finished;
    }

    if(islocal)
    {
        locktype = LOCK_TYPE_LOCAL;
        palloc->islocal = islocal;
    }

    result = lock_initialize(palloc->rwlock, name, 0, locktype, LOCK_USET_XREAD_XWRITE, NULL TSRMLS_CC);
    if(FAILED(result))
    {
        goto Finished;
    }

    result = lock_getnewname(palloc->rwlock, "ALLOC_INIT", evtname, MAX_PATH);
    if(FAILED(result))
    {
        goto Finished;
    }

    palloc->hinitdone = CreateEvent(NULL, TRUE, FALSE, evtname);
    if(palloc->hinitdone == NULL)
    {
        result = FATAL_ALLOC_INIT_EVENT;
        goto Finished;
    }

    if(GetLastError() == ERROR_ALREADY_EXISTS)
    {
        _ASSERT(islocal == 0);
        isfirst = 0;

        /* Wait for other process to initialize completely */
        WaitForSingleObject(palloc->hinitdone, INFINITE);
    }

    /* If the segment is not initialized, set */
    /* header on the shared memory segment start */
    if(islocal || isfirst)
    {
        lock_writelock(palloc->rwlock);

        /* Put the start header of size 0 */
        /* followed by the first free block of size free_size */
        frestart = (alloc_free_header *)(header + 1);
        fremid   = (alloc_free_header *)((char *)frestart + ALLOC_FREE_BLOCK_OVERHEAD);

        /* Set start of doubly circular linked list */
        frestart->size      = 0;
        frestart->is_free   = BLOCK_ISFREE_FIRST;
        frestart->prev_free = 0;
        frestart->next_free = POINTER_OFFSET(staddr, fremid);
        *((size_t *)((char *)(frestart + 1))) = frestart->size;

        /* Set mid of doubly linked list */
        fremid->size      = size - ALLOC_SEGMENT_HEADER_SIZE - ALLOC_FREE_BLOCK_OVERHEAD - ALLOC_FREE_BLOCK_OVERHEAD - ALLOC_FREE_BLOCK_OVERHEAD;
        freend   = (alloc_free_header *)((char *)fremid + ALLOC_FREE_BLOCK_OVERHEAD + fremid->size);

        fremid->is_free   = BLOCK_ISFREE_FREE;
        fremid->prev_free = POINTER_OFFSET(staddr, frestart);
        fremid->next_free = POINTER_OFFSET(staddr, freend);
        *((size_t *)(((char *)(fremid + 1)) + fremid->size)) = fremid->size;

        freend->size      = 0;
        freend->is_free   = BLOCK_ISFREE_LAST;
        freend->next_free = 0;
        freend->prev_free = POINTER_OFFSET(staddr, fremid);
        *((size_t *)((char *)(freend + 1))) = freend->size;

        /* Set segment header */
        header->mapcount   = 1;
        header->total_size = size;
        header->free_size  = fremid->size;

        header->cacheheader = 0;
        header->usedcount   = 0;
        header->freecount   = 1;

        SetEvent(palloc->hinitdone);

        lock_writeunlock(palloc->rwlock);
    }
    else
    {
        InterlockedIncrement(&header->mapcount);
    }

    _ASSERT(SUCCEEDED(result));

Finished:

    if(FAILED(result))
    {
        dprintimportant("failure %d in alloc_initialize", result);
        _ASSERT(result > WARNING_COMMON_BASE);

        if(palloc != NULL)
        {
            if(palloc->rwlock != NULL)
            {
                lock_terminate(palloc->rwlock);
                lock_destroy(palloc->rwlock);

                palloc->rwlock = NULL;
            }

            if(palloc->hinitdone != NULL)
            {
                CloseHandle(palloc->hinitdone);
                palloc->hinitdone = NULL;
            }

            palloc->memaddr = NULL;
            palloc->size    = 0;
            palloc->header  = NULL;
        }
    }

    dprintverbose("end alloc_initialize");

    return result;
}

void alloc_terminate(alloc_context * palloc)
{ 
    dprintverbose("start alloc_terminate");

    if(palloc != NULL)
    {
        if(palloc->header != NULL)
        {
            InterlockedDecrement(&palloc->header->mapcount);
            palloc->header = NULL;
        }

        if(palloc->rwlock != NULL)
        {
            lock_terminate(palloc->rwlock);
            lock_destroy(palloc->rwlock);

            palloc->rwlock = NULL;
        }

        if(palloc->hinitdone != NULL)
        {
            CloseHandle(palloc->hinitdone);
            palloc->hinitdone = NULL;
        }

        palloc->memaddr = NULL;
        palloc->size    = 0;
    }

    dprintverbose("end alloc_terminate");

    return;
}

int alloc_create_mpool(alloc_context * palloc, size_t * phoffset)
{
    int                   result = NONFATAL;
    alloc_mpool_header *  header = NULL;

    dprintverbose("start alloc_create_mpool");

    _ASSERT(palloc   != NULL);
    _ASSERT(phoffset != NULL);

    *phoffset = 0;

    header = (alloc_mpool_header *)alloc_smalloc(palloc, sizeof(alloc_mpool_header));
    if(header == NULL)
    {
        result = FATAL_OUT_OF_MEMORY;
        goto Finished;
    }
    
    header->foffset = 0;
    header->loffset = 0;
    header->socurr  = 0;
    header->mocurr  = 0;
    header->locurr  = 0;

    *phoffset = POINTER_OFFSET(palloc->memaddr, header);

Finished:

    if(FAILED(result))
    {
        dprintimportant("failure %d in alloc_create_mpool", result);
        _ASSERT(result > WARNING_COMMON_BASE);
    }

    dprintverbose("end alloc_create_mpool");

    return result;
}

void alloc_free_mpool(alloc_context * palloc, size_t hoffset)
{
    void *                pvoid  = NULL;
    alloc_mpool_header *  header = NULL;
    alloc_mpool_segment * pchunk = NULL;
    alloc_mpool_segment * ptemp  = NULL;

    dprintverbose("start alloc_free_mpool");

    _ASSERT(palloc  != NULL);
    _ASSERT(hoffset >  0);
 
    header = (alloc_mpool_header *)((char *)palloc->memaddr + hoffset);
    if(header->foffset > 0)
    {
        pchunk = (alloc_mpool_segment *)((char *)palloc->memaddr + header->foffset);
        while(pchunk != NULL)
        {
            ptemp = pchunk;
            pchunk = ((pchunk->next == 0) ? NULL : (alloc_mpool_segment *)((char *)palloc->memaddr + pchunk->next));

            if(ptemp->aoffset != 0)
            {
                pvoid = (void *)((char *)palloc->memaddr + ptemp->aoffset);

                alloc_sfree(palloc, pvoid);
                ptemp->aoffset = 0;
            }

            alloc_sfree(palloc, ptemp);
            ptemp = NULL;
        }
    }

    alloc_sfree(palloc, header);
    header = NULL;

    dprintverbose("end alloc_free_mpool");
    return;
}

void * alloc_get_cacheheader(alloc_context * palloc, unsigned int valuecount, unsigned int type)
{
    void *                 pvoid  = NULL;
    alloc_segment_header * header = NULL;
    unsigned int           msize  = 0;

    dprintverbose("start alloc_get_cacheheader");

    _ASSERT(palloc != NULL);
    header = palloc->header;

    if(header->cacheheader == 0)
    {
        /* Memory for cache header is not allocated yet */
        if(type == CACHE_TYPE_FILELIST)
        {
            _ASSERT(valuecount >  0);
            msize = sizeof(aplist_header) + (valuecount - 1) * sizeof(size_t);
        }
        else if(type == CACHE_TYPE_RESPATHS)
        {
            _ASSERT(valuecount > 0);
            msize = sizeof(rplist_header) + (valuecount - 1) * sizeof(size_t);
        }
        else if(type == CACHE_TYPE_FILECONTENT)
        {
            _ASSERT(valuecount == 0);
            msize = sizeof(fcache_header);
        }
        else if(type == CACHE_TYPE_BYTECODES)
        {
            _ASSERT(valuecount == 0);
            msize = sizeof(ocache_header);
        }
        else
        {
            _ASSERT(FALSE);
        }

        if(FAILED(allocate_memory(palloc, msize, &pvoid)))
        {
            pvoid = NULL;
            goto Finished;
        }

        lock_writelock(palloc->rwlock);
        if(header->cacheheader != 0)
        {
            /* Some other process allocated before this process could do */
            lock_writeunlock(palloc->rwlock);
            free_memory(palloc, pvoid);
            pvoid = (void *)((char *)palloc->memaddr + header->cacheheader);
        }
        else
        {
            header->cacheheader = POINTER_OFFSET(palloc->memaddr, pvoid);
            lock_writeunlock(palloc->rwlock);
        }
    }
    else
    {
        pvoid = (void *)((char *)palloc->memaddr + header->cacheheader);
    }

Finished:

    dprintverbose("end alloc_get_cacheheader");
    return pvoid;
}

__inline
void * alloc_get_cachevalue(alloc_context * palloc, size_t offset)
{
    _ASSERT(palloc != NULL);

    if(offset != 0)
    {
        return (void *)((char *)palloc->memaddr + offset);
    }
    else
    {
        return NULL;
    }
}

__inline
size_t alloc_get_valueoffset(alloc_context * palloc, void * pvalue)
{
    _ASSERT(palloc != NULL);
    
    if(pvalue != NULL)
    {
        return POINTER_OFFSET(palloc->memaddr, pvalue);
    }
    else
    {
        return 0;
    }
}

int alloc_getinfo(alloc_context * palloc, alloc_info ** ppinfo)
{
    int          result = NONFATAL;
    alloc_info * pinfo  = NULL;

    dprintverbose("start alloc_getinfo");

    _ASSERT(palloc != NULL);
    _ASSERT(ppinfo != NULL);

    *ppinfo = NULL;

    pinfo = (alloc_info *)alloc_emalloc(sizeof(alloc_info));
    if(pinfo == NULL)
    {
        result = FATAL_OUT_OF_LMEMORY;
        goto Finished;
    }

    lock_readlock(palloc->rwlock);

    pinfo->total_size   = palloc->header->total_size;
    pinfo->free_size    = palloc->header->free_size;
    pinfo->usedcount    = palloc->header->usedcount;
    pinfo->freecount    = palloc->header->freecount;
    pinfo->mem_overhead = (palloc->header->usedcount + palloc->header->freecount + 2) * sizeof(alloc_free_header);

    lock_readunlock(palloc->rwlock);

    *ppinfo = pinfo;
    _ASSERT(SUCCEEDED(result));

Finished:

    if(FAILED(result))
    {
        dprintimportant("failure %d in alloc_getinfo", result);
        _ASSERT(result > WARNING_COMMON_BASE);

        if(pinfo != NULL)
        {
            alloc_efree(pinfo);
            pinfo = NULL;
        }
    }

    dprintverbose("end alloc_getinfo");

    return result;
}

void alloc_freeinfo(alloc_info * pinfo)
{
    if(pinfo != NULL)
    {
        alloc_efree(pinfo);
        pinfo = NULL;
    }
}

void * alloc_emalloc(size_t size)
{
    return alloc_malloc(NULL, ALLOC_TYPE_PROCESS, size);
}

void * alloc_pemalloc(size_t size)
{
    return alloc_malloc(NULL, ALLOC_TYPE_PROCESS_PERSISTENT, size);
}

void * alloc_smalloc(alloc_context * palloc, size_t size)
{
    _ASSERT(palloc != NULL);
    return alloc_malloc(palloc, ALLOC_TYPE_SHAREDMEM, size);
}

void * alloc_erealloc(void * addr, size_t size)
{
    return alloc_realloc(NULL, ALLOC_TYPE_PROCESS, addr, size);
}

void * alloc_perealloc(void * addr, size_t size)
{
    return alloc_realloc(NULL, ALLOC_TYPE_PROCESS_PERSISTENT, addr, size);
}

void * alloc_srealloc(alloc_context * palloc, void * addr, size_t size)
{
    _ASSERT(palloc != NULL);
    return alloc_realloc(palloc, ALLOC_TYPE_SHAREDMEM, addr, size);
}

char * alloc_estrdup(const char * str)
{
    return alloc_strdup(NULL, ALLOC_TYPE_PROCESS, str);
}

char * alloc_pestrdup(const char * str)
{
    return alloc_strdup(NULL, ALLOC_TYPE_PROCESS_PERSISTENT, str);
}

char * alloc_sstrdup(alloc_context * palloc, const char * str)
{
    _ASSERT(palloc != NULL);
    return alloc_strdup(palloc, ALLOC_TYPE_SHAREDMEM, str);
}

void alloc_efree(void * addr)
{
    alloc_free(NULL, ALLOC_TYPE_PROCESS, addr);
    return;
}

void alloc_pefree(void * addr)
{
    alloc_free(NULL, ALLOC_TYPE_PROCESS_PERSISTENT, addr);
    return;
}

void alloc_sfree(alloc_context * palloc, void * addr)
{
    _ASSERT(palloc != NULL);
    alloc_free(palloc, ALLOC_TYPE_SHAREDMEM, addr);

    return;
}

void * alloc_oemalloc(alloc_context * palloc, size_t hoffset, size_t size)
{
    _ASSERT(palloc == NULL);
    _ASSERT(hoffset == 0);

    return alloc_malloc(palloc, ALLOC_TYPE_PROCESS, size);
}

void * alloc_ommalloc(alloc_context * palloc, size_t hoffset, size_t size)
{
    alloc_mpool_header *  header = NULL;
    alloc_mpool_segment * pchunk = NULL;
    alloc_mpool_segment * ptemp  = NULL;

    void *                addr   = NULL;
    void *                pvoid  = NULL;
    size_t                offset = 0;
    size_t                rsize  = 0;
    size_t                bsize  = 0;
    unsigned char         ctype  = 0;

    dprintdecorate("start alloc_ommalloc");

    _ASSERT(palloc  != NULL);
    _ASSERT(hoffset >  0);
    _ASSERT(size    >  0);

    rsize  = ALIGNDWORD(size);
    header = (alloc_mpool_header *)((char *)palloc->memaddr + hoffset);

    if(rsize <= POOL_ALLOCATION_SMALL_MAX)
    {
        offset = header->socurr;
        bsize  = POOL_ALLOCATION_SMALL_BSIZE;
        ctype  = POOL_ALLOCATION_TYPE_SMALL;
    }
    else if(rsize <= POOL_ALLOCATION_MEDIUM_MAX)
    {
        offset = header->mocurr;
        bsize  = POOL_ALLOCATION_MEDIUM_BSIZE;
        ctype  = POOL_ALLOCATION_TYPE_MEDIUM;
    }
    else if(rsize <= POOL_ALLOCATION_LARGE_MAX)
    {
        offset = header->locurr;
        bsize  = POOL_ALLOCATION_LARGE_BSIZE;
        ctype  = POOL_ALLOCATION_TYPE_LARGE;
    }
    else
    {
        offset = 0;
        bsize  = rsize;
        ctype  = POOL_ALLOCATION_TYPE_HUGE;
    }

    /* Check if chunk has enough space for this allocation */
    /* Else we need to allocate a new chunk and use that */
    if(offset > 0)
    {
        pchunk = (alloc_mpool_segment *)((char *)palloc->memaddr + offset);
        if(pchunk->size - pchunk->position < rsize)
        {
            offset = 0;
            pchunk = NULL;
        }
    }

    if(offset == 0)
    {
        pchunk = (alloc_mpool_segment *)alloc_smalloc(palloc, sizeof(alloc_mpool_segment));
        if(pchunk == NULL)
        {
            goto Finished;
        }

        /* Initialize pchunk */
        pchunk->aoffset  = 0;
        pchunk->size     = 0;
        pchunk->position = 0;
        pchunk->next     = 0;

        pvoid = alloc_smalloc(palloc, bsize);
        if(pvoid == NULL)
        {
            goto Finished;
        }

        /* Update aoffset to offset of allocated memory */
        /* Also update size after allocating block */
        pchunk->aoffset = POINTER_OFFSET(palloc->memaddr, pvoid);
        pchunk->size = bsize;

        offset = POINTER_OFFSET(palloc->memaddr, pchunk);
        if(header->foffset == 0)
        {
            _ASSERT(header->loffset == 0);

            header->foffset = offset;
            header->loffset = offset;
        }
        else
        {
            ptemp = (alloc_mpool_segment *)((char *)palloc->memaddr + header->loffset);
            ptemp->next     = offset;
            header->loffset = offset;
        }

        switch(ctype)
        {
            case POOL_ALLOCATION_TYPE_SMALL:
                header->socurr = offset;
                break;
            case POOL_ALLOCATION_TYPE_MEDIUM:
                header->mocurr = offset;
                break;
            case POOL_ALLOCATION_TYPE_LARGE:
                header->locurr = offset;
                break;
        }
    }
    else
    {
        pchunk = (alloc_mpool_segment *)((char *)palloc->memaddr + offset);    
    }

    _ASSERT(pchunk != NULL);
    _ASSERT(pchunk->size - pchunk->position >= rsize);

    addr = (void *)((char *)palloc->memaddr + pchunk->aoffset + pchunk->position);
    pchunk->position += rsize;

    pchunk = NULL;

Finished:

    if(pchunk != NULL)
    {
        if(pchunk->aoffset != 0)
        {
            ptemp = (alloc_mpool_segment *)((char *)palloc->memaddr + pchunk->aoffset);
            alloc_sfree(palloc, ptemp);
            pchunk->aoffset = 0;
        }

        alloc_sfree(palloc, pchunk);
        pchunk = NULL;
    }

    dprintdecorate("end alloc_ommalloc");
    return addr;
}

void * alloc_oerealloc(alloc_context * palloc, size_t hoffset, void * addr, size_t size)
{
    _ASSERT(palloc != NULL);
    _ASSERT(hoffset == 0);

    return alloc_realloc(palloc, ALLOC_TYPE_PROCESS, addr, size);
}

void * alloc_omrealloc(alloc_context * palloc, size_t hoffset, void * addr, size_t size)
{
    /* When using pool memory, we don't know the size of a */
    /* particular block allocation.  So we can't really do a realloc */
    _ASSERT(FALSE);
    return NULL;
}

char * alloc_oestrdup(alloc_context * palloc, size_t hoffset, const char * str)
{
    _ASSERT(palloc == NULL);
    _ASSERT(hoffset == 0);

    return alloc_strdup(palloc, ALLOC_TYPE_PROCESS, str);
}

char * alloc_omstrdup(alloc_context * palloc, size_t hoffset, const char * str)
{
    char * memaddr = NULL;
    int    strl    = 0;
    
    _ASSERT(palloc  != NULL);
    _ASSERT(hoffset >  0);
    _ASSERT(str     != NULL);

    strl = strlen(str) + 1;
    memaddr = (char *)alloc_ommalloc(palloc, hoffset, strl);
    if(memaddr == NULL)
    {
        return NULL;
    }

    memcpy_s(memaddr, strl, str, strl);
    return memaddr;
}

void alloc_oefree(alloc_context * palloc, size_t hoffset, void * addr)
{
    _ASSERT(palloc == NULL);
    _ASSERT(hoffset == 0);

    alloc_free(palloc, ALLOC_TYPE_PROCESS, addr);
    return;
}

void alloc_omfree(alloc_context * palloc, size_t hoffset, void * addr)
{
    /* Nothing to do. We will free memory in the pool in one go */
    return;
}

void alloc_runtest()
{
    int             result  = NONFATAL;
    void *          memaddr = NULL;
    unsigned char   islocal = 0;

    void *          mem1    = NULL;
    void *          mem2    = NULL;
    void *          mem3    = NULL;
    void *          mem4    = NULL;

    size_t          freesiz = 0;
    
    alloc_context *         palloc = NULL;
    alloc_segment_header *  header = NULL;
    alloc_free_header *     freeh  = NULL;
    alloc_used_header *     usedh  = NULL;

    TSRMLS_FETCH();
    dprintverbose("*** STARTING ALLOC TESTS ***");

    memaddr = malloc(4096);
    if(memaddr == NULL)
    {
        result = FATAL_OUT_OF_LMEMORY;
        goto Finished;
    }

    result = alloc_create(&palloc);
    if(FAILED(result))
    {
        goto Finished;
    }

    result = alloc_initialize(palloc, islocal, "ALLOC_TEST", memaddr, 4096 TSRMLS_CC);
    if(FAILED(result))
    {
        goto Finished;
    }

    /* Verify alloc_context, alloc_segment_header and free blocks */
    _ASSERT(palloc->rwlock  != NULL);
    _ASSERT(palloc->size    == 4096);
    _ASSERT(palloc->header  != NULL);
    _ASSERT(palloc->memaddr != NULL);

    freesiz = palloc->header->free_size;

    /* Allocate 128 bytes 4 times and verify segment header, */
    /* free blocks and used blocks */
    mem1 = alloc_smalloc(palloc, 128);
    freesiz = freesiz - ALLOC_USED_BLOCK_OVERHEAD - 128;

    _ASSERT(mem1 != NULL);
    _ASSERT(POINTER_OFFSET(memaddr, mem1) < 4096);
    _ASSERT(palloc->header->usedcount == 1);
    _ASSERT(palloc->header->freecount == 1);
    _ASSERT(palloc->header->free_size == freesiz);

    mem2 = alloc_smalloc(palloc, 128);
    freesiz = freesiz - ALLOC_USED_BLOCK_OVERHEAD - 128;

    _ASSERT(mem2 != NULL);
    _ASSERT(POINTER_OFFSET(memaddr, mem2) < 4096);
    _ASSERT(palloc->header->usedcount == 2);
    _ASSERT(palloc->header->freecount == 1);
    _ASSERT(palloc->header->free_size == freesiz);

    mem3 = alloc_smalloc(palloc, 128);
    freesiz = freesiz - ALLOC_USED_BLOCK_OVERHEAD - 128;

    _ASSERT(mem3 != NULL);
    _ASSERT(POINTER_OFFSET(memaddr, mem3) < 4096);
    _ASSERT(palloc->header->usedcount == 3);
    _ASSERT(palloc->header->freecount == 1);
    _ASSERT(palloc->header->free_size == freesiz);

    mem4 = alloc_smalloc(palloc, 128);
    freesiz = freesiz - ALLOC_USED_BLOCK_OVERHEAD - 128;

    _ASSERT(mem4 != NULL);
    _ASSERT(POINTER_OFFSET(memaddr, mem4) < 4096);
    _ASSERT(palloc->header->usedcount == 4);
    _ASSERT(palloc->header->freecount == 1);
    _ASSERT(palloc->header->free_size == freesiz);

    /* Verify that free and allocating again reuse the same address */
    alloc_sfree(palloc, mem4);
    _ASSERT(mem4 == alloc_smalloc(palloc, 128));

    /* Free 3rd memory block and verify everything */
    /* Free 4th memory block and verify everything */
    /* Free 1st memory block and verify everything */
    /* Free 2nd memory block and verify everything */
    alloc_sfree(palloc, mem3);
    _ASSERT(palloc->header->usedcount == 3);
    _ASSERT(palloc->header->freecount == 2);

    alloc_sfree(palloc, mem4);
    _ASSERT(palloc->header->usedcount == 2);
    _ASSERT(palloc->header->freecount == 1);

    alloc_sfree(palloc, mem1);
    _ASSERT(palloc->header->usedcount == 1);
    _ASSERT(palloc->header->freecount == 2);

    alloc_sfree(palloc, mem2);
    _ASSERT(palloc->header->usedcount == 0);
    _ASSERT(palloc->header->freecount == 1);

    /* Allocate free_size number of bytes so that memory is full */
    /* Try allocating another block and verify error */
    /* Free block and allocate 1024 bytes twice and verify */
    /* Free one block. Verify. Free other block and verify */
    mem1 = alloc_smalloc(palloc, palloc->header->free_size);
    mem2 = alloc_smalloc(palloc, 128);

    _ASSERT(mem2 == NULL);
    _ASSERT(palloc->header->usedcount == 1);

    alloc_sfree(palloc, mem1);
    mem2 = alloc_smalloc(palloc, 1024);
    mem3 = alloc_smalloc(palloc, 1024);

    alloc_sfree(palloc, mem2);
    _ASSERT(palloc->header->usedcount == 1);

    alloc_sfree(palloc, mem3);

    _ASSERT(palloc->header->usedcount == 0);
    _ASSERT(palloc->header->freecount == 1);

    _ASSERT(SUCCEEDED(result));

Finished:

    if(palloc != NULL)
    {
        alloc_terminate(palloc);
        alloc_destroy(palloc);

        palloc =  NULL;
    }

    if(memaddr != NULL)
    {
        free(memaddr);
        memaddr = NULL;
    }

    dprintverbose("*** ENDING ALLOC TESTS ***");

    return;
}
