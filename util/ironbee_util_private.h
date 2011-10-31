/*****************************************************************************
 * Licensed to Qualys, Inc. (QUALYS) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * QUALYS licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/

#ifndef _IB_UTIL_PRIVATE_H_
#define _IB_UTIL_PRIVATE_H_

/**
 * @file
 * @brief IronBee - Private Utility Declarations
 *
 * @author Brian Rectanus <brectanus@qualys.com>
 * @author Pablo Rincon <pablo.rincon.crespo@gmail.com>
 */

#include <ironbee/util.h>


/* Memory pool definitions */

/** Default page size. Buffers will be of size N * IB_MPOOL_DEFAULT_PAGE_SIZE */
#define IB_MPOOL_DEFAULT_PAGE_SIZE ((size_t)1024)

/**
 * The page size can be set to other with ib_mpool_create_ex()
 * but it must be greater than IB_MPOOL_MIN_PAGE_SIZE, otherwise it will be
 * overwriten by IB_MPOOL_MIN_PAGE_SIZE
 */
#define IB_MPOOL_MIN_PAGE_SIZE     ((size_t)512)

/**
 * IB_MPOOL_INCREASE_FACTOR -> Min 2, 4, 8, .. when a new var needs to be
 * allocated and size is greater than the default pagesize, it will create a
 * buffer of size page_size * num_pages_needed * IB_MPOOL_INCREASE_FACTOR.
 * Keep in mind that a high value might misuse mem of the system */
#define IB_MPOOL_INCREASE_FACTOR    2

/**
 * IB_MPOOL_MIN_SIZE_BITS defines the minimum size guaranteed by indexed[0]
 * as a exponent of 2, so IB_MPOOL_MIN_SIZE_BITS 4 implies that 16 bytes can
 * be allocated in buffers linked from indexed[0] */
#define IB_MPOOL_MIN_SIZE_BITS      4

/**
 * IB_MPOOL_NUM_SLOTS defines the number of slots in \a indexed */
#define IB_MPOOL_NUM_SLOTS          8

/**
 * IB_MPOOL_REMAINING_LIMIT is the limit of available mem of a buffer.
 * Less mem than IB_MPOOL_MIN_SIZE_BITS makes the buffer to be considered as
 * full, so it will be moved to busy_buffers list */
#define IB_MPOOL_REMAINING_LIMIT    (1 << IB_MPOOL_MIN_SIZE_BITS)


/**
 * @internal
 * Memory buffers structure. Size must be n * IB_MPOOL_DEFAULT_PAGE_SIZE
 */
struct ib_mpool_buffer_t {
    uint8_t                    *buffer;     /**< ptr to the buffer */
    size_t                      used;       /**< amount of mem really used */
    size_t                      size;       /**< size */
    
    ib_mpool_buffer_t          *prev;       /**< Sibling prev */
    ib_mpool_buffer_t          *next;       /**< Sibling next */
    /* @todo later we might want to be able to free just one page
     * so here we will need references to parent and child pools */
};

#define IB_MPOOL_MAX_INDEX 7

/**
 * @internal
 * Memory pool structure.
 *     The behavior of ib_mpool_t can be changed by tuning sizes and definitions
 *     but right now, this is how empty memory (not allocated) of buffers
 *     is indexed:
 *
 *     indexed[] will hold the buffers in such way that buffers linked from
 *     indexed[0] guarantee to have available memory (free) of at
 *                least 16 bytes and less than 32
 *     indexed[1] -> at least 32 and less than 64
 *     indexed[2] -> at least 64 and less than 128
 *     ...           ...
 *     indexed[7] -> at least 2048 and no limit here. We should set one.
 *     When ib_mpool_(c?alloc|memdup) is called, the ib_mpool_buffer_t
 *     will reserve size for the allocation. The remaining buffer available will
 *     determine the index at indexed[] where the buffer will be moved.
 *     This allow to reutilize buffers in order to reduce fragmentation.
 *     The limit is 7 in the current definition, but it should be possible to
 *     increase it by just changing the sizes / limits
 */
