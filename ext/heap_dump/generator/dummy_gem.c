//used for separating non-public types from ruby source
//TODO: DRY ../ext/heap_dump/heap_dump.c
#include "ruby.h"
#include "ruby/encoding.h"
#include <stdlib.h>
#include <stdio.h>


#include "constant.h"


#include "node.h"
#include "vm_core.h"
// #include "atomic.h"
#include "iseq.h"

#ifdef HAVE_GC_H
#include "gc.h"
#endif

#ifdef HAVE_INTERNAL_H
#include "internal.h"
#else
#define RCLASS_EXT(c) (RCLASS(c)->ptr)
#endif

#define NODE_OP_ASGN2_ARG NODE_LAST + 1

#ifndef FALSE
# define FALSE 0
#elif FALSE
# error FALSE must be false
#endif
#ifndef TRUE
# define TRUE 1
#elif !TRUE
# error TRUE must be true
#endif

#include "method.h"

// #include "ruby_io.h" // need rb_io_t

// #include "api/yajl_gen.h"//

#ifndef RUBY_VM
#error No RUBY_VM, old rubies not supported
#endif

// simple test - rake compile && bundle exec ruby -e 'require "heap_dump"; HeapDump.dump'

#include <dlfcn.h>

#ifdef HAVE_GC_INTERNAL_H
// #include "gc_internal.h"
#else
  // #error No internal gc header for your ruby
  //TODO: just do not dump something?
#endif

//#include "fiber.h"
// #include "internal_typed_data.h"


void main(){}