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

extern VALUE ruby_engine_name;