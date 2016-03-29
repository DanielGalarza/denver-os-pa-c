/*
 * Daniel Galarza
 * OS,  Spring 2016
 */

#include <stdlib.h>
#include <assert.h>
#include <stdio.h> // for perror()
#include <mm_malloc.h>

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
    // note: holds pointers only, other functions to allocate/deallocate

    // Checking if mem_init() hasn't already been called.

        // Checking if pool hasn't been initialized. If it hasn't, then initialize it.
    if (pool_store == NULL) {
        // allocate the pool store with initial capacity
        // Setting the pool_store to allocate (mem pool store init capacity) and size of the pool manager struct
        pool_store = (pool_mgr_pt*) calloc(MEM_POOL_STORE_INIT_CAPACITY,
                            sizeof(pool_mgr_pt)); //sizeof(pool_mgr_pt) OR  sizeof(pool_mgr_t)??

        pool_store_capacity = MEM_POOL_STORE_INIT_CAPACITY;
        pool_store_size = 0;

        //printf("ALLOC_OK\n");
        return ALLOC_OK;
    }

    else
        return ALLOC_CALLED_AGAIN;

}


alloc_status mem_free() {
    // ensure that it's called only once for each mem_init
    // make sure all pool managers have been de-allocated
    // can free the pool store array
    // update static variables

    if(pool_store == NULL) {
        //printf("ALLOC_CALLED_AGAIN\n");
        return ALLOC_CALLED_AGAIN;
    }

    int i = 0;
    while(i<pool_store_size) {
        if (pool_store[i] != NULL) {
            return ALLOC_FAIL;
        }
        i++;
    }

    // Freeing pool_store memory and resetting pool_store, it's size and capacity to original states.
    free(pool_store);

    pool_store = NULL; // an array of pointers, only expand
    pool_store_size = 0;
    pool_store_capacity = 0;

    //printf("ALLOC_OK\n");

    return ALLOC_OK;
}

pool_pt mem_pool_open(size_t size, alloc_policy policy) {

    // make sure that the pool store is allocated
    if(pool_store == NULL){
        //printf("pool store is NULL\n");
        return NULL;
    }

    // expand the pool store, if necessary
    _mem_resize_pool_store();

    // if pool_store_size divided by pool_store_capacity is greater than 75 percent.....
    /*if(_mem_resize_pool_store() != ALLOC_OK) {
        printf("ALLOC_OK");
        return NULL;
    } */

    // allocate a new mem pool mgr
    pool_mgr_pt pool_mgr = calloc(1, sizeof(pool_mgr_t));

    // check success, on error return null
    if(pool_mgr == NULL) {
        return NULL;
    }

    //Generic variable to shorten code.
    //pool_t p_m = pool_mgr->pool;

    // allocate a new memory pool
    pool_mgr->pool.mem = (char*) calloc(size, sizeof(char));
    pool_mgr->pool.policy = policy;
    pool_mgr->pool.total_size = size;
    pool_mgr->pool.alloc_size = 0;
    pool_mgr->pool.num_allocs = 0;
    pool_mgr->pool.num_gaps = 1;

    // check success, on error deallocate mgr and return null
    if(&pool_mgr->pool == NULL){
        free(pool_mgr);
        return NULL;
    }

    // allocate a new node heap
    pool_mgr->node_heap = (node_pt) calloc(MEM_NODE_HEAP_INIT_CAPACITY, sizeof(node_t));

    // check success, on error deallocate mgr/pool and return null
    if(pool_mgr->node_heap == NULL){

        free(pool_mgr);
        free(&pool_mgr->pool);
        return NULL;
    }

    // allocate a new gap index
    pool_mgr->gap_ix = (gap_pt) calloc(MEM_GAP_IX_INIT_CAPACITY, sizeof(gap_t)); //might need to cast with (gap_t)

    // check success, on error deallocate mgr/pool/heap and return null
    if(pool_mgr->gap_ix == NULL){

        free(&pool_mgr->pool);
        free(pool_mgr->node_heap);
        free(pool_mgr);

        return NULL;
    }


    //node heap
    //node_pt n_h = pool_mgr->node_heap;

    // assign all the pointers and update meta data:
    //   initialize top node of node heap
    pool_mgr->node_heap->alloc_record.size = size;
    pool_mgr->node_heap->alloc_record.mem = pool_mgr->pool.mem;
    pool_mgr->node_heap->used = 1;
    pool_mgr->node_heap->allocated = 0;
    pool_mgr->node_heap->next = NULL;
    pool_mgr->node_heap->prev = NULL;


    //   initialize top node of gap index
    //   initialize pool mgr
    pool_mgr->gap_ix[0].size = size;
    pool_mgr->gap_ix[0].node = pool_mgr->node_heap;
    pool_mgr->gap_ix_capacity = MEM_GAP_IX_INIT_CAPACITY;
    pool_mgr->total_nodes = MEM_NODE_HEAP_INIT_CAPACITY;
    pool_mgr->used_nodes = 1;

    //   link pool mgr to pool store
    pool_store[pool_store_size] = pool_mgr;
    pool_store_size += 1;

    // return the address of the mgr, cast to (pool_pt)
    return (pool_pt) pool_mgr;  //maybe &pool_mgr

}



