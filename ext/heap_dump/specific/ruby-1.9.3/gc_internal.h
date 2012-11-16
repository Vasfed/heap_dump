//TODO: does conflict with ruby framework?
#include "ruby/re.h"

//gc.c

#define MARK_STACK_MAX 1024
//
/* for GC profile */
#define GC_PROFILE_MORE_DETAIL 0
typedef struct gc_profile_record {
    double gc_time;
    double gc_mark_time;
    double gc_sweep_time;
    double gc_invoke_time;

    size_t heap_use_slots;
    size_t heap_live_objects;
    size_t heap_free_objects;
    size_t heap_total_objects;
    size_t heap_use_size;
    size_t heap_total_size;

    int have_finalize;
    int is_marked;

    size_t allocate_increase;
    size_t allocate_limit;
} gc_profile_record;
//

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
    struct heaps_slot *next;
    struct heaps_slot *prev;
};

struct sorted_heaps_slot {
    RVALUE *start;
    RVALUE *end;
    struct heaps_slot *slot;
};

struct gc_list {
    VALUE *varptr;
    struct gc_list *next;
};

#define CALC_EXACT_MALLOC_SIZE 0

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
  struct sorted_heaps_slot *sorted;
  size_t length;
  size_t used;
  RVALUE *freelist;
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
    } flags;
    struct {
  st_table *table;
  RVALUE *deferred;
    } final;
    struct {
  VALUE buffer[MARK_STACK_MAX];
  VALUE *ptr;
  int overflow;
    } markstack;
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
} rb_objspace_t;

//

#define malloc_limit    objspace->malloc_params.limit
#define malloc_increase   objspace->malloc_params.increase
#define heaps     objspace->heap.ptr
#define heaps_length    objspace->heap.length
#define heaps_used    objspace->heap.used
#define freelist    objspace->heap.freelist
#define lomem     objspace->heap.range[0]
#define himem     objspace->heap.range[1]
#define heaps_inc   objspace->heap.increment
#define heaps_freed   objspace->heap.freed
#define dont_gc     objspace->flags.dont_gc
#define during_gc   objspace->flags.during_gc
#define finalizer_table   objspace->final.table
#define deferred_final_list objspace->final.deferred
#define mark_stack    objspace->markstack.buffer
#define mark_stack_ptr    objspace->markstack.ptr
#define mark_stack_overflow objspace->markstack.overflow
#define global_List   objspace->global_list
#define ruby_gc_stress    objspace->gc_stress
#define initial_malloc_limit  initial_params.initial_malloc_limit
#define initial_heap_min_slots  initial_params.initial_heap_min_slots
#define initial_free_min  initial_params.initial_free_min

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

#define FOR_EACH_HEAP_SLOT(p) for (i = 0; i < heaps_used; i++) {\
      RVALUE *p = objspace->heap.sorted[i].start, *pend = objspace->heap.sorted[i].end;\
      if(!p) continue;\
      for (; p < pend; p++) {
#define FOR_EACH_HEAP_SLOT_END(total) } total += objspace->heap.sorted[i].slot->limit; }

