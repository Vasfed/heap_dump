  //FIXME: autogen this from ruby (this copied from 1.9.2p290)
struct thgroup {
  int enclosed;
  VALUE group;
};


  //

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

struct generator {
  VALUE proc;
};

struct yielder {
  VALUE proc;
};

//
struct METHOD {
    VALUE recv;
    VALUE rclass;
    ID id;
    rb_method_entry_t me;
};

//

#define METHOD_DEFINITIONP(m) m->me.def

#define HAVE_RB_CLASS_TBL 1

inline st_table * rb_get_class_tbl(){
  //class.c:
  extern st_table *rb_class_tbl;
  return rb_class_tbl;
}

#define HAVE_RB_GLOBAL_TBL 1
inline st_table * rb_get_global_tbl(){
  //class.c:
  extern st_table *rb_global_tbl;
  return rb_global_tbl;
}
#define gvar_getter_t rb_gvar_getter_t
#define gvar_setter_t rb_gvar_setter_t
#define gvar_marker_t rb_gvar_marker_t

struct trace_var {
    int removed;
    void (*func)(VALUE arg, VALUE val);
    VALUE data;
    struct trace_var *next;
};

//struct global_variable {
struct rb_global_variable {
    int   counter;
    void *data;
    gvar_getter_t *getter;
    gvar_setter_t *setter;
    gvar_marker_t *marker;
    int block_trace;
    struct trace_var *trace;
};

extern VALUE ruby_engine_name;