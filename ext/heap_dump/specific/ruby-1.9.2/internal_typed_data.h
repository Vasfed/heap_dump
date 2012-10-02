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