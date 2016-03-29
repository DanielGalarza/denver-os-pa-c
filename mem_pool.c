/*
 * Daniel Galarza
 * OS,  Spring 2016
 */
/*
 * Daniel Galarza
 * OS,  Spring 2016
 */

#include <stdlib.h>
#include <stdio.h> // for perror()

#include "mem_pool.h"

/*************/
/*           */
/* Constants */
/*           */
/*************/

static const float      MEM_FILL_FACTOR                 = 0.75;
static const unsigned   MEM_EXPAND_FACTOR               = 2;

static const unsigned   MEM_POOL_STORE_INIT_CAPACITY    = 20;
static const float      MEM_POOL_STORE_FILL_FACTOR      = 0.75;
static const unsigned   MEM_POOL_STORE_EXPAND_FACTOR    = 2;

static const unsigned   MEM_NODE_HEAP_INIT_CAPACITY     = 40;
static const float      MEM_NODE_HEAP_FILL_FACTOR       = 0.75;
static const unsigned   MEM_NODE_HEAP_EXPAND_FACTOR     = 2;

static const unsigned   MEM_GAP_IX_INIT_CAPACITY        = 40;
static const float      MEM_GAP_IX_FILL_FACTOR          = 0.75;
static const unsigned   MEM_GAP_IX_EXPAND_FACTOR        = 2;



/*********************/
/*                   */
/* Type declarations */
/*                   */
/*********************/
typedef struct _node {
    alloc_t alloc_record;
    unsigned used;
    unsigned allocated;
    struct _node *next, *prev; // doubly-linked list for gap deletion
} node_t, *node_pt;

typedef struct _gap {
    size_t size;
    node_pt node;
} gap_t, *gap_pt;

typedef struct _pool_mgr {
    pool_t pool;
    node_pt node_heap;
    unsigned total_nodes;
    unsigned used_nodes;
    gap_pt gap_ix;
    unsigned gap_ix_capacity;
} pool_mgr_t, *pool_mgr_pt;



/***************************/
/*                         */
/* Static global variables */
/*                         */
/***************************/
static pool_mgr_pt *pool_store = NULL; // an array of pointers, only expand
static unsigned pool_store_size = 0;
static unsigned pool_store_capacity = 0;



/********************************************/
/*                                          */
/* Forward declarations of static functions */
/*                                          */
/********************************************/
static alloc_status _mem_resize_pool_store();
static alloc_status _mem_resize_node_heap(pool_mgr_pt pool_mgr);
static alloc_status _mem_resize_gap_ix(pool_mgr_pt pool_mgr);
static alloc_status _mem_add_to_gap_ix(pool_mgr_pt pool_mgr, size_t size, node_pt node);
static alloc_status _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr, size_t size, node_pt node);
static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr);



/****************************************/
/*                                      */
/* Definitions of user-facing functions */
/*                                      */
/****************************************/
alloc_status mem_init() {

    // ensure that it's called only once until mem_free

    if (pool_store == NULL) {

        // allocate the pool store with initial capacity
        pool_store = calloc(MEM_POOL_STORE_INIT_CAPACITY, sizeof(pool_mgr_pt));
        pool_store_capacity = MEM_POOL_STORE_INIT_CAPACITY;
        pool_store_size = 0;
        return ALLOC_OK;
    }

    return ALLOC_CALLED_AGAIN;
}

alloc_status mem_free() {
    // ensure that it's called only once for each mem_init
    if (pool_store == NULL)
        return ALLOC_CALLED_AGAIN;

    // make sure all pool managers have been deallocated
    int i = 0;
    while (i < pool_store_size) {
        if (pool_store[i] != NULL) {
            return ALLOC_FAIL;
        }
        i++;
    }

    // can free the pool store array
    free(pool_store);


    // update static variables
    pool_store_size = 0;
    pool_store_capacity = 0;
    pool_store = NULL;

    return ALLOC_OK;
}


