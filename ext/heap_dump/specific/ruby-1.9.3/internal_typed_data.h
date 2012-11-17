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
#define HAVE_RB_CLASS_TBL 1
//For som reason this fails to link directly on 1.9.3 :(

//HACK:
// otool -L `which ruby`
// /Users/vasfed/.rvm/rubies/ruby-1.9.3-p194/bin/ruby:
//   @executable_path/../lib/libruby.1.9.1.dylib (compatibility version 1.9.1, current version 1.9.1)
// nm ~/.rvm/rubies/ruby-1.9.3-p194/lib/libruby.1.9.1.dylib | grep rb_class_tbl
// 000000000024be28 s _rb_class_tbl
// 00000000000b311c T _rb_intern
// 000000000024bd38 S _rb_mKernel

#include <dlfcn.h>

inline st_table * rb_get_class_tbl(){
  Dl_info info;
  if(!dladdr(rb_intern, &info) || !info.dli_fname){
    return NULL;
  }
  void* image = dlopen(info.dli_fname, RTLD_NOLOAD | RTLD_GLOBAL);
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

//FIXME: get global const for it:  rb_define_global_const("RUBY_ENGINE", ruby_engine_name = MKSTR(engine));
#define ruby_engine_name Qnil
