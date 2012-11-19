#include "ruby/re.h"

#ifndef GC_PROFILE_MORE_DETAIL
#define GC_PROFILE_MORE_DETAIL 0
#endif

typedef struct gc_profile_record {
    double gc_time;
    double gc_invoke_time;

    size_t heap_total_objects;
    size_t heap_use_size;
    size_t heap_total_size;

    int is_marked;

#if GC_PROFILE_MORE_DETAIL
    double gc_mark_time;
    double gc_sweep_time;

    size_t heap_use_slots;
    size_t heap_live_objects;
    size_t heap_free_objects;

    int have_finalize;

    size_t allocate_increase;
    size_t allocate_limit;
#endif
} gc_profile_record;


#if defined(_MSC_VER) || defined(__BORLANDC__) || defined(__CYGWIN__)
#pragma pack(push, 1) /* magic for reducing sizeof(RVALUE): 24 -> 20 */
#endif

typedef struct RVALUE {
    union {
  struct {
      VALUE flags;    /* always 0 for freed obj */
      struct RVALUE *next;
  } free;
  struct RBasic  basic;
  struct RObject object;
  struct RClass  klass;
  struct RFloat  flonum;
  struct RString string;
  struct RArray  array;
  struct RRegexp regexp;
  struct RHash   hash;
  struct RData   data;
  struct RTypedData   typeddata;
  struct RStruct rstruct;
  struct RBignum bignum;
  struct RFile   file;
  struct RNode   node;
  struct RMatch  match;
  struct RRational rational;
  struct RComplex complex;
    } as;
#ifdef GC_DEBUG
    const char *file;
    int   line;
#endif
} RVALUE;

#if defined(_MSC_VER) || defined(__BORLANDC__) || defined(__CYGWIN__)
#pragma pack(pop)
#endif

struct heaps_slot {
    void *membase;
    RVALUE *slot;
    size_t limit;
    uintptr_t *bits;
    RVALUE *freelist;
    struct heaps_slot *next;
    struct heaps_slot *prev;
    struct heaps_slot *free_next;
};

struct heaps_header {
    struct heaps_slot *base;
    uintptr_t *bits;
};

struct sorted_heaps_slot {
    RVALUE *start;
    RVALUE *end;
    struct heaps_slot *slot;
};

struct heaps_free_bitmap {
    struct heaps_free_bitmap *next;
};

struct gc_list {
    VALUE *varptr;
    struct gc_list *next;
};

#define STACK_CHUNK_SIZE 500

typedef struct stack_chunk {
    VALUE data[STACK_CHUNK_SIZE];
    struct stack_chunk *next;
} stack_chunk_t;

typedef struct mark_stack {
    stack_chunk_t *chunk;
    stack_chunk_t *cache;
    size_t index;
    size_t limit;
    size_t cache_size;
    size_t unused_cache_size;
} mark_stack_t;

#ifndef CALC_EXACT_MALLOC_SIZE
#define CALC_EXACT_MALLOC_SIZE 0
#endif

typedef struct rb_objspace {
    struct {
  size_t limit;
  size_t increase;
#if CALC_EXACT_MALLOC_SIZE
  size_t allocated_size;
  size_t allocations;
#endif
    } malloc_params;
    struct {
  size_t increment;
  struct heaps_slot *ptr;
  struct heaps_slot *sweep_slots;
  struct heaps_slot *free_slots;
  struct sorted_heaps_slot *sorted;
  size_t length;
  size_t used;
        struct heaps_free_bitmap *free_bitmap;
  RVALUE *range[2];
  RVALUE *freed;
  size_t live_num;
  size_t free_num;
  size_t free_min;
  size_t final_num;
  size_t do_heap_free;
    } heap;
    struct {
  int dont_gc;
  int dont_lazy_sweep;
  int during_gc;
  rb_atomic_t finalizing;
    } flags;
    struct {
  st_table *table;
  RVALUE *deferred;
    } final;
    mark_stack_t mark_stack;
    struct {
  int run;
  gc_profile_record *record;
  size_t count;
  size_t size;
  double invoke_time;
    } profile;
    struct gc_list *global_list;
    size_t count;
    int gc_stress;

    struct mark_func_data_struct {
  void *data;
  void (*mark_func)(VALUE v, void *data);
    } *mark_func_data;
} rb_objspace_t;

