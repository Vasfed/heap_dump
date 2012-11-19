 //FIXME: autogen this from ruby (this copied from 1.9.3p194)

// thread.c:
struct thgroup {
  int enclosed;
  VALUE group;
};

// enumerator.c:
struct enumerator {
    VALUE obj;
    ID    meth;
    VALUE args;
    VALUE fib;
    VALUE dst;
    VALUE lookahead;
    VALUE feedvalue;
    VALUE stop_exc;
};

//
struct generator {
    VALUE proc;
};

struct yielder {
    VALUE proc;
};


// proc.c:
struct METHOD {
    VALUE recv;
    VALUE rclass;
    VALUE defined_class; //FIXME: dump this
    ID id;
    rb_method_entry_t *me;
    struct unlinked_method_entry_list_entry *ume;
};

//
#define METHOD_DEFINITIONP(m) (m->me ? m->me->def : NULL)

//class.c:
#define HAVE_RB_CLASS_TBL 1
//For som reason this fails to link directly on 1.9.3 :(

//HACK:
#include <dlfcn.h>

inline st_table * rb_get_class_tbl(){
  Dl_info info;
  void* image;
  if(!dladdr(rb_intern, &info) || !info.dli_fname){
    return NULL;
  }
  image = dlopen(info.dli_fname, RTLD_NOLOAD | RTLD_GLOBAL);
  // printf("Image is %p, addr is %p (%p rel)\n", image, rb_intern, ((void*)rb_intern - image));
  if(image)
  {
    void* tbl = dlsym(image, "_rb_class_tbl");
    dlclose(image);
    if(tbl)
      return tbl;
  }

  //TODO: parse sym table and calculate address?

  return NULL;
}

#define ruby_current_thread ((rb_thread_t *)RTYPEDDATA_DATA(rb_thread_current()))
#define GET_THREAD() ruby_current_thread

//FIXME: get global const for it:  rb_define_global_const("RUBY_ENGINE", ruby_engine_name = MKSTR(engine));
#define ruby_engine_name Qnil

#define ID_ALLOCATOR 0

//vm_trace.c
typedef enum {
    RUBY_HOOK_FLAG_SAFE    = 0x01,
    RUBY_HOOK_FLAG_DELETED = 0x02,
    RUBY_HOOK_FLAG_RAW_ARG = 0x04
} rb_hook_flag_t;
typedef struct rb_event_hook_struct {
    rb_hook_flag_t hook_flags;
    rb_event_flag_t events;
    rb_event_hook_func_t func;
    VALUE data;
    struct rb_event_hook_struct *next;
} rb_event_hook_t;

//vm_backtrace.c
inline static int
calc_lineno(const rb_iseq_t *iseq, const VALUE *pc)
{
    return rb_iseq_line_no(iseq, pc - iseq->iseq_encoded);
}

int rb_vm_get_sourceline(const rb_control_frame_t * cfp){
  int lineno = 0;
  const rb_iseq_t *iseq = cfp->iseq;

  if (RUBY_VM_NORMAL_ISEQ_P(iseq)) {
  lineno = calc_lineno(cfp->iseq, cfp->pc);
  }
  return lineno;
}