struct ib_mpool_t {
    ib_mpool_buffer_t      *busy_buffers; /**< List of reserved buffers */
    ib_mpool_buffer_t      *current;      /**< Points to the last buffer used
                                               (but not the next one to use!)*/
    size_t                  size;         /**< Sum of all buffer sizes */
    size_t                  buffer_cnt;   /**< Counter of buffers allocated
                                               (with malloc)*/
    size_t                  inuse;        /**< Number of bytes in real use */
    size_t                  page_size;    /**< default page size */

    ib_mpool_t             *parent;       /**< Pointer to parent pool
                                               (used only if it's a child) */
    ib_mpool_t             *next;         /**< Sibling next */
    ib_mpool_t             *prev;         /**< Sibling prev */
    ib_mpool_t             *child;        /**< Pointer to child list */

    ib_mpool_cleanup_fn_t   free;         /**< Function cleanup callback */
    void                   *free_data;    /**< Data to pass to the callback */

    ib_mpool_buffer_t        *indexed[IB_MPOOL_MAX_INDEX + 1];
    /* @todo threads? we will need a spinlock/mutex here to lock
       on allocs/free()s */
};

/* Helper Macros for ib_mpool_t / ib_mpool_buffer_t */

/**
 * @internal
 * Allocates mem for a var in a buffer. The addres is pointed by ptr.
 *
 * @param buf Pointer to the buffer
 * @param rsize Size to allocate
 * @param ptr Pointer to the start of the mem allocated
 */
#define IB_MPOOL_BUFFER_ALLOC(buf,rsize,ptr) \
        do { \
         (ptr) = (buf)->buffer + (buf)->used; \
         (buf)->used += (rsize); \
        } while (0)

/**
 * @internal
 * Reset the allocations of vars inside a buffer
 *
 * @param buf Pointer to the buffer
 */
#define IB_MPOOL_BUFFER_RESET(buf) \
        do { \
         (buf)->used = 0; \
        } while (0)

/**
 * @internal
 * Returns the available mem (free mem) of a buffer
 *
 * @param buf Pointer to the buffer
 */
#define IB_MPOOL_BUFFER_AVAILABLE(buf) \
        ((int)((int)(buf)->size - (int)(buf)->used))

/**
 * @internal
 * Determines if certain size can be allocated in a buffer
 *
 * @param buf Pointer to the buffer
 * @param rsize Size to check
 */
#define IB_MPOOL_BUFFER_IS_AVAILABLE(buf,rsize) \
        (( (int)((int)(buf)->size - (int)(buf)->used) > (int)(rsize)))

/**
 * @internal
 * Creates a new buffer of size rsize
 *
 * @param buf Pointer to the buffer
 * @param rsize Size of the buffer
 */
#define IB_MPOOL_CREATE_BUFFER(buf,rsize) \
    do { \
     (buf) = (ib_mpool_buffer_t *)malloc(sizeof(ib_mpool_buffer_t)); \
     if ((buf) != NULL) { \
      (buf)->size = (rsize); \
      (buf)->buffer = (uint8_t *)malloc((rsize)); \
      if ((buf)->buffer == NULL) { \
       free(buf); \
       buf = NULL; \
      } \
      else { \
       (buf)->used = 0; \
      } \
     } \
    } while(0)

/**
 * @internal
 * Adds a buffer to the indexed[] lists
 *
 * @param pool Pointer to the pool
 * @param rbuf Pointer to the buffer
 * @param rindex Index position of indexed[] (calculated by IB_MPOOL_SET_INDEX)
 */
#define IB_MPOOL_ADD_BUFFER(pool,rbuf,rindex) \
    do { \
     (rbuf)->prev = NULL; \
     (rbuf)->next = (pool)->indexed[(rindex)]; \
     if ((rbuf)->next != NULL) { \
         (rbuf)->next->prev = (rbuf); \
     } \
     (pool)->indexed[(rindex)] = (rbuf); \
     (pool)->size += (rbuf)->size; \
     (pool)->buffer_cnt += 1; \
     (pool)->current = (rbuf); \
    } while (0) 

/**
 * @internal
 * Get the index that should be used for a buffer with 'size' available
 *
 * @param size Size of empty mem at the buffer to index
 * @param rindex Index to be used
 */