#define malloc_limit    objspace->malloc_params.limit
#define malloc_increase   objspace->malloc_params.increase
#define heaps     objspace->heap.ptr
#define heaps_length    objspace->heap.length
#define heaps_used    objspace->heap.used
#define lomem     objspace->heap.range[0]
#define himem     objspace->heap.range[1]
#define heaps_inc   objspace->heap.increment
#define heaps_freed   objspace->heap.freed
#define dont_gc     objspace->flags.dont_gc
#define during_gc   objspace->flags.during_gc
#define finalizing    objspace->flags.finalizing
#define finalizer_table   objspace->final.table
#define deferred_final_list objspace->final.deferred
#define global_List   objspace->global_list
#define ruby_gc_stress    objspace->gc_stress
#define initial_malloc_limit  initial_params.initial_malloc_limit
#define initial_heap_min_slots  initial_params.initial_heap_min_slots
#define initial_free_min  initial_params.initial_free_min

#define is_lazy_sweeping(objspace) ((objspace)->heap.sweep_slots != 0)

#define nonspecial_obj_id(obj) (VALUE)((SIGNED_VALUE)(obj)|FIXNUM_FLAG)

#define RANY(o) ((RVALUE*)(o))
#define has_free_object (objspace->heap.free_slots && objspace->heap.free_slots->freelist)

#define HEAP_HEADER(p) ((struct heaps_header *)(p))
#define GET_HEAP_HEADER(x) (HEAP_HEADER((uintptr_t)(x) & ~(HEAP_ALIGN_MASK)))
#define GET_HEAP_SLOT(x) (GET_HEAP_HEADER(x)->base)
#define GET_HEAP_BITMAP(x) (GET_HEAP_HEADER(x)->bits)
#define NUM_IN_SLOT(p) (((uintptr_t)(p) & HEAP_ALIGN_MASK)/sizeof(RVALUE))
#define BITMAP_INDEX(p) (NUM_IN_SLOT(p) / (sizeof(uintptr_t) * CHAR_BIT))
#define BITMAP_OFFSET(p) (NUM_IN_SLOT(p) & ((sizeof(uintptr_t) * CHAR_BIT)-1))
#define MARKED_IN_BITMAP(bits, p) (bits[BITMAP_INDEX(p)] & ((uintptr_t)1 << BITMAP_OFFSET(p)))

//
#define RANY(o) ((RVALUE*)(o))
//

static inline int
is_pointer_to_heap(rb_objspace_t *objspace, void *ptr)
{
    register RVALUE *p = RANY(ptr);
    register struct sorted_heaps_slot *heap;
    register size_t hi, lo, mid;

    if (p < lomem || p > himem) return FALSE;
    if ((VALUE)p % sizeof(RVALUE) != 0) return FALSE;

    /* check if p looks like a pointer using bsearch*/
    lo = 0;
    hi = heaps_used;
    while (lo < hi) {
  mid = (lo + hi) / 2;
  heap = &objspace->heap.sorted[mid];
  if (heap->start <= p) {
      if (p < heap->end)
    return TRUE;
      lo = mid + 1;
  }
  else {
      hi = mid;
  }
    }
    return FALSE;
}

//from count_objects:
#define FOR_EACH_HEAP_SLOT(p) for (i = 0; i < heaps_used; i++) {\
      RVALUE *p = objspace->heap.sorted[i].start, *pend = objspace->heap.sorted[i].end;\
      if(!p) continue;\
      for (; p < pend; p++) {
#define FOR_EACH_HEAP_SLOT_END(total) } total += objspace->heap.sorted[i].slot->limit; }

#define NODE_OPTBLOCK 1000000 //FIXME