pool_pt mem_pool_open(size_t size, alloc_policy policy) {

    // make sure there the pool store is allocated
    if (pool_store == NULL)
        return NULL;

    // expand the pool store, if necessary
    _mem_resize_pool_store();


    // allocate a new mem pool mgr
    pool_mgr_pt pool_mgr = calloc(1, sizeof(pool_mgr_t));

    // check success, on error return null
    if(pool_mgr == NULL)
        return NULL;


    // allocate a new memory pool
    pool_mgr->pool.mem = (char*) calloc(size, sizeof(char));
    pool_mgr->pool.policy = policy;
    pool_mgr->pool.total_size = size;
    pool_mgr->pool.alloc_size = 0;
    pool_mgr->pool.num_allocs = 0;
    pool_mgr->pool.num_gaps = 1;

    // check success, on error deallocate mgr and return null
    if((&pool_mgr->pool) == NULL){
        free(pool_mgr);
        return NULL;
    }

    // allocate a new node heap
    pool_mgr->node_heap = calloc(MEM_NODE_HEAP_INIT_CAPACITY, sizeof(node_t));


    // check success, on error deallocate mgr/pool and return null
    if(pool_mgr->node_heap == NULL){

        free(pool_mgr);
        free(&pool_mgr->pool);
        return NULL;

    }

    // allocate a new gap index
    pool_mgr->gap_ix = calloc(MEM_GAP_IX_INIT_CAPACITY, sizeof(gap_t));



    // check success, on error deallocate mgr/pool/heap and return null
    if(pool_mgr->gap_ix == NULL){

        free(pool_mgr);
        free(&pool_mgr->pool);
        free(pool_mgr->node_heap);
        return NULL;

    }


    // assign all the pointers and update meta data:
    //   initialize top node of node heap
    node_pt node_h = (node_pt) pool_mgr->node_heap;

    node_h->alloc_record.size = size;
    node_h->alloc_record.mem = pool_mgr->pool.mem;
    node_h->used = 1;
    node_h->allocated = 0;
    node_h->next = NULL;
    node_h->prev = NULL;



    //   initialize top node of gap index
    //   initialize pool mgr
    pool_mgr->gap_ix[0].size = size;
    pool_mgr->gap_ix[0].node = pool_mgr->node_heap;
    pool_mgr->gap_ix_capacity = MEM_GAP_IX_INIT_CAPACITY;
    pool_mgr->total_nodes = MEM_NODE_HEAP_INIT_CAPACITY;
    pool_mgr->used_nodes = 1;

    //   link pool mgr to pool store
    pool_store[pool_store_size] = pool_mgr;
    pool_store_size++;

    // return the address of the mgr, cast to (pool_pt)
    return (pool_pt) pool_mgr;
}


alloc_status mem_pool_close(pool_pt pool) {


    // get mgr from pool by casting the pointer to (pool_mgr_pt)
    pool_mgr_pt pool_mgr = (pool_mgr_pt) pool;

    // check if this pool is allocated
    if (pool == NULL  || !pool->num_gaps == 1 || !pool->num_allocs == 0)
        return ALLOC_NOT_FREED;

    // free memory pool
    // free node heap
    // free gap index
    free(pool->mem);
    free(pool_mgr->node_heap);
    free(pool_mgr->gap_ix);

    int j = 0;
    while(j < pool_store_capacity) {
        if (pool_store[j] == pool_mgr) {
            pool_store[j] = NULL;
            break;
        }
        j++;
    }

    // note: don't decrement pool_store_size, because it only grows
    // free mgr
    free(pool_mgr);
    return ALLOC_OK;
}