alloc_status mem_pool_close(pool_pt pool) {

    // get mgr from pool by casting the pointer to (pool_mgr_pt)
    pool_mgr_pt pool_mgr = (pool_mgr_pt) pool;

    // check if this pool is allocated  OR  check if pool has only one gap  OR  check if it has zero allocations
    if(pool == NULL || (!pool->num_gaps ==1) || (!pool->num_gaps == 0)) {
        return ALLOC_NOT_FREED;
    }

    // free memory pool
    // free node heap
    // free gap index
    free(pool->mem);
    free(pool_mgr->node_heap);
    free(pool_mgr->gap_ix);

    // find mgr in pool store and set to null
    int i = 0;
    while (i < pool_store_capacity) {
        if(pool_store[i] == pool_mgr) {
            pool_store[i] = NULL;
            break;
        }
        i++;
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
    if (pool->num_gaps == 0) {
        return NULL;
    }

    // expand heap node, if necessary, quit on error
    _mem_resize_node_heap(pool_mgr);

    // check used nodes fewer than total nodes, quit on error
    if (pool_mgr->used_nodes >= pool_mgr->total_nodes) {
        return NULL;
    }

    // get a node for allocation:
    node_pt node = NULL;

    // if FIRST_FIT, then find the first sufficient node in the node heap
    if (pool->policy == FIRST_FIT) {
        int i = 0;
        while(i < pool_mgr->total_nodes) {
            if (pool_mgr->node_heap[i].used == 1 && pool_mgr->node_heap[i].allocated == 0 && pool_mgr->node_heap[i].alloc_record.size >= size) {
                node = &pool_mgr->node_heap[i];
                break;
            }
            i++;
        }
    }


    // if BEST_FIT, then find the first sufficient node in the gap index
    else if (pool->policy == BEST_FIT) {
        int i =0;
        while(pool_mgr->gap_ix_capacity) {
            if (pool_mgr->gap_ix[i].size >= size) {
                node = pool_mgr->gap_ix[i].node;
                break;
            }
            i++;
        }
    }

    // check if node found
    if (node == NULL) {
        return NULL;
    }

    // update metadata (num_allocs, alloc_size)
    pool->num_allocs += 1;
    pool->alloc_size += size;

    // calculate the size of the remaining gap, if any
    size_t remaining_gap = node->alloc_record.size - size;

    // remove node from gap index
    _mem_remove_from_gap_ix(pool_mgr, size, node);

    // convert gap_node to an allocation node of given size
    node->alloc_record.size = size;
    node->used = 1;
    node->allocated = 1;

    // adjust node heap:
    if (remaining_gap > 0) {

        //If remaining gap, need a new node
        node_pt un_node = NULL;

        //Find an unused one in the node heap
        int i = 0;
        while(i < pool_mgr->total_nodes) {
            if (pool_mgr->node_heap[i].used == 0) {
                un_node = &pool_mgr->node_heap[i];
                break;
            }
            i++;
        }

        //Make sure one was found
        if (un_node == NULL) {
            return NULL;
        }

        //Initialize it to a gap node
        un_node->alloc_record.mem = node->alloc_record.mem + size;
        un_node->alloc_record.size = remaining_gap;
        un_node->used = 1;
        un_node->allocated = 0;
        un_node->next = NULL;
        un_node->prev = NULL;

        //update metadata
        pool_mgr->used_nodes += 1;
        un_node->prev = node;         // update linked list (new node right after the node for allocation)
        un_node->next = node->next;

        //if next node is not null...
        if (node->next != NULL)
            node->next->prev = un_node;

        node->next = un_node;

        // add to gap index
        // check if successful
        if (_mem_add_to_gap_ix(pool_mgr, remaining_gap, un_node) == ALLOC_FAIL) {
            return NULL;
        }
    }

    // return allocation record by casting the node to (alloc_pt)
    return (alloc_pt) node;
}

alloc_status mem_del_alloc(pool_pt pool, alloc_pt alloc) {

    // get mgr from pool by casting the pointer to (pool_mgr_pt)
    pool_mgr_pt pool_mgr = (pool_mgr_pt) pool;

    // get node from alloc by casting the pointer to (node_pt)
    node_pt node = (node_pt) alloc;


    // find the node in the node heap
    int i = 0;
    int del_node = -1;

    while(pool_mgr->total_nodes) {

        if (&pool_mgr->node_heap[i] == node) {
            del_node = i;
            break;
        }
        i++;
    }

    // this is node-to-delete
    // make sure it's found
    if (del_node == -1) {
        return ALLOC_FAIL;
    }

    // convert to gap node
    node->used = 1;
    node->allocated = 0;

    // update metadata (num_allocs, alloc_size)
    pool->num_allocs--;
    pool->alloc_size -= alloc->size;

    // if the next node in the list is also a gap, merge into node-to-delete
    if (node->next != NULL && node->next->allocated == 0) {

        //add the size to the node-to-delete
        alloc->size += node->next->alloc_record.size;

        //remove the next node from gap index
        _mem_remove_from_gap_ix(pool_mgr, node->next->alloc_record.size, node->next);

        //update node as unused
        node->next->used = 0;
        node->next->alloc_record.size = 0;
        node->next->alloc_record.mem = NULL;


        //update metadata (used nodes)
        pool_mgr->used_nodes -= 1;

        //update linked list:
        if (node->next->next == NULL) {

            node->next->prev = NULL;
            node->next = NULL;
        }

        else {

            node->next->next->prev = node;
            node->next = node->next->next;
        }
    }

    // if the previous node in the list is also a gap, merge into previous!
    if (node->prev->allocated == 0 && node->prev != NULL) {

        //add the size of node-to-delete to the previous
        node->prev->alloc_record.size += alloc->size;

        //remove the previous node from gap index
        //check success
        if (_mem_remove_from_gap_ix(pool_mgr, node->prev->alloc_record.size - alloc->size, node->prev) == ALLOC_FAIL) {
            return ALLOC_FAIL;
        }

        //update node-to-delete as unused
        node->alloc_record.mem = NULL;
        node->used = 0;
        node->alloc_record.size = 0;

        //update metadata (used_nodes)
        pool_mgr->used_nodes--;

        //update linked list
        /*
                        if (node_to_del->next) {
                            prev->next = node_to_del->next;
                            node_to_del->next->prev = prev;
                        } else {
                            prev->next = NULL;
                        }
                        node_to_del->next = NULL;
                        node_to_del->prev = NULL;
         */

        if (node->next != NULL) {

            node->prev->next = node->next;
            node->next->prev = node->prev;
        }

        else {
            node->prev->next = NULL;
        }

        //change the node to add to the previous node!
        node = node->prev;
    }

    //add the resulting node to the gap index
    _mem_add_to_gap_ix(pool_mgr, node->alloc_record.size, node);

    // check success
    return ALLOC_OK;
}


void mem_inspect_pool(pool_pt pool, pool_segment_pt *segments, unsigned *num_segments) {

    // get the mgr from the pool
    pool_mgr_pt pool_mgr = (pool_mgr_pt) pool;

    // allocate the segments array with size == used_nodes
    pool_segment_pt pool_segments = calloc(pool_mgr->used_nodes, sizeof(pool_segment_t)); //cast pool_segments_pt maybe?

    // check successful
    if (pool_segments == NULL) {
        return;
    }

    // loop through the node heap and the segments array
    node_pt node = pool_mgr->node_heap;

    int segment_count = 0;

    while (node != NULL) {
        //    for each node, write the size and allocated in the segment
        pool_segments[segment_count].allocated = node->allocated;
        pool_segments[segment_count].size = node->alloc_record.size;
        node = node->next;
        segment_count += 1;
    }

    // "return" the values:
    *num_segments = pool_mgr->used_nodes;
    *segments = pool_segments;

}


/***********************************/
/*                                 */
/* Definitions of static functions */
/*                                 */
/***********************************/
static alloc_status _mem_resize_pool_store() {
    // check if necessary
    // don't forget to update capacity variables
    // if pool_store_size divided by pool_store_capacity is greater than 75 percent.....
    if (((float) pool_store_size / pool_store_capacity) > MEM_POOL_STORE_FILL_FACTOR) {

        pool_store = realloc(pool_store, sizeof(pool_mgr_pt) * MEM_POOL_STORE_EXPAND_FACTOR * pool_store_capacity);
        pool_store_capacity *= MEM_POOL_STORE_EXPAND_FACTOR;
    }

    if (pool_store == NULL)
        return ALLOC_FAIL;

    return ALLOC_OK;

}





static alloc_status _mem_resize_node_heap(pool_mgr_pt pool_mgr) {

    if (((float) pool_mgr->used_nodes / pool_mgr->total_nodes) > MEM_NODE_HEAP_FILL_FACTOR) {

        pool_mgr->node_heap = realloc(pool_mgr->node_heap, sizeof(node_t) * pool_mgr->total_nodes * MEM_NODE_HEAP_EXPAND_FACTOR);
        pool_mgr->total_nodes *= MEM_NODE_HEAP_EXPAND_FACTOR;
    }

    if (pool_mgr->node_heap == NULL) {
        return ALLOC_FAIL;
    }

    return ALLOC_OK;
}




static alloc_status _mem_resize_gap_ix(pool_mgr_pt pool_mgr) {

    if (((float) pool_mgr->pool.num_gaps / pool_mgr->gap_ix_capacity) > MEM_GAP_IX_FILL_FACTOR) {
        pool_mgr->gap_ix = realloc(&pool_mgr->gap_ix_capacity, sizeof(gap_t) * pool_mgr->pool.num_gaps * MEM_GAP_IX_EXPAND_FACTOR);
        pool_mgr->gap_ix_capacity *= MEM_GAP_IX_EXPAND_FACTOR;
    }

    if (pool_mgr->gap_ix == NULL) {
        return ALLOC_FAIL;
    }

    return ALLOC_OK;
}

static alloc_status _mem_add_to_gap_ix(pool_mgr_pt pool_mgr, size_t size, node_pt node) {

    // expand the gap index, if necessary (call the function)
    _mem_resize_gap_ix(pool_mgr);

    // add the entry at the end
    pool_mgr->gap_ix[pool_mgr->pool.num_gaps].size = size;

    pool_mgr->gap_ix[pool_mgr->pool.num_gaps].node = node;

    // update metadata (num_gaps)
    pool_mgr->pool.num_gaps++;

    // sort the gap index (call the function)
    if(_mem_sort_gap_ix(pool_mgr) == ALLOC_FAIL) {
        return ALLOC_FAIL;
    }

    // check success
    return ALLOC_OK;
}

static alloc_status _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr, size_t size, node_pt node) {

    // find the position of the node in the gap index
    int node_index = -1;
    for (int i = 0; i < pool_mgr->gap_ix_capacity; i++){

        if (pool_mgr->gap_ix[i].node == node){

            node_index = i;
            i = pool_mgr->gap_ix_capacity;
        }
    }

    // loop from there to the end of the array:
    int j = 0;
    while(pool_mgr->gap_ix_capacity - 1) {
        pool_mgr->gap_ix[i] = pool_mgr->gap_ix[i + 1];
        j++;
    }

    // update metadata (num_gaps)
    pool_mgr->pool.num_gaps -= 1;


    // zero out the element at position num_gaps!
    pool_mgr->gap_ix[pool_mgr->pool.num_gaps].node = NULL;
    pool_mgr->gap_ix[pool_mgr->pool.num_gaps].size = 0;

    return ALLOC_OK;
}


static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr) {
    // the new entry is at the end, so "bubble it up"
    // loop from num_gaps - 1 until but not including 0:
    //    if the size of the current entry is less than the previous (u - 1)
    //    or if the sizes are the same but the current entry points to a
    //    node with a lower address of pool allocation address (mem)
    //       swap them (by copying) (remember to use a temporary variable)

    // the new entry is at the end, so "bubble it up"
    // loop from num_gaps - 1 until but not including 0:

    int i = pool_mgr->pool.num_gaps - 1;
    while(i > 0) {

        int difference = (int) (pool_mgr->gap_ix[i].size - pool_mgr->gap_ix[i - 1].size);

        if (difference < 0) {
            gap_t temp = pool_mgr->gap_ix[i - 1];
            pool_mgr->gap_ix[i - 1] = pool_mgr->gap_ix[i];
            pool_mgr->gap_ix[i] = temp;
        }

        else if (difference == 0 && pool_mgr->gap_ix[i].node->alloc_record.mem < pool_mgr->gap_ix[i - 1].node->alloc_record.mem) {
            gap_t temp = pool_mgr->gap_ix[i - 1];
            pool_mgr->gap_ix[i - 1] = pool_mgr->gap_ix[i];
            pool_mgr->gap_ix[i] = temp;
        }
        i -= 1;
    }
    return ALLOC_OK;
}


