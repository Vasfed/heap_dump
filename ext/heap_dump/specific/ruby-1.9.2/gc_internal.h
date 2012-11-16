//extracted from ruby 1.9.2 p290, should be compatible with all 1.9.2's

//TODO: does conflict with ruby framework?
#include "ruby/re.h"

//FIXME: this should be autoextracted from ruby
// see how this is done in ruby-internal gem


#define MARK_STACK_MAX 1024

#ifndef CALC_EXACT_MALLOC_SIZE
#define CALC_EXACT_MALLOC_SIZE 0
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

struct heaps_slot {
    void *membase;
    RVALUE *slot;
    size_t limit;
    int finalize_flag;
};

struct heaps_header {
    struct heaps_slot *base;
    uintptr_t *bits;
};

struct gc_list {
    VALUE *varptr;
    struct gc_list *next;
};


// 1.9.2-p290:
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
  size_t length;
  size_t used;
  RVALUE *freelist;
  RVALUE *range[2];
  RVALUE *freed;
    } heap;
    struct {
  int dont_gc;
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
    unsigned int count;
    int gc_stress;
} rb_objspace_t;

//
#define RANY(o) ((RVALUE*)(o))

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
#define mark_stack    objspace->markstack.buffer
#define mark_stack_ptr    objspace->markstack.ptr
#define mark_stack_overflow objspace->markstack.overflow
#define global_List   objspace->global_list


//1.9.2-p290
static inline int
is_pointer_to_heap(rb_objspace_t *objspace, void *ptr)
{
    register RVALUE *p = RANY(ptr);
    register struct heaps_slot *heap;
    register size_t hi, lo, mid;

    if (p < lomem || p > himem) return FALSE;
    if ((VALUE)p % sizeof(RVALUE) != 0) return FALSE;

  /* check if p looks like a pointer using bsearch*/
      lo = 0;
      hi = heaps_used;
      while (lo < hi) {
    mid = (lo + hi) / 2;
    heap = &heaps[mid];
    if (heap->slot <= p) {
        if (p < heap->slot + heap->limit)
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
      RVALUE *p = heaps[i].slot; RVALUE *pend = p + heaps[i].limit;\
      if(!p || p < heaps[i].membase) continue;\
      for (; p < pend; p++) {
#define FOR_EACH_HEAP_SLOT_END(total) } total += heaps[i].limit; }