alloc_pt mem_new_alloc(pool_pt pool, size_t size) {


    // get mgr from pool by casting the pointer to (pool_mgr_pt)
    pool_mgr_pt pool_mgr = (pool_mgr_pt) pool;

    // check if any gaps, return null if none
    if (pool->num_gaps == 0)
        return NULL;

    // expand heap node, if necessary, quit on error
    _mem_resize_node_heap(pool_mgr);

    // check used nodes fewer than total nodes, quit on error
    if (pool_mgr->used_nodes >= pool_mgr->total_nodes)
        return NULL;


    // get a node for allocation:
    node_pt node_alloc = NULL;

    // if FIRST_FIT, then find the first sufficient node in the node heap
    if (pool->policy == FIRST_FIT) {
        int i = 0;
        while (i < pool_mgr->total_nodes) {
            if (pool_mgr->node_heap[i].used == 1 &&
                pool_mgr->node_heap[i].allocated == 0 &&
                pool_mgr->node_heap[i].alloc_record.size >= size) {
                node_alloc = &pool_mgr->node_heap[i];
                break;
            }
            i++;
        }
    }

    else if (pool->policy == BEST_FIT) {
        int i = 0;
        while (i < pool_mgr->gap_ix_capacity) {
            if (pool_mgr->gap_ix[i].size >= size) {
                node_alloc = pool_mgr->gap_ix[i].node;
                break;
            }
            i++;
        }
    }

    // check if node found
    if (node_alloc == NULL)
        return NULL;


    // update metadata (num_allocs, alloc_size)
    // calculate the size of the remaining gap, if any
    // remove node from gap index
    pool->num_allocs += 1;
    pool->alloc_size += size;
    size_t remaining_gap = node_alloc->alloc_record.size - size;
    _mem_remove_from_gap_ix(pool_mgr, size, node_alloc);




    // convert gap_node to an allocation node of given size
    node_alloc->alloc_record.size = size;
    node_alloc->used = 1;
    node_alloc->allocated = 1;


    // adjust node heap:
    if (remaining_gap > 0) {

        //   if remaining gap, need a new node
        node_pt nodes_unused = NULL;

        //   find an unused one in the node heap
        int i = 0;
        while(i < pool_mgr->total_nodes) {
            if (pool_mgr->node_heap[i].used == 0) {
                nodes_unused = &pool_mgr->node_heap[i];
                break;
            }
            i++;
        }

        //   make sure one was found
        if (nodes_unused == NULL)
            return NULL;

        //   initialize it to a gap node
        nodes_unused->alloc_record.mem = node_alloc->alloc_record.mem + size;
        nodes_unused->alloc_record.size = remaining_gap;
        nodes_unused->used = 1;
        nodes_unused->allocated = 0;
        nodes_unused->next = NULL;
        nodes_unused->prev = NULL;


        //   update metadata (used_nodes)
        //   update linked list (new node right after the node for allocation)
        pool_mgr->used_nodes++;
        nodes_unused->prev = node_alloc;
        nodes_unused->next = node_alloc->next;
        if (node_alloc->next != NULL)
            node_alloc->next->prev = nodes_unused;
        node_alloc->next = nodes_unused;

        //   add to gap index
        //   check if successful
        if (_mem_add_to_gap_ix(pool_mgr, remaining_gap, nodes_unused) == ALLOC_FAIL)
            return NULL;

    }

    // return allocation record by casting the node to (alloc_pt)
    return (alloc_pt) node_alloc;
}