#define IB_MPOOL_SET_INDEX(size,rindex) \
    do { \
     size_t sz = 0; \
     for (sz = (size) >> IB_MPOOL_MIN_SIZE_BITS; sz >> (rindex); (rindex)++); \
     if ((rindex) > 0) {\
      (rindex)--; \
     } \
     if ((rindex) > IB_MPOOL_MAX_INDEX) { \
      (rindex) = IB_MPOOL_MAX_INDEX; \
     } \
    }while(0)

/**
 * @internal
 * Get the starting index where a buffer ready to allocate 'size' can be found
 *
 * @param size Size of the var to be allocated
 * @param rindex Index to be used
 */
#define IB_MPOOL_GET_REQ_INDEX(size,rindex) \
    do { \
     size_t sz = 0; \
     for (sz = (size) >> IB_MPOOL_MIN_SIZE_BITS; sz >> (rindex); (rindex)++); \
     if ((rindex) > IB_MPOOL_MAX_INDEX) { \
      (rindex) = IB_MPOOL_MAX_INDEX; \
     } \
    }while(0)


/**
 * @internal
 * Dynamic Shared Object (DSO) structure.
 */
struct ib_dso_t {
    ib_mpool_t          *mp;            /**< Memory pool */
    void                *handle;        /**< Real DSO handle */
};

struct ib_hash_entry_t {
    const void          *key;
    size_t               len;
    const void          *data;
    unsigned int         hash;
    ib_hash_entry_t     *next;
};

struct ib_hash_iter_t {
    ib_hash_t           *cur_ht;
    ib_hash_entry_t     *cur_entry;
    ib_hash_entry_t     *next;
    unsigned int         index;
};

/**
 * @internal
 * Hash Table structure.
 */
struct ib_hash_t {
    uint8_t              flags;
    ib_hashfunc_t        hash_fn;
    ib_hash_entry_t    **slots;
    unsigned int         size;
    ib_hash_iter_t       iterator;  /* For ib_internalhash_first(NULL, ...) */
    ib_mpool_t          *mp;        /**< Mem pool */
    ib_hash_entry_t     *free;
    unsigned int         cnt;
};

/**
 * @internal
 * Field value structure.
 *
 * This allows for multiple types of values to be stored within a field.
 */
struct ib_field_val_t {
    ib_field_get_fn_t  fn_get;        /**< Function to get a value. */
#if 0
    ib_field_set_fn_t  fn_set;        /**< Function to set a value. */
    ib_field_rset_fn_t fn_rset;       /**< Function to set a relative value. */
#endif
    void              *fndata;        /**< Data passed to function calls. */
    void               *pval;         /**< Address where value is stored */
    union {
        ib_num_t       num;           /**< Generic numeric value */
        ib_unum_t      unum;          /**< Generic unsigned numeric value */
        ib_bytestr_t  *bytestr;       /**< Byte string value */
        char          *nulstr;        /**< NUL string value */
        ib_list_t     *list;          /**< List of fields */
        void          *ptr;           /**< Pointer value */
    } u;
};

/**
 * @internal
 * Dynamic array structure.
 */
struct ib_array_t {
    ib_mpool_t       *mp;
    size_t            ninit;
    size_t            nextents;
    size_t            nelts;
    size_t            size;
    void             *extents;
};

/**
 * @internal
 * Calculate the extent index from the array index for an array.
 *
 * @param arr Array
 * @param idx Array index
 *
 * @returns Extent index where data resides
 */
#define IB_ARRAY_EXTENT_INDEX(arr,idx) ((idx) / (arr)->ninit)

/**
 * @internal
 * Calculate the data index from the array and extent indexes for an array.
 *
 * @param arr Array
 * @param idx Array index
 * @param extent_idx Extent index (via @ref IB_ARRAY_EXTENT_INDEX)
 *
 * @returns Data index where data resides within the given extent
 */
#define IB_ARRAY_DATA_INDEX(arr,idx,extent_idx) ((idx) - ((extent_idx) * (arr)->ninit))

/**
 * @internal
 * Set to 1 the specified bit index of a byte array
 * Warning: The bit offset/index starts from the HSB
 *
 * @param byte Array of byte (uint8_t*)
 * @param bit index of bit
 */
#define IB_SET_BIT_ARRAY(byte, bit)      (byte[bit / 8] |= (0x01 << (7 - (bit % 8))));

/**
 * @internal
 * Read a bit from the specified byte
 * Warning: The bit offset/index starts from the HSB
 *
 * @param byte Byte to look at (uint8_t)
 * @param bit index of bit
 * @returns 0 or 1
 */
