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
    ID id;
    rb_method_entry_t *me;
    struct unlinked_method_entry_list_entry *ume;
};

//
#define METHOD_DEFINITIONP(m) (m->me ? m->me->def : NULL)

//class.c:
// #define HAVE_RB_CLASS_TBL 1
//For som reason this fails to link on 1.9.3 :(
// extern st_table *rb_class_tbl;

#define ruby_current_thread ((rb_thread_t *)RTYPEDDATA_DATA(rb_thread_current()))

//FIXME: get global const for it:  rb_define_global_const("RUBY_ENGINE", ruby_engine_name = MKSTR(engine));
#define ruby_engine_name Qnil