alloc_status mem_del_alloc(pool_pt pool, alloc_pt alloc) {

    // get mgr from pool by casting the pointer to (pool_mgr_pt)
    pool_mgr_pt pool_mgr = (pool_mgr_pt) pool;

    // get node from alloc by casting the pointer to (node_pt)
    node_pt node = (node_pt) alloc;

    // find the node in the node heap
    // find the node in the node heap

    int i = 0;
    int del = -1;
    while(pool_mgr->total_nodes) {

        if (&pool_mgr->node_heap[i] == node) {
            del = i;
            break;
        }
        i++;
    }

    // make sure it's found
    if (del == -1) {
        return ALLOC_FAIL;
    }

    // convert to gap node
    // update metadata (num_allocs, alloc_size)
    node->used = 1;
    node->allocated = 0;
    pool->num_allocs--;
    pool->alloc_size -= alloc->size;

    // if the next node in the list is also a gap, merge into node-to-delete
    if (node->next != NULL && node->next->allocated == 0) {

        //   add the size to the node-to-delete
        alloc->size += node->next->alloc_record.size;

        //   remove the next node from gap index
        _mem_remove_from_gap_ix(pool_mgr, node->next->alloc_record.size, node->next);


        //   update node as unused
        node->next->used = 0;
        node->next->alloc_record.size = 0;
        node->next->alloc_record.mem = NULL;

        //   update metadata (used nodes)
        pool_mgr->used_nodes--;


        //   update linked list:
        if (node->next->next == NULL) {

            node->next->prev = NULL;
            node->next = NULL;

        }

        else {
            node->next->next->prev = node;
            node->next = node->next->next;
        }

    }



    // this merged node-to-delete might need to be added to the gap index
    // but one more thing to check...
    // if the previous node in the list is also a gap, merge into previous!

    if (node->prev != NULL && node->prev->allocated == 0) {

        //   add the size of node-to-delete to the previous
        node->prev->alloc_record.size += alloc->size;


        //   remove the previous node from gap index
        if (_mem_remove_from_gap_ix(pool_mgr,
                                    node->prev->alloc_record.size - alloc->size,
                                    node->prev) == ALLOC_FAIL) {
            return ALLOC_FAIL;
        }

        //   update node-to-delete as unused
        node->alloc_record.size = 0;
        node->used = 0;
        node->alloc_record.mem = NULL;

        //   update metadata (used_nodes)
        pool_mgr->used_nodes--;

        //   update linked list
        if (node->next == NULL) {
            node->prev->next = NULL;
        }

        else {
            node->prev->next = node->next;
            node->next->prev = node->prev;
        }



        //   change the node to add to the previous node!
        node = node->prev;
    }


    // add the resulting node to the gap index
    _mem_add_to_gap_ix(pool_mgr, node->alloc_record.size, node);
    return ALLOC_OK;
}


void mem_inspect_pool(pool_pt pool, pool_segment_pt *segments, unsigned *num_segments) {


    // get the mgr from the pool
    pool_mgr_pt pool_mgr = (pool_mgr_pt) pool;

    // allocate the segments array with size == used_nodes
    pool_segment_pt poolSegs = (pool_segment_pt) calloc(pool_mgr->used_nodes, sizeof(pool_segment_t));

    // check successful
    if (poolSegs == NULL) {
        return;
    }

    // loop through the node heap and the segments array
    node_pt node = pool_mgr->node_heap;
    int segsCount = 0;


    while(node != NULL) {

        // for each node, write the size and allocated in the segment
        poolSegs[segsCount].size = node->alloc_record.size;
        poolSegs[segsCount].allocated = node->allocated;
        segsCount++;
        node = node->next;
    }

    *num_segments = pool_mgr->used_nodes;

    *segments = poolSegs;

}


/***********************************/
/*                                 */
/* Definitions of static functions */
/*                                 */
/***********************************/


static alloc_status _mem_resize_pool_store() {

    // check if necessary
    if (((float) pool_store_size / pool_store_capacity) > MEM_POOL_STORE_FILL_FACTOR) {

        // reallocate w/ size expanded by expand factor
        //update capacity

        pool_store = realloc(pool_store, sizeof(pool_mgr_pt) * pool_store_capacity * MEM_POOL_STORE_EXPAND_FACTOR);
        pool_store_capacity *= MEM_POOL_STORE_EXPAND_FACTOR;
    }

    if (pool_store == NULL)
        return ALLOC_FAIL;



    return ALLOC_OK;
}