#define IB_READ_BIT(byte, bit)             ((byte >> (7 - ((bit) % 8)) ) & 0x01)

/**
 * @internal
 * Calculate the size in bytes to hold a prefix of length bits
 *
 * @param bits The number of bits we want to store
 * @returns size in bytes needed for that bits
 */
#define IB_BITS_TO_BYTES(bits)           (((bits) % 8 == 0) ? ((bits) / 8) : ((bits) / 8) + 1)

/**
 * @internal
 * Set to 1 the specified bit index of a byte
 * Warning: The bit offset/index starts from the HSB
 *
 * @param byte Byte to look at (uint8_t)
 * @param bit index of bit
 */
#define IB_SET_BIT(byte, bit)            (byte |= (0x01 << (7 - (bit % 8))));

/**
 * @internal
 * Read the HSB of a byte
 *
 * @param byte Byte to look at (uint8_t)
 * @returns 0 or 1
 */
#define IB_GET_DIR(byte)                (((byte) >> 7) & 0x01)

/**
 * @internal
 * Prefix for radix nodes
 */
struct ib_radix_prefix_t {
    uint8_t *rawbits;
    uint8_t prefixlen;
};

/**
 * @internal
 * Radix node structure
 */
struct ib_radix_node_t {
    ib_radix_prefix_t *prefix;

    struct ib_radix_node_t *zero;
    struct ib_radix_node_t *one;

    void *data;
};

/**
 * @internal
 * Radix tree structure
 */
struct ib_radix_t {
    ib_radix_node_t *start;

    ib_radix_update_fn_t update_data;
    ib_radix_print_fn_t  print_data;
    ib_radix_free_fn_t   free_data;

    size_t data_cnt;

    ib_mpool_t *mp;
};

/**
 * @internal
 * Matching functions type helper
 */
enum {
    IB_RADIX_PREFIX,
    IB_RADIX_CLOSEST,
};

/**
 * @internal
 * Return if the given prefix is IPV4
 *
 * @param cidr const char * with format ip/mask where mask is optional
 * @returns 1 if true, 0 if false
 */
#define IB_RADIX_IS_IPV4(cidr) ((strchr(cidr, ':') == NULL) ? 1 : 0)

/**
 * @internal
 * Return if the given prefix is IPV6
 *
 * @param cidr const char * with format ip/mask where mask is optional
 * @returns 1 if true, 0 if false
 */
#define IB_RADIX_IS_IPV6(cidr) ((strchr(cidr, ':') != NULL) ? 1 : 0)


/**
 *@ internal
 * bin tree for fast goto() function of aho corasick
 */
typedef struct ib_ac_bintree_t ib_ac_bintree_t;

/**
 * @internal
 * Aho Corasick state state, used to represent a state
 */
struct ib_ac_state_t {
    ib_ac_char_t       letter;    /**< the char to go to this state */

    uint8_t            flags;     /**< flags for this state */
    size_t             level;     /**< level in the tree (depth/length) */

    ib_ac_state_t     *fail;      /**< state to go to if goto() fail */
    ib_ac_state_t     *outputs;   /**< pointer to other matching states */

    ib_ac_state_t     *child;     /**< next state to goto() of next level */
    ib_ac_state_t     *sibling;   /**< sibling state (linked list) */
    ib_ac_state_t     *parent;    /**< parent state */

    ib_ac_bintree_t   *bintree;   /**< bintree to speed up the goto() search*/

    uint32_t           match_cnt; /**< match count of this state */

    ib_ac_char_t      *pattern;   /**< (sub) pattern path to this state */

    ib_ac_callback_t   callback;  /**< callback function for matches */
    void              *data;      /**< callback (or match entry) extra params */

};

/**
 * @internal
 * binary tree that performs the Aho Corasick goto function for a given
 * state and letter
 */
struct ib_ac_bintree_t {
    ib_ac_char_t       letter;    /**< the current char */
    ib_ac_state_t     *state;     /**< the goto() state for letter */

    ib_ac_bintree_t   *left;      /**< chars lower than current */
    ib_ac_bintree_t   *right;     /**< chars greater than current */
};
#endif /* IB_UTIL_PRIVATE_H_ */