static alloc_status _mem_resize_node_heap(pool_mgr_pt pool_mgr) {

    // check if necessary
    if (((float) pool_mgr->used_nodes / pool_mgr->total_nodes) > MEM_NODE_HEAP_FILL_FACTOR) {


        // reallocate w/ size expanded by expand factor
        //update capacity
        pool_mgr->node_heap = realloc(pool_mgr->node_heap, sizeof(node_t) * pool_mgr->total_nodes * MEM_NODE_HEAP_EXPAND_FACTOR);
        pool_mgr->total_nodes *= MEM_NODE_HEAP_EXPAND_FACTOR;
    }

    if (pool_mgr->node_heap == NULL)
        return ALLOC_FAIL;



    return ALLOC_OK;
}

static alloc_status _mem_resize_gap_ix(pool_mgr_pt pool_mgr) {


    // check if necessary
    if (((float) pool_mgr->pool.num_gaps / pool_mgr->gap_ix_capacity) > MEM_GAP_IX_FILL_FACTOR) {


        // reallocate w/ size expanded by expand factor
        //update capacity
        pool_mgr->gap_ix = realloc(&pool_mgr->gap_ix_capacity, sizeof(gap_t) * pool_mgr->pool.num_gaps * MEM_GAP_IX_EXPAND_FACTOR);
        pool_mgr->gap_ix_capacity *= MEM_GAP_IX_EXPAND_FACTOR;
    }

    if (pool_mgr->gap_ix == NULL)
        return ALLOC_FAIL;



    return ALLOC_OK;
}

static alloc_status _mem_add_to_gap_ix(pool_mgr_pt pool_mgr, size_t size, node_pt node) {
    // expand the gap index, if necessary (call the function)
    _mem_resize_gap_ix(pool_mgr);

    // add the entry at the end
    pool_mgr->gap_ix[pool_mgr->pool.num_gaps].node = node;
    pool_mgr->gap_ix[pool_mgr->pool.num_gaps].size = size;
    pool_mgr->pool.num_gaps++;

    // sort the gap index (call the function)
    // check success
    if(_mem_sort_gap_ix(pool_mgr) == ALLOC_FAIL)
        return ALLOC_FAIL;



    return ALLOC_OK;
}

static alloc_status _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr, size_t size, node_pt node) {


    // find the position of the node in the gap index
    int i = 0;
    int indexOfNode = -1;

    while( i < pool_mgr->gap_ix_capacity){
        if (pool_mgr->gap_ix[i].node == node){
            indexOfNode = i;
            break;
        }
        i++;
    }

    // loop from there to the end of the array
    int j = indexOfNode;
    while(j < pool_mgr ->gap_ix_capacity-1) {
        pool_mgr->gap_ix[j] = pool_mgr->gap_ix[j + 1];
        j++;
    }

    // update metadata (num_gaps)
    // zero out the element at position num_gaps!
    pool_mgr->pool.num_gaps--;
    pool_mgr->gap_ix[pool_mgr->pool.num_gaps].size = 0;
    pool_mgr->gap_ix[pool_mgr->pool.num_gaps].node = NULL;

    return ALLOC_OK;
}

static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr) {

    // the new entry is at the end, so "bubble it up"
    // loop from num_gaps - 1 until but not including 0:
    int i = pool_mgr->pool.num_gaps - 1;
    while(i > 0) {

        int difference = (int) (pool_mgr->gap_ix[i].size - pool_mgr->gap_ix[i - 1].size);

        // if the size of the current entry is less than the previous (u - 1)
        if(difference < 0) {

            // swap them (by copying) (remember to use a temporary variable)
            gap_t t = pool_mgr->gap_ix[i - 1];
            pool_mgr->gap_ix[i - 1] = pool_mgr->gap_ix[i];
            pool_mgr->gap_ix[i] = t;
        }

        else if(difference == 0 && pool_mgr->gap_ix[i].node->alloc_record.mem < pool_mgr->gap_ix[i - 1].node->alloc_record.mem) {

            //swap by alloc_record.mem
            gap_t t = pool_mgr->gap_ix[i - 1];
            pool_mgr->gap_ix[i - 1] = pool_mgr->gap_ix[i];
            pool_mgr->gap_ix[i] = t;
        }

        i--;
    }


    return ALLOC_OK;
}



