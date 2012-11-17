#include "ruby.h"
#include "ruby/encoding.h"
#include <stdlib.h>
#include <stdio.h>


#ifdef HAVE_CONSTANT_H
//have this in 1.9.3
#include "constant.h"
#else
#include "internal_constant.h"
#endif

#include "node.h"
#include "vm_core.h"
// #include "atomic.h"
#include "iseq.h"

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

#include "ruby_io.h" // need rb_io_t

#include "api/yajl_gen.h"

#ifndef RUBY_VM
#error No RUBY_VM, old rubies not supported
#endif

// simple test - rake compile && bundle exec ruby -e 'require "heap_dump"; HeapDump.dump'

#include <dlfcn.h>

#ifdef HAVE_GC_INTERNAL_H
#include "gc_internal.h"
#else
  #error No internal gc header for your ruby
  //TODO: just do not dump something?
#endif

#include "fiber.h"
#include "internal_typed_data.h"


static VALUE rb_mHeapDumpModule;

static ID classid;

//shortcuts to yajl
#define YAJL ctx->yajl
#define yg_string(str,len) yajl_gen_string(YAJL, str, len)
#define yg_cstring(str) yg_string(str, (unsigned int)strlen(str))
#define yg_rstring(str) yg_string(RSTRING_PTR(str), (unsigned int)RSTRING_LEN(str))
#define yg_int(i) yajl_gen_integer(YAJL, i)
#define yg_double(d) (yajl_gen_double(YAJL, d)==yajl_gen_invalid_number? yg_cstring("inf|NaN") : true)
#define yg_null() yajl_gen_null(YAJL)
#define yg_bool(b) yajl_gen_bool(YAJL, b);

//#define yg_id(obj) yg_int(NUM2LONG(rb_obj_id(obj)))
#define yg_id(obj) yg_id1(obj,ctx)


#define ygh_id(key,obj) {yg_cstring(key); yg_id(obj);}
#define ygh_int(key,i) {yg_cstring(key); yg_int((long int)(i));}
#define ygh_double(key,d) {yg_cstring(key); yg_double(d);}
#define ygh_string(key,str,len) {yg_cstring(key); yg_string(str,len);}
#define ygh_cstring(key,str) {yg_cstring(key); yg_cstring(str);}
#define ygh_rstring(key,str) {yg_cstring(key); yg_rstring(str);}

#define yg_map() yajl_gen_map_open(YAJL);
#define yg_map_end() yajl_gen_map_close(YAJL);
#define yg_array() yajl_gen_array_open(YAJL);
#define yg_array_end() yajl_gen_array_close(YAJL);


// context for objectspace_walker callback
typedef struct walk_ctx {
  int walker_called;
  int live_objects;
  FILE* file;

  yajl_gen yajl;
} walk_ctx_t;

static void flush_yajl(walk_ctx_t *ctx){
  const unsigned char* buf;
  unsigned int len;
  if(yajl_gen_get_buf(ctx->yajl, &buf, &len) == yajl_gen_status_ok){
    fwrite(buf, len, 1, ctx->file);
    yajl_gen_clear(ctx->yajl);
  }
}

static inline int is_in_heap(void *ptr, void* osp);

static inline const char* rb_type_str(int type){
  switch(type){
    #define T(t) case t: return #t;
    T(T_NONE); T(T_NIL);
    T(T_OBJECT); T(T_CLASS); T(T_ICLASS); T(T_MODULE);
    T(T_SYMBOL); T(T_STRING); T(T_REGEXP); T(T_MATCH);
    T(T_ARRAY); T(T_HASH); T(T_STRUCT);

    T(T_FILE);
    T(T_FIXNUM); T(T_BIGNUM); T(T_FLOAT); T(T_RATIONAL); T(T_COMPLEX);

    T(T_TRUE); T(T_FALSE);
    T(T_DATA);
    T(T_UNDEF);
    T(T_NODE); // code?
    T(T_ZOMBIE);
    #undef T
    default:
      return "_unknown_type_";
  }
}

static inline const char* rb_builtin_type(VALUE obj){
  //NOTE: only for heap objects, on embedded use (TYPE(...))
  return rb_type_str(BUILTIN_TYPE(obj));
}

#define true 1
#define false 0

//FIXME: handle non-ids?
static void yg_id1(VALUE obj, walk_ctx_t* ctx){
  if(!obj) {
    yajl_gen_null(ctx->yajl);
    return;
  }
  if (IMMEDIATE_P(obj)) {
    if (FIXNUM_P(obj)) {
      yg_int(FIX2LONG(obj));
      return;
    }
    if (obj == Qtrue){ yajl_gen_bool(ctx->yajl, true); return; }
    if (SYMBOL_P(obj)) {
      yg_cstring(rb_id2name(SYM2ID(obj)));
      return;
    }
    if (obj == Qundef) { yg_cstring("(undef)"); return; }

    yg_cstring("(unknown)");
    return;
  } else /*non-immediate*/ {
    if (!RTEST(obj)) {
      if (obj == Qnil){
        yajl_gen_null(ctx->yajl);
        return;
      }
      if (obj == Qfalse) {
        yajl_gen_bool(ctx->yajl, false);
        return;
      }
    }
  }

  if(BUILTIN_TYPE(obj) == T_STRING && (!(RBASIC(obj)->flags & RSTRING_NOEMBED))){
    //embedded string
    if(rb_enc_get_index(obj) == rb_usascii_encindex())
      yg_rstring(obj);
    else{
      //FIXME: convert encoding/safe syms etc?
      //yg_cstring("(encoded string)");
      yg_rstring(obj);
    }
    return;
  }

  yg_int(NUM2LONG(rb_obj_id(obj)));
}

const char* node_type_name(const NODE* obj){
#define N(n) case NODE_##n: return #n;
  switch(nd_type(obj)){
    N(ALIAS)
#ifdef HAVE_NODE_ALLOCA
    N(ALLOCA)
#endif
    N(AND) N(ARGS) N(ARGSCAT) N(ARGSPUSH) N(ARRAY) N(ATTRASGN) N(BACK_REF) N(BEGIN) N(BLOCK) N(BLOCK_ARG) N(BLOCK_PASS) N(BMETHOD) N(BREAK)
    N(CALL) N(CASE) N(CDECL) N(CLASS) N(COLON2) N(COLON3) N(CONST) N(CVAR) N(CVASGN) N(CVDECL) N(DASGN) N(DASGN_CURR) N(DEFINED) N(DEFN)
    N(DEFS) N(DOT2) N(DOT3) N(DREGX) N(DREGX_ONCE) N(DSTR) N(DSYM) N(DVAR) N(DXSTR) N(ENSURE) N(EVSTR) N(FALSE) N(FCALL) N(FLIP2) N(FLIP3)
    N(FOR) N(GASGN) N(GVAR) N(HASH) N(IASGN) N(IF) N(IFUNC) N(ITER) N(IVAR) N(LASGN) N(LIT) N(LVAR) N(MASGN) N(MATCH) N(MATCH2) N(MATCH3)
    N(MEMO) N(MODULE) N(NEXT) N(NIL) N(NTH_REF) N(OPT_N) N(OP_ASGN1) N(OP_ASGN2) N(OP_ASGN2_ARG) N(OP_ASGN_AND) N(OP_ASGN_OR) N(OR) N(POSTEXE)
    N(REDO) N(RESBODY) N(RESCUE) N(RETRY) N(RETURN) N(SCLASS) N(SCOPE) N(SELF) N(SPLAT) N(STR) N(SUPER) N(TO_ARY) N(TRUE) N(UNDEF) N(UNTIL)
    N(VALIAS) N(VCALL) N(WHEN) N(WHILE) N(XSTR) N(YIELD) N(ZARRAY) N(ZSUPER) N(LAST)
    default:
      return "unknown";
  };
#undef N
}


static void dump_node_refs(NODE* obj, walk_ctx_t* ctx){
  switch (nd_type(obj)) {
    case NODE_IF:   /* 1,2,3 */
    case NODE_FOR:
    case NODE_ITER:
    case NODE_WHEN:
    case NODE_MASGN:
    case NODE_RESCUE:
    case NODE_RESBODY:
    case NODE_CLASS:
    case NODE_BLOCK_PASS:
      //gc_mark(objspace, (VALUE)obj->as.node.u2.node, lev);
      yg_id((VALUE)obj->u2.node);
      /* fall through */
    case NODE_BLOCK:  /* 1,3 */
    case NODE_OPTBLOCK:
    case NODE_ARRAY:
    case NODE_DSTR:
    case NODE_DXSTR:
    case NODE_DREGX:
    case NODE_DREGX_ONCE:
    case NODE_ENSURE:
    case NODE_CALL:
    case NODE_DEFS:
    case NODE_OP_ASGN1:
      //gc_mark(objspace, (VALUE)obj->as.node.u1.node, lev);
      yg_id((VALUE)obj->u1.node);
      /* fall through */
    case NODE_SUPER:  /* 3 */
    case NODE_FCALL:
    case NODE_DEFN:
    case NODE_ARGS_AUX:
      //ptr = (VALUE)obj->as.node.u3.node;
      yg_id((VALUE)obj->u3.node);
      return; //goto again;

    case NODE_WHILE:  /* 1,2 */
    case NODE_UNTIL:
    case NODE_AND:
    case NODE_OR:
    case NODE_CASE:
    case NODE_SCLASS:
    case NODE_DOT2:
    case NODE_DOT3:
    case NODE_FLIP2:
    case NODE_FLIP3:
    case NODE_MATCH2:
    case NODE_MATCH3:
    case NODE_OP_ASGN_OR:
    case NODE_OP_ASGN_AND:
    case NODE_MODULE:
    case NODE_ALIAS:
    case NODE_VALIAS:
    case NODE_ARGSCAT:
      //gc_mark(objspace, (VALUE)obj->as.node.u1.node, lev);
      yg_id((VALUE)obj->u1.node);
      /* fall through */
    case NODE_GASGN:  /* 2 */
    case NODE_LASGN:
    case NODE_DASGN:
    case NODE_DASGN_CURR:
    case NODE_IASGN:
    case NODE_IASGN2:
    case NODE_CVASGN:
    case NODE_COLON3:
    case NODE_OPT_N:
    case NODE_EVSTR:
    case NODE_UNDEF:
    case NODE_POSTEXE:
      //ptr = (VALUE)obj->as.node.u2.node;
      yg_id((VALUE)obj->u2.node);
      return; //goto again;

    case NODE_HASH: /* 1 */
    case NODE_LIT:
    case NODE_STR:
    case NODE_XSTR:
    case NODE_DEFINED:
    case NODE_MATCH:
    case NODE_RETURN:
    case NODE_BREAK:
    case NODE_NEXT:
    case NODE_YIELD:
    case NODE_COLON2:
    case NODE_SPLAT:
    case NODE_TO_ARY:
      //ptr = (VALUE)obj->as.node.u1.node;
      yg_id((VALUE)obj->u1.node);
      return; //goto again;

    case NODE_SCOPE:  /* 2,3 */ //ANN("format: [nd_tbl]: local table, [nd_args]: arguments, [nd_body]: body");
      //actually this is not present in live ruby 1.9+
      if(obj->nd_tbl){
        ID *tbl = RNODE(obj)->nd_tbl;
        unsigned long i = 0, size = tbl[0];
        tbl++;
        for (; i < size; i++) {
          //TODO: dump local var names?
          // rb_id2name(tbl[i])...
          //yg_id(tbl[i]); //FIXME: these are ids, not values
        }
      }
    case NODE_CDECL:
    case NODE_OPT_ARG:
      //gc_mark(objspace, (VALUE)obj->as.node.u3.node, lev);
      //ptr = (VALUE)obj->as.node.u2.node;
      //goto again;
      yg_id((VALUE)obj->u3.node);
      yg_id((VALUE)obj->u2.node);
      return;

    case NODE_ARGS: /* custom */
      #if 0
      //RUBY 1.9.3
      {
        struct rb_args_info *args = obj->u3.args;
        if (args) {
            if (args->pre_init)    yg_id((VALUE)args->pre_init); //gc_mark(objspace, (VALUE)args->pre_init, lev);
            if (args->post_init)   yg_id((VALUE)args->post_init); //gc_mark(objspace, (VALUE)args->post_init, lev);
            if (args->opt_args)    yg_id((VALUE)args->opt_args); //gc_mark(objspace, (VALUE)args->opt_args, lev);
            if (args->kw_args)     yg_id((VALUE)args->kw_args); //gc_mark(objspace, (VALUE)args->kw_args, lev);
            if (args->kw_rest_arg) yg_id((VALUE)args->kw_rest_arg); //gc_mark(objspace, (VALUE)args->kw_rest_arg, lev);
        }
      }
      //ptr = (VALUE)obj->as.node.u2.node;
      yg_id(obj->u2.node);
      //goto again;
      #endif
      yg_id((VALUE)obj->u1.node);
      return;

    case NODE_ZARRAY: /* - */
    case NODE_ZSUPER:
    case NODE_VCALL:
    case NODE_GVAR:
    case NODE_LVAR:
    case NODE_DVAR:
    case NODE_IVAR:
    case NODE_CVAR:
    case NODE_NTH_REF:
    case NODE_BACK_REF:
    case NODE_REDO:
    case NODE_RETRY:
    case NODE_SELF:
    case NODE_NIL:
    case NODE_TRUE:
    case NODE_FALSE:
    case NODE_ERRINFO:
    case NODE_BLOCK_ARG:
      break;
    case NODE_ALLOCA:
      //mark_locations_array(objspace, (VALUE*)obj->as.node.u1.value, obj->as.node.u3.cnt); :
      {
        VALUE* x = (VALUE*)obj->u1.value;
        unsigned long n = obj->u3.cnt;
        while (n--) {
          //v = *x;
         // if (is_in_heap((void *)v), objspace) {
             // //gc_mark(objspace, v, 0);
            yg_id(*x);
         // }
          x++;
        }
      }
      //ptr = (VALUE)obj->as.node.u2.node;
      yg_id((VALUE)obj->u2.node);
      //goto again;
      return;

    case NODE_MEMO:
      yg_id((VALUE)obj->u1.node);
      break;

    //not implemented:

    case NODE_CONST:
      //no ref, just id
      // if(n->nd_vid == 0)return Qfalse;
      // else if(n->nd_vid == 1)return Qtrue;
      // else return ID2SYM(n->nd_vid);
      break;
    case NODE_ATTRASGN:
      //FIXME: may hold references!
      break;

    //iteration func - blocks,procs,lambdas etc:
    case NODE_IFUNC: //NEN_CFNC, NEN_TVAL, NEN_STATE? / u2 seems to be data for func(context?)
      {
        //find in symbol table, if present:
        Dl_info info;
        if(dladdr(obj->nd_cfnc, &info) && info.dli_sname){
          yg_cstring(info.dli_sname);
        }
      }
      if(is_in_heap(obj->u2.node, 0)){
        //TODO: do we need to dump it inline?
        yg_id((VALUE)obj->u2.node);
      }
      if(is_in_heap( (void*)obj->nd_aid, 0)){
        yg_id(obj->nd_aid);
      }
      break;

    //empty:
    case NODE_BEGIN: break;
    default:    /* unlisted NODE */
      //FIXME: check pointers!

      {
      //printf("UNKNOWN NODE TYPE %d(%s): %p %p %p\n", nd_type(obj), node_type_name(obj), (void*)obj->u1.node, (void*)obj->u2.node, (void*)obj->u3.node);
      }

      // if (is_in_heap(obj->as.node.u1.node, objspace)) { gc_mark(objspace, (VALUE)obj->as.node.u1.node, lev); }
      // if (is_in_heap(obj->as.node.u2.node, objspace)) { gc_mark(objspace, (VALUE)obj->as.node.u2.node, lev); }
      // if (is_in_heap(obj->as.node.u3.node, objspace)) { gc_mark(objspace, (VALUE)obj->as.node.u3.node, lev); }

      //yg_id((VALUE)obj->u1.node);
      //yg_id((VALUE)obj->u2.node);
      //yg_id((VALUE)obj->u3.node);
  }
}

static inline void dump_node(NODE* obj, walk_ctx_t *ctx){
  ygh_int("nd_type", nd_type(obj));
  ygh_cstring("nd_type_str", node_type_name(obj));

  yg_cstring("refs");
  yajl_gen_array_open(ctx->yajl);
  dump_node_refs(obj, ctx);
  yajl_gen_array_close(ctx->yajl);
}

static int
dump_keyvalue(st_data_t key, st_data_t value, walk_ctx_t *ctx){
    if(!key || (VALUE)key == Qnil){
      yg_cstring("___null_key___"); //TODO: just ""?
    } else {
      //TODO: keys must be strings
      const int type = TYPE(key);
      if(type == T_SYMBOL || type == T_STRING && (!(RBASIC(key)->flags & RSTRING_NOEMBED)))
        yg_id((VALUE)key);
      else
        {
          char buf[128];
          buf[sizeof(buf)-1] = 0;
          switch(type){
            case T_FIXNUM:
              snprintf(buf, sizeof(buf)-1, "%ld", FIX2LONG(key));
              break;
            case T_FLOAT:
              snprintf(buf, sizeof(buf)-1, "%lg", NUM2DBL(key));
              break;
            default:
              snprintf(buf, sizeof(buf)-1, "__id_%ld", NUM2LONG(rb_obj_id(key)));
              break;
          }
          yg_cstring(buf);
        }
      }
    yg_id((VALUE)value);
    return ST_CONTINUE;
}

static void dump_hash(VALUE obj, walk_ctx_t* ctx){
  yg_cstring("val");
  yajl_gen_map_open(ctx->yajl);
  if(RHASH_SIZE(obj) > 0){
    //TODO: mark keys and values separately?
    st_foreach(RHASH(obj)->ntbl, dump_keyvalue, (st_data_t)ctx);
  }
  yajl_gen_map_close(ctx->yajl);
}

static void dump_method_definition_as_value(const rb_method_definition_t *def, walk_ctx_t *ctx){
  if (!def) {
    yajl_gen_null(ctx->yajl);
    return;
  }

  switch (def->type) {
    case VM_METHOD_TYPE_ISEQ:
      yg_id(def->body.iseq->self);
      break;
    case VM_METHOD_TYPE_CFUNC: yg_cstring("(CFUNC)"); break;
    case VM_METHOD_TYPE_ATTRSET:
    case VM_METHOD_TYPE_IVAR:
      yg_id(def->body.attr.location);
      break;
    case VM_METHOD_TYPE_BMETHOD:
      yg_id(def->body.proc);
      break;
    case VM_METHOD_TYPE_ZSUPER: yg_cstring("(ZSUPER)"); break;
    case VM_METHOD_TYPE_UNDEF: yg_cstring("(UNDEF)"); break;
    case VM_METHOD_TYPE_NOTIMPLEMENTED: yg_cstring("(NOTIMP)"); break;
    case VM_METHOD_TYPE_OPTIMIZED: /* Kernel#send, Proc#call, etc */ yg_cstring("(OPTIMIZED)"); break;
    case VM_METHOD_TYPE_MISSING: yg_cstring("(MISSING)"); break;
    default:
      yajl_gen_null(ctx->yajl);
      break;
    }
}

static int dump_method_entry_i(ID key, const rb_method_entry_t *me, st_data_t data){
  walk_ctx_t *ctx = (void*)data;
  if(key == ID_ALLOCATOR) {
    yg_cstring("___allocator___");
  } else {
    yg_cstring(rb_id2name(key));
  }

  //gc_mark(objspace, me->klass, lev);?
  dump_method_definition_as_value(me->def, ctx);
  return ST_CONTINUE;
}

static int dump_iv_entry(ID key, VALUE value, walk_ctx_t *ctx){
  const char* key_str = rb_id2name(key);
  if(key_str)
    yg_cstring(key_str);
  else{
    // cannot use yg_null() - keys must be strings
    //TODO: just ""?
    yg_cstring("___null_key___");
  }
  yg_id(value);
  return ST_CONTINUE;
}

static int dump_const_entry_i(ID key, const rb_const_entry_t *ce, walk_ctx_t *ctx){
  VALUE value = ce->value;
  yg_cstring(rb_id2name(key));
  yg_id(value);
  return ST_CONTINUE;
}

static const char* iseq_type(VALUE type){
  switch(type){
    case ISEQ_TYPE_TOP:    return "top";
    case ISEQ_TYPE_METHOD: return "method";
    case ISEQ_TYPE_BLOCK:  return "block";
    case ISEQ_TYPE_CLASS:  return "class";
    case ISEQ_TYPE_RESCUE: return "rescue";
    case ISEQ_TYPE_ENSURE: return "ensure";
    case ISEQ_TYPE_EVAL:   return "eval";
    case ISEQ_TYPE_MAIN:   return "main";
    case ISEQ_TYPE_DEFINED_GUARD: return "defined_guard";
  }
  return "unknown_iseq";
}

static void dump_iseq(const rb_iseq_t* iseq, walk_ctx_t *ctx){
  if(iseq->name) ygh_rstring("name", iseq->name);
  if(iseq->filename) ygh_rstring("filename", iseq->filename);
  ygh_int("line", FIX2LONG(iseq->line_no));

  //if(iseq->type != 25116) //also 28 in mark_ary
  ygh_cstring("type", iseq_type(iseq->type));
  //see isec.c: iseq_data_to_ary(rb_iseq_t* )

  //28 is what?
  ygh_id("refs_array_id", iseq->mark_ary);


  ygh_id("coverage", iseq->coverage);
  ygh_id("klass", iseq->klass);
  ygh_id("cref_stack", (VALUE)iseq->cref_stack); //NODE*

  ID id = iseq->defined_method_id;
  yg_cstring("defined_method_id");
  if(id && id != ID_ALLOCATOR){
    yg_cstring(rb_id2name(id)); // symbol=ID2SYM(id);
  } else {
    yg_int(id);
  }

  if (iseq->compile_data != 0) {
    struct iseq_compile_data *const compile_data = iseq->compile_data;
    ygh_id("cd_marks_ary", compile_data->mark_ary);
    ygh_id("cd_err_info", compile_data->err_info);
    ygh_id("cd_catch_table_ary", compile_data->catch_table_ary);
  }

  if(iseq->local_table_size > 0){
    yg_cstring("local_table");
    yg_array();
    int i;
    for(i = 0; i < iseq->local_table_size; i++){
      char* name = rb_id2name(iseq->local_table[i]);
      if(name){
        yg_cstring(name);
      } else {
        yg_cstring("(unnamed)");
      }
    }
    yg_array_end();
  }
}

static void dump_block(const rb_block_t* block, walk_ctx_t *ctx){
    // VALUE self;     /* share with method frame if it's only block */
    // VALUE *lfp;     /* share with method frame if it's only block */
    // VALUE *dfp;     /* share with method frame if it's only block */
    // rb_iseq_t *iseq;
    // VALUE proc;

  if(block->iseq && !RUBY_VM_IFUNC_P(block->iseq)) {
      yg_cstring("iseq");
      yajl_gen_map_open(ctx->yajl);
      //FIXME: id may be different (due to RBasic fields)!!!
      ygh_id("id", (VALUE)block->iseq);
      dump_iseq(block->iseq, ctx);
      yajl_gen_map_close(ctx->yajl);
    } else {
      ygh_id("iseq", (VALUE)block->iseq);
    }

  ygh_id("self", block->self);

  //FIXME: these are pointers to some memory, may be dumped more clever
  ygh_id("lfp", (VALUE)block->lfp);
  ygh_id("dfp", (VALUE)block->dfp);
  //lfp = local frame pointer? local_num elems?
  // dfp = ?
}



static void yg_fiber_status(enum fiber_status status, walk_ctx_t* ctx){
  switch(status){
    case CREATED: yg_cstring("CREATED"); break;
    case RUNNING: yg_cstring("RUNNING"); break;
    case TERMINATED: yg_cstring("TERMINATED"); break;
  }
}

static void yg_fiber_type(enum context_type status, walk_ctx_t* ctx){
  switch(status){
    case CONTINUATION_CONTEXT: yg_cstring("CONTINUATION_CONTEXT"); break;
    case FIBER_CONTEXT: yg_cstring("FIBER_CONTEXT"); break;
    case ROOT_FIBER_CONTEXT: yg_cstring("ROOT_FIBER_CONTEXT"); break;
  }
}

static void dump_locations(VALUE* p, long int n, walk_ctx_t *ctx){
  if(n > 0){
    VALUE* x = p;
    while(n--){
      VALUE v = *x;
      if(is_in_heap((void*)v, NULL)) //TODO: sometimes thread is known, may get its th->vm->objspace (in case there's a few)
        yg_id(v);
      x++;
    }
  }
}

static void dump_thread(const rb_thread_t* th, walk_ctx_t *ctx);


vm_dump_each_thread_func(st_data_t key, VALUE obj, walk_ctx_t *ctx){
  //here stored 'self' from thread
  // yg_map();
  ygh_id("id", obj);
  // const rb_thread_t *th = obj; //RTYPEDDATA_DATA(obj);

  //or direct pointer?
  // dump_thread(th, ctx);
  //yg_id(obj);
  // yg_map_end();
  return ST_CONTINUE;
}


static void dump_data_if_known(VALUE obj, walk_ctx_t *ctx){

  // VM
  // VM/env
  // VM/thread
  // autoload
  // binding <-
  // encoding
  // iseq <-
  // method <-
  // mutex
  // proc <-
  // thgroup
  // time
  // barrier
  // strio
  // etc...

  const char* typename = RTYPEDDATA_TYPE(obj)->wrap_struct_name;

  if(!strcmp("iseq", typename)){
    const rb_iseq_t* iseq = RTYPEDDATA_DATA(obj);
    dump_iseq(iseq, ctx);
    return;
  }

  if(!strcmp("autoload", typename)){
    const st_table *tbl = RTYPEDDATA_DATA(obj);
    yg_cstring("val");
    yajl_gen_map_open(ctx->yajl);
    st_foreach((st_table *)tbl, dump_method_entry_i, (st_data_t)ctx); //removing const, but this should not affect hash
    yajl_gen_map_close(ctx->yajl);
    return;
  }

  if(!strcmp("barrier", typename)){
    ygh_id("val", (VALUE)RTYPEDDATA_DATA(obj));
    return;
  }

  if(!strcmp("proc", typename)){
    const rb_proc_t *proc = RTYPEDDATA_DATA(obj);
    ygh_int("is_lambda", proc->is_lambda);
    ygh_id("blockprocval", proc->blockprocval);
    ygh_id("envval", proc->envval);
    //TODO: dump refs from env here (they're dumped in env itself, but just to make analysis easier)?

    //TODO: is this proc->block.iseq sometimes bound somewhere (seems to be not, but dupes exist)
    yg_cstring("block");
    yajl_gen_map_open(ctx->yajl);
    dump_block(&proc->block, ctx);
    yajl_gen_map_close(ctx->yajl);
    return;
  }

  if(!strcmp("method", typename)){
    struct METHOD *data = RTYPEDDATA_DATA(obj);
    ygh_id("rclass", data->rclass);
    ygh_id("recv", data->recv);
    ygh_int("method_id", data->id);

    yg_cstring("method");
    if(METHOD_DEFINITIONP(data)){
      dump_method_definition_as_value(METHOD_DEFINITIONP(data), ctx);
    }
    return;
  }

  if(!strcmp("binding", typename)){
    rb_binding_t *bind = RTYPEDDATA_DATA(obj);
    if(!bind) return;
    ygh_id("env", bind->env);
    ygh_id("filename", bind->filename);
    return;
  }

  if(!strcmp("VM/env", typename)){
    const rb_env_t* env = RTYPEDDATA_DATA(obj);
    int i = 0;
    yg_cstring("env");
    yajl_gen_array_open(ctx->yajl);
    //for(; i < env->env_size; i++) yg_id(env->env[i]);
    dump_locations(env->env, env->env_size, ctx);
    yajl_gen_array_close(ctx->yajl);

    ygh_int("local_size", env->local_size);
    ygh_id("prev_envval", env->prev_envval);

    yg_cstring("block");
    yajl_gen_map_open(ctx->yajl);
    dump_block(&env->block, ctx);
    yajl_gen_map_close(ctx->yajl);
    return;
  }

  if(!strcmp("enumerator", typename)){
    struct enumerator *ptr = RTYPEDDATA_DATA(obj);
    ygh_id("obj", ptr->obj);
    ygh_id("args", ptr->args);
    ygh_id("fib", ptr->fib);
    ygh_id("dst", ptr->dst);
    ygh_id("lookahead", ptr->lookahead);
    ygh_id("feedvalue", ptr->feedvalue);
    ygh_id("stop_exc", ptr->stop_exc);
    return;
  }

  if(!strcmp("generator", typename)){
    struct generator *ptr = RTYPEDDATA_DATA(obj);
    ygh_id("proc", ptr->proc);
    return;
  }

  if(!strcmp("yielder", typename)){
    struct yielder *ptr = RTYPEDDATA_DATA(obj);
    ygh_id("proc", ptr->proc);
    return;
  }

  if(!strcmp("VM", typename)){
    const rb_vm_t *vm = RTYPEDDATA_DATA(obj);

    ygh_id("thgroup_default", vm->thgroup_default);
    // rb_gc_register_mark_object - goes in that array (not to be freed until vm dies)
    ygh_id("mark_object_ary", vm->mark_object_ary);
    ygh_id("load_path", vm->load_path);
    ygh_id("loaded_features", vm->loaded_features);
    ygh_id("top_self", vm->top_self);
    ygh_id("coverages", vm->coverages);

    //TODO:
    if (vm->living_threads) {
      yg_cstring("threads");
      yg_array();
      st_foreach(vm->living_threads, vm_dump_each_thread_func, (st_data_t)ctx);
      yg_array_end();
      }
    //   rb_gc_mark_locations(vm->special_exceptions, vm->special_exceptions + ruby_special_error_count);

    //   if (vm->loading_table) {
    //       rb_mark_tbl(vm->loading_table);
    //   }

    //   mark_event_hooks(vm->event_hooks);

    //   for (i = 0; i < RUBY_NSIG; i++) {
    //       if (vm->trap_list[i].cmd)
    //     rb_gc_mark(vm->trap_list[i].cmd);
    //   }
    //     }
    return;
  }

  if(!strcmp("fiber", typename)){
    rb_fiber_t *fib = RTYPEDDATA_DATA(obj);
    ygh_id("prev", fib->prev);
    yg_cstring("status");
    yg_fiber_status(fib->status, ctx);

    yg_cstring("cont");
    yg_map();
      // actually this is embedded continuation object, these may be standalone datas in the wild
      yg_cstring("type");
      yg_fiber_type(fib->cont.type, ctx);

      ygh_id("self", fib->cont.self);
      ygh_id("value", fib->cont.value);

      yg_cstring("saved_thread");
      yg_map();
      dump_thread(&fib->cont.saved_thread, ctx);
      yg_map_end();

      //stacks:
      if(fib->cont.vm_stack) {
        yg_cstring("vm_stack");
        yg_array();
        dump_locations(fib->cont.vm_stack, fib->cont.vm_stack_slen + fib->cont.vm_stack_clen, ctx);
        yg_array_end();
      }
      if (fib->cont.machine_stack) {
        yg_cstring("mach_stack");
        yg_array();
        dump_locations(fib->cont.machine_stack, fib->cont.machine_stack_size, ctx);
        yg_array_end();
      }
    yg_map_end();
    return;
  }

  if(!strcmp("VM/thread", typename)){
    const rb_thread_t *th = RTYPEDDATA_DATA(obj);
    dump_thread(th, ctx);
    return;
  }

  if(!strcmp("time", typename)){
    // struct time_object *tobj = RTYPEDDATA_DATA(obj);
    // if (!tobj) return;
    // if (!FIXWV_P(tobj->timew))
    //     rb_gc_mark(w2v(tobj->timew));
    // rb_gc_mark(tobj->vtm.year);
    // rb_gc_mark(tobj->vtm.subsecx);
    // rb_gc_mark(tobj->vtm.utc_offset);
    VALUE flt = rb_funcall(obj, rb_intern("to_f"), 0);
    if(BUILTIN_TYPE(flt) == T_FLOAT){ ygh_double("val", RFLOAT_VALUE(flt)); }
    return;
  }

  if(!strcmp("thgroup", typename)){
    const struct thgroup* gr = RTYPEDDATA_DATA(obj);
    ygh_id("group", gr->group);
    ygh_int("enclosed", gr->enclosed);
    return;
  }


}

static VALUE rb_class_real_checked(VALUE cl)
{
    if (cl == 0 || IMMEDIATE_P(cl))
        return 0;
    while (cl && ((RBASIC(cl)->flags & FL_SINGLETON) || BUILTIN_TYPE(cl) == T_ICLASS)) {
      if(RCLASS_EXT(cl) && RCLASS_SUPER(cl)){
        cl = RCLASS_SUPER(cl);
      } else {
        return 0;
      }
    }
    return cl;
}

static inline void walk_live_object(VALUE obj, walk_ctx_t *ctx){
  ctx->live_objects++;
  yajl_gen_map_open(ctx->yajl);

  ygh_int("id", NUM2LONG(rb_obj_id(obj)));
  ygh_cstring("bt", rb_builtin_type(obj));

  //TODO:
  #ifdef GC_DEBUG
  //RVALUE etc. has file/line info in this case
  #endif

  yg_cstring("class");
  yg_id(rb_class_of(obj));

  //ivars for !(obj|class|module):
  // if (FL_TEST(obj, FL_EXIVAR) || rb_special_const_p(obj))
  // return generic_ivar_get(obj, id, warn);

  const int bt_type = BUILTIN_TYPE(obj);

  // for generic types ivars are held separately in a table
  if(bt_type != T_OBJECT && bt_type != T_CLASS && bt_type != T_MODULE && bt_type != T_ICLASS){
    st_table* generic_tbl = rb_generic_ivar_table(obj);
    if(generic_tbl){
      yg_cstring("generic_ivars");
      yg_map();
      st_foreach(generic_tbl, dump_iv_entry, (st_data_t)ctx);
      yg_map_end();
    }
  }

  switch(bt_type){ // no need to call TYPE(), as value is on heap
    case T_NODE:
      dump_node(RNODE(obj), ctx);
      break;
    case T_STRING:
      //TODO: limit string len!
      {
      int enc_i = rb_enc_get_index(obj);
      rb_encoding* enc = rb_enc_from_index(enc_i);
      if(enc){
        ygh_cstring("encoding", enc->name);
      }
      //FIXME: convert encoding and dump?
      //if(enc_i == rb_usascii_encindex())
      //this produces warnings on dump read, but recoverable
      ygh_string("val", RSTRING_PTR(obj), (unsigned int)RSTRING_LEN(obj));
      }
      break;
    case T_SYMBOL:
      ygh_cstring("val", rb_id2name(SYM2ID(obj)));
      break;
    case T_REGEXP:
      {
      int enc_i = rb_enc_get_index(obj);
      rb_encoding* enc = rb_enc_from_index(enc_i);
      if(enc){
        ygh_cstring("encoding", enc->name);
      }
      //FIXME: encodings?
      // if(enc_i == rb_usascii_encindex())
      ygh_string("val", RREGEXP_SRC_PTR(obj), (unsigned int)RREGEXP_SRC_LEN(obj));
      }
      break;
    // T(T_MATCH);

    case T_ARRAY:
      // if (FL_TEST(obj, ELTS_SHARED)) ...
      yg_cstring("val");
      yajl_gen_array_open(ctx->yajl);
      {
      long i, len = RARRAY_LEN(obj);
      VALUE *ptr = RARRAY_PTR(obj);
      for(i = 0; i < len; i++) yg_id(*ptr++);
      }
      yajl_gen_array_close(ctx->yajl);
      break;

    case T_STRUCT:
      yg_cstring("refs"); //ivars
      yajl_gen_array_open(ctx->yajl);
      {
      long len = RSTRUCT_LEN(obj);
      VALUE *ptr = RSTRUCT_PTR(obj);
      while (len--) yg_id(*ptr++);
      }
      yajl_gen_array_close(ctx->yajl);
      break;

    case T_HASH:
      dump_hash(obj, ctx);
      break;

    case T_OBJECT:
      //yg_cstring("class");
      //yg_id(rb_class_of(obj));


      // yg_cstring("refs"); //ivars
      // yajl_gen_array_open(ctx->yajl);
      // {
      // long i, len = ROBJECT_NUMIV(obj);
      // VALUE *ptr = ROBJECT_IVPTR(obj);
      // for (i = 0; i < len; i++) yg_id(*ptr++);
      // }
      // yajl_gen_array_close(ctx->yajl);
      yg_cstring("ivs");
      yajl_gen_map_open(ctx->yajl); //TODO: what are iv keys?
      rb_ivar_foreach(obj, dump_iv_entry, (st_data_t)ctx);
      yajl_gen_map_close(ctx->yajl);
      break;

    case T_ICLASS:
    case T_CLASS:
    case T_MODULE:
      {
      VALUE name = rb_ivar_get(obj, classid);
      if (name != Qnil){
        ygh_cstring("name", rb_id2name(SYM2ID(name)));
      } else if(RCLASS_EXT(obj) && RCLASS_EXT(obj)->super){
        // more expensive + allocates a string
        VALUE path = rb_class_path(rb_class_real_checked(obj));

        ygh_rstring("name", path);
      }

      yg_cstring("methods");
      yajl_gen_map_open(ctx->yajl);

      if(RCLASS_M_TBL(obj) && RCLASS_M_TBL(obj)->num_entries > 0){ // num check not necessary?
        st_foreach(RCLASS_M_TBL(obj), dump_method_entry_i, (st_data_t)ctx);
      }
      yajl_gen_map_close(ctx->yajl);

      if (RCLASS_EXT(obj)){
        if(RCLASS_IV_TBL(obj) && RCLASS_IV_TBL(obj)->num_entries > 0){
          yg_cstring("ivs");
          yajl_gen_map_open(ctx->yajl); //TODO: what are iv keys?
          st_foreach(RCLASS_IV_TBL(obj), dump_iv_entry, (st_data_t)ctx);
          yajl_gen_map_close(ctx->yajl);
        }

        #ifdef HAVE_CONSTANT_H
        // this is for 1.9.3 or so - where rb_classext_t has const_tbl
        if(RCLASS_CONST_TBL(obj)){
          yg_cstring("consts");
          yg_map();
          st_foreach(RCLASS_CONST_TBL(obj), dump_const_entry_i, (st_data_t)ctx);
          yg_map_end();
        }
        #endif

        ygh_id("super", RCLASS_SUPER(obj));
      }
      }
      break;

    case T_FILE:
      yg_cstring("refs"); //ivars
      yajl_gen_array_open(ctx->yajl);
      if (RFILE(obj)->fptr) {
        yg_id(RFILE(obj)->fptr->pathv);
        yg_id(RFILE(obj)->fptr->tied_io_for_writing);
        yg_id(RFILE(obj)->fptr->writeconv_asciicompat);
        yg_id(RFILE(obj)->fptr->writeconv_pre_ecopts);
        yg_id(RFILE(obj)->fptr->encs.ecopts);
        yg_id(RFILE(obj)->fptr->write_lock);
      }
      yajl_gen_array_close(ctx->yajl);
      break;

    case T_FIXNUM:
      ygh_int("val", FIX2LONG(obj));
      break;
    case T_FLOAT:
      ygh_double("val", RFLOAT_VALUE(obj));
      break;
    case T_RATIONAL:
      //TODO: dump value for immediate components
      yg_cstring("refs");
      yajl_gen_array_open(ctx->yajl);
      yg_id(RRATIONAL(obj)->num);
      yg_id(RRATIONAL(obj)->den);
      yajl_gen_array_close(ctx->yajl);
      break;
    case T_COMPLEX:
      yg_cstring("refs");
      yajl_gen_array_open(ctx->yajl);
      yg_id(RCOMPLEX(obj)->real);
      yg_id(RCOMPLEX(obj)->imag);
      yajl_gen_array_close(ctx->yajl);
      break;

    case T_BIGNUM:
      {
        long len = RBIGNUM_LEN(obj), i;
        BDIGIT* digits = RBIGNUM_DIGITS(obj);
        yg_cstring("digits");
        yajl_gen_array_open(ctx->yajl);
        for(i = 0; i < len; i++)
          yg_int(digits[i]);
        yajl_gen_array_close(ctx->yajl);
      }
      break;

    case T_DATA: // data of extensions + raw bytecode etc., refs undumpable? maybe in some way mess with mark callback? (need to intercept rb_gc_mark :( )
      if(RTYPEDDATA_P(obj)){
        ygh_cstring("type_name", RTYPEDDATA_TYPE(obj)->wrap_struct_name);
#if HAVE_RB_DATA_TYPE_T_FUNCTION
        if(RTYPEDDATA_TYPE(obj)->function.dsize) ygh_int("size", RTYPEDDATA_TYPE(obj)->function.dsize(RTYPEDDATA_DATA(obj)));
#else
        if(RTYPEDDATA_TYPE(obj)->dsize) ygh_int("size", RTYPEDDATA_TYPE(obj)->dsize(RTYPEDDATA_DATA(obj)));
#endif
        dump_data_if_known(obj, ctx);
      }
      break;

    // T(T_UNDEF);
    default: break;
  }
  yajl_gen_map_close(ctx->yajl);
  flush_yajl(ctx);
  fprintf(ctx->file, "\n");
}

/*
 * will be called several times (the number of heap slot, at current implementation) with:
 *   vstart: a pointer to the first living object of the heap_slot.
 *   vend: a pointer to next to the valid heap_slot area.
 *   stride: a distance to next VALUE.
*/
static int objspace_walker(void *vstart, void *vend, int stride, walk_ctx_t *ctx) {
  ctx->walker_called++;

  VALUE v = (VALUE)vstart;
  for (; v != (VALUE)vend; v += stride) {
    if (RBASIC(v)->flags) { // is live object
      walk_live_object(v, ctx);
    }
  }
//  return 1; //stop
  return 0; // continue to iteration
}


//TODO: move to separate header.
/*
  Bits of code taken directly from ruby gc
  Copyright (C) 1993-2007 Yukihiro Matsumoto
  Copyright (C) 2000  Network Applied Communication Laboratory, Inc.
  Copyright (C) 2000  Information-technology Promotion Agency, Japan
*/
// #if defined(__x86_64__) && defined(__GNUC__) && !defined(__native_client__)
// #define SET_MACHINE_STACK_END(p) __asm__("movq\t%%rsp, %0" : "=r" (*(p)))
// #elif defined(__i386) && defined(__GNUC__) && !defined(__native_client__)
// #define SET_MACHINE_STACK_END(p) __asm__("movl\t%%esp, %0" : "=r" (*(p)))
// #else
NOINLINE(static void rb_gc_set_stack_end(VALUE **stack_end_p));
#define SET_MACHINE_STACK_END(p) rb_gc_set_stack_end(p)
#define USE_CONSERVATIVE_STACK_END
// #endif
static void
rb_gc_set_stack_end(VALUE **stack_end_p)
{
    VALUE stack_end;
    *stack_end_p = &stack_end;
}

#ifdef __ia64
#define SET_STACK_END (SET_MACHINE_STACK_END(&th->machine_stack_end), th->machine_register_stack_end = rb_ia64_bsp())
#else
#define SET_STACK_END SET_MACHINE_STACK_END(&th->machine_stack_end)
#endif

#define STACK_START (th->machine_stack_start)
#define STACK_END (th->machine_stack_end)
#define STACK_LEVEL_MAX (th->machine_stack_maxsize/sizeof(VALUE))

#if STACK_GROW_DIRECTION < 0
# define STACK_LENGTH  (size_t)(STACK_START - STACK_END)
#elif STACK_GROW_DIRECTION > 0
# define STACK_LENGTH  (size_t)(STACK_END - STACK_START + 1)
#else
# define STACK_LENGTH  ((STACK_END < STACK_START) ? (size_t)(STACK_START - STACK_END) \
      : (size_t)(STACK_END - STACK_START + 1))
#endif
#if !STACK_GROW_DIRECTION
int ruby_stack_grow_direction;
int
ruby_get_stack_grow_direction(volatile VALUE *addr)
{
    VALUE *end;
    SET_MACHINE_STACK_END(&end);

    if (end > addr) return ruby_stack_grow_direction = 1;
    return ruby_stack_grow_direction = -1;
}
#endif

#if STACK_GROW_DIRECTION < 0
#define GET_STACK_BOUNDS(start, end, appendix) ((start) = STACK_END, (end) = STACK_START)
#elif STACK_GROW_DIRECTION > 0
#define GET_STACK_BOUNDS(start, end, appendix) ((start) = STACK_START, (end) = STACK_END+(appendix))
#else
#define GET_STACK_BOUNDS(start, end, appendix) \
    ((STACK_END < STACK_START) ? \
     ((start) = STACK_END, (end) = STACK_START) : ((start) = STACK_START, (end) = STACK_END+(appendix)))
#endif

#define rb_setjmp(env) RUBY_SETJMP(env)
#define rb_jmp_buf rb_jmpbuf_t

#define numberof(array) (int)(sizeof(array) / sizeof((array)[0]))


/////////////


static inline int is_in_heap(void *ptr, void* osp){
  rb_objspace_t *objspace = osp;
  if(!ptr) return false;
  if(!objspace) objspace = GET_THREAD()->vm->objspace;
  return is_pointer_to_heap(objspace, ptr);
}



static int
dump_backtrace(void* data, VALUE file, int line, VALUE method, int argc, VALUE* argv)
{
    walk_ctx_t *ctx = data;
    yg_map();
    const char *filename = NIL_P(file) ? "<ruby>" : RSTRING_PTR(file);

    ygh_cstring("file", filename);
    ygh_int("line", line);

    if (NIL_P(method)) {
      //fprintf(fp, "\tfrom %s:%d:in unknown method\n", filename, line);
    }
    else {
      //fprintf(fp, "\tfrom %s:%d:in `%s'\n", filename, line, RSTRING_PTR(method));
      ygh_rstring("method", method);
    }
    ygh_int("argc", argc);
    if(argc > 0){
      yg_cstring("argv");
      yg_array();
      int i;
      for(i = 0; i < argc; i++)
        yg_id(argv[i]);
      yg_array_end();
    }
    yg_map_end();
    return FALSE;
}

typedef int (rb_backtrace_iter_ext_func)(void *arg, VALUE file, int line, VALUE method_name, int argc, VALUE* argv);

// copied from ruby_ext_backtrace
static int
vm_backtrace_each_ext(rb_thread_t *th, int lev, void (*init)(void *), rb_backtrace_iter_ext_func *iter, void *arg)
{
  const rb_control_frame_t *limit_cfp = th->cfp;
  const rb_control_frame_t *cfp = (void *)(th->stack + th->stack_size);
  VALUE file = Qnil;
  int line_no = 0;

  cfp -= 2;
  //skip lev frames:
  while (lev-- >= 0) {
    if (++limit_cfp > cfp)
        return FALSE;
  }

  if (init) (*init)(arg);

  limit_cfp = RUBY_VM_NEXT_CONTROL_FRAME(limit_cfp);
  if (th->vm->progname) file = th->vm->progname;

  while (cfp > limit_cfp) {
    if (cfp->iseq != 0) {
        if (cfp->pc != 0) {
          rb_iseq_t *iseq = cfp->iseq;

          line_no = rb_vm_get_sourceline(cfp);
          file = iseq->filename;

          //arguments pushed this way: *reg_cfp->sp++ = recv; for (i = 0; i < argc; i++) *reg_cfp->sp++ = argv[i];
          //local vars = cfp->iseq->local_size - cfp->iseq->arg_size;
          //in memory: receiver params locals (bp(incremented))
          VALUE* argv = &cfp->bp[- cfp->iseq->local_size - 1];
          if ((*iter)(arg, file, line_no, iseq->name, cfp->iseq->arg_size, argv)) break;
        }
    } else
      if (RUBYVM_CFUNC_FRAME_P(cfp)) {
        ID id = cfp->me->def? cfp->me->def->original_id : cfp->me->called_id;

        if (NIL_P(file)) file = ruby_engine_name;

        if (id != ID_ALLOCATOR){
          VALUE* argv = NULL;
          // when argc==-1/-2(variable length params without/with splat) - the cfp has no info on params count :(
          //TODO: infere from somewhere ex. find self in stack? (not guaranted btw, for example: obj.method(obj, 123, obj) - will find last param instead of self)
          if(cfp->me->def->body.cfunc.argc >= 0){ //only fixed args
            argv = &cfp->bp[- cfp->me->def->body.cfunc.argc - 2]; // args+self, bp was incremented thus minus 2
          }
          //file+line no from previous iseq frame
          if((*iter)(arg, file, line_no, rb_id2str(id), cfp->me->def->body.cfunc.argc, argv)) break;
        }
      }
    cfp = RUBY_VM_NEXT_CONTROL_FRAME(cfp);
  }
  return TRUE;
}

static void dump_thread(const rb_thread_t* th, walk_ctx_t *ctx){
   if(th->stack){
    VALUE *p = th->stack;
    VALUE *sp = th->cfp->sp;
    rb_control_frame_t *cfp = th->cfp;
    rb_control_frame_t *limit_cfp = (void *)(th->stack + th->stack_size);

    yg_cstring("stack");
    yajl_gen_array_open(ctx->yajl);
    while (p < sp) yg_id(*p++);
    yajl_gen_array_close(ctx->yajl);
    yg_cstring("stack_locations");
    yg_array();
    dump_locations(p, th->mark_stack_len, ctx);
    yg_array_end();

    yg_cstring("cfp");
    yajl_gen_array_open(ctx->yajl);
    //TODO: this is kind of backtrace, but other direction plus some other info, merge it in backtrace.
    while (cfp != limit_cfp) {
      yajl_gen_map_open(ctx->yajl);
      rb_iseq_t *iseq = cfp->iseq;
      ygh_id("proc", cfp->proc);
      ygh_id("self", cfp->self);
      if (iseq) {
          ygh_id("iseq", RUBY_VM_NORMAL_ISEQ_P(iseq) ? iseq->self : (VALUE)iseq);
          int line_no = rb_vm_get_sourceline(cfp);
          ygh_rstring("file", iseq->filename);
          ygh_int("line_no",line_no);
      }
      if (cfp->me){
        const rb_method_entry_t *me = cfp->me;
        //((rb_method_entry_t *)cfp->me)->mark = 1;
        yg_cstring("me");
        yajl_gen_map_open(ctx->yajl);
        //
        //rb_method_flag_t flag;
     //   char mark;
        //rb_method_definition_t *def;
        ygh_id("klass", me->klass);
        ID id = me->called_id;

        if(me->def){
          id = me->def->original_id;
          yg_cstring("def");
          dump_method_definition_as_value(me->def, ctx);
        }
        if(id != ID_ALLOCATOR)
          ygh_rstring("meth_id", rb_id2str(id));
        yajl_gen_map_close(ctx->yajl);
      }
      cfp = RUBY_VM_PREVIOUS_CONTROL_FRAME(cfp);
      yajl_gen_map_close(ctx->yajl);
    }
    yajl_gen_array_close(ctx->yajl);
  }

  yg_cstring("backtrace");
  yg_array();
  vm_backtrace_each_ext(th, -1, NULL, dump_backtrace, ctx);
  yg_array_end();

  //TODO: mark other...
  ygh_id("first_proc", th->first_proc);
  if (th->first_proc) ygh_id("first_proc", th->first_args);

  ygh_id("thgroup", th->thgroup);
  ygh_id("value", th->value);
  ygh_id("errinfo", th->errinfo);
  ygh_id("thrown_errinfo", th->thrown_errinfo);
  ygh_id("local_svar", th->local_svar);
  ygh_id("top_self", th->top_self);
  ygh_id("top_wrapper", th->top_wrapper);
  ygh_id("fiber", th->fiber);
  ygh_id("root_fiber", th->root_fiber);
  ygh_id("stat_insn_usage", th->stat_insn_usage);
  ygh_id("last_status", th->last_status);
  ygh_id("locking_mutex", th->locking_mutex);

  if (GET_THREAD() != th && th->machine_stack_start && th->machine_stack_end) {
      // rb_gc_mark_machine_stack(th);
      VALUE *stack_start, *stack_end;
      GET_STACK_BOUNDS(stack_start, stack_end, 0);
      // /sizeof(VALUE)?
      yg_cstring("mach_stack");
      yg_array();
      dump_locations(stack_start, (stack_end-stack_start), ctx);
      yg_array_end();

      yg_cstring("mach_regs");
      yg_array();
      dump_locations((VALUE *)&th->machine_regs, sizeof(th->machine_regs) / sizeof(VALUE), ctx);
      yg_array_end();
  }

  yg_cstring("local_storage");
  yajl_gen_map_open(ctx->yajl);
  if(th->local_storage){
    st_foreach(th->local_storage, dump_iv_entry, (st_data_t)ctx); //?
  }
  yajl_gen_map_close(ctx->yajl);

    // mark_event_hooks(th->event_hooks);
  rb_event_hook_t *hook = th->event_hooks;
  yg_cstring("event_hooks");
  yajl_gen_array_open(ctx->yajl);
  while(hook){
    yg_id(hook->data);
    hook = hook->next;
  }
  yajl_gen_array_close(ctx->yajl);
}


static void dump_machine_context(walk_ctx_t *ctx){
  //TODO: other threads?
  rb_thread_t* th = GET_THREAD()->vm->main_thread; //GET_THREAD();
  union {
    rb_jmp_buf j;
    VALUE v[sizeof(rb_jmp_buf) / sizeof(VALUE)];
  } save_regs_gc_mark;
  VALUE *stack_start, *stack_end;


  FLUSH_REGISTER_WINDOWS;
  /* This assumes that all registers are saved into the jmp_buf (and stack) */
  rb_setjmp(save_regs_gc_mark.j);

  SET_STACK_END;
  GET_STACK_BOUNDS(stack_start, stack_end, 1);

  yg_cstring("registers");
  yajl_gen_array_open(ctx->yajl);
  //mark_locations_array(objspace, save_regs_gc_mark.v, numberof(save_regs_gc_mark.v));
  VALUE* x = save_regs_gc_mark.v;
  unsigned long n = numberof(save_regs_gc_mark.v);
  while (n--) {
    VALUE v = *(x++);
    if(is_in_heap((void*)v, NULL))
      yg_id(v);
  }
  yajl_gen_array_close(ctx->yajl);

  yg_cstring("stack");
  yajl_gen_array_open(ctx->yajl);
  //rb_gc_mark_locations(stack_start, stack_end);
  if(stack_start < stack_end){
    n = stack_end - stack_start;
    x = stack_start;
    while (n--) {
      VALUE v = *(x++);
      //FIXME: other objspace (not default one?)
      if(is_in_heap((void*)v, NULL)) {
        yg_id(v);
      }
    }
  }

  yajl_gen_array_close(ctx->yajl);
}

#ifdef HAVE_RB_CLASS_TBL
// 1.9.2, rb_class_tbl fails to be linked in 1.9.3 :(

static int dump_class_tbl_entry(ID key, rb_const_entry_t* ce/*st_data_t val*/, walk_ctx_t *ctx){
  if (!rb_is_const_id(key)) return ST_CONTINUE; //?
  VALUE value = ce->value;

  char* id = rb_id2name(key);
  if(id)
    yg_cstring(id);
  else
    yg_cstring("(unknown)");
  yg_id(value);
  return ST_CONTINUE;
}
#endif


//public symbol, can be used from GDB
void heapdump_dump(const char* filename){
  struct walk_ctx ctx_o, *ctx = &ctx_o;
  memset(ctx, 0, sizeof(*ctx));

  if(!filename){
    filename = "dump.json";
  }
  printf("Dump should go to %s\n", filename);
  ctx->file = fopen(filename, "wt");
  ctx->yajl = yajl_gen_alloc(NULL,NULL);
  yajl_gen_array_open(ctx->yajl);

  //dump origins:
  yajl_gen_map_open(ctx->yajl);
  ygh_cstring("id", "_ROOTS_");

  printf("machine context\n");

  dump_machine_context(ctx);
  flush_yajl(ctx);
  // fprintf(ctx->file, "\n");

  struct gc_list *list;
  /* mark protected global variables */
  printf("global_list\n");
  yg_cstring("globals");
  yg_array();
  for (list = GET_THREAD()->vm->global_List; list; list = list->next) {
    VALUE v = *list->varptr;
    yg_id(v);
  }
  yg_array_end();

#ifdef HAVE_RB_CLASS_TBL
  st_table *rb_class_tbl = rb_get_class_tbl();
  if (rb_class_tbl && rb_class_tbl->num_entries > 0){
    printf("classes\n");
    yg_cstring("classes");
    yg_map();
    st_foreach(rb_class_tbl, dump_class_tbl_entry, (st_data_t)ctx);
    yg_map_end();
    flush_yajl(ctx);
  }
#endif

  //TODO: other gc entry points - symbols, encodings, etc.

  yajl_gen_map_close(ctx->yajl); //id:roots
  flush_yajl(ctx);
  fprintf(ctx->file, "\n");

  //now dump all live objects
  printf("starting objspace walk\n");
  rb_objspace_each_objects(objspace_walker, ctx);

  yajl_gen_array_close(ctx->yajl);
  flush_yajl(ctx);
  yajl_gen_free(ctx->yajl);
  fclose(ctx->file);

  printf("Walker called %d times, seen %d live objects.\n", ctx->walker_called, ctx->live_objects);
}

static VALUE
rb_heapdump_dump(VALUE self, VALUE filename)
{
  Check_Type(filename, T_STRING);
  heapdump_dump(RSTRING_PTR(filename));
  return Qnil;
}



// HeapDump.count_objects:

#undef YAJL
#define YAJL yajl
static int
iterate_user_type_counts(VALUE key, VALUE value, yajl_gen yajl){
  yg_rstring(key);
  yg_int(FIX2LONG(value));
  return ST_CONTINUE;
}

static VALUE
rb_heapdump_count_objects(VALUE self, VALUE string_prefixes, VALUE do_gc){
  rb_check_array_type(string_prefixes);
  yajl_gen_config cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.beautify = true;
  cfg.htmlSafe = true;
  cfg.indentString = "    ";
  yajl_gen yajl = yajl_gen_alloc(&cfg,NULL);
  yg_map();
  if(do_gc){
    yg_cstring("gc_ran");
    yg_bool(true);
    rb_gc_start();
  }

  rb_objspace_t *objspace = GET_THREAD()->vm->objspace;
  size_t counts[T_MASK+1];
  size_t freed = 0;
  size_t total = 0;
  size_t i;
  VALUE hash = rb_hash_new();

  for (i = 0; i <= T_MASK; i++) counts[i] = 0;

  FOR_EACH_HEAP_SLOT(p)
    // danger: allocates memory while walking heap
    if (p->as.basic.flags) {
      int type = BUILTIN_TYPE(p);
      counts[type]++;
      if(type == T_OBJECT){
        //take class etc.
        VALUE cls = rb_class_real_checked(CLASS_OF(p));
        if(!cls) continue;
        VALUE class_name = rb_class_path(cls);
        long int n = RARRAY_LEN(string_prefixes)-1;
        for(; n >= 0; n--){
          VALUE prefix = rb_check_string_type(RARRAY_PTR(string_prefixes)[n]);
          if(NIL_P(prefix)) continue;
          rb_enc_check(class_name, prefix);
          if (RSTRING_LEN(class_name) < RSTRING_LEN(prefix)) continue;
          if (!memcmp(RSTRING_PTR(class_name), RSTRING_PTR(prefix), RSTRING_LEN(prefix)))
            if(RSTRING_LEN(class_name) == RSTRING_LEN(prefix) ||
              RSTRING_PTR(class_name)[RSTRING_LEN(prefix)] == ':'){
              //class match
              VALUE val = rb_hash_aref(hash, class_name);
              long num;
              if(FIXNUM_P(val)){
                num = FIX2LONG(val) + 1;
              } else {
                num = 1;
              }
              rb_hash_aset(hash, class_name, LONG2FIX(num));
            }
        }
      }
    } else {
      freed++;
    }
  FOR_EACH_HEAP_SLOT_END(total)

  ygh_int("total_slots", total);
  ygh_int("free_slots", freed);
  yg_cstring("basic_types");
  yg_map();
  for (i = 0; i <= T_MASK; i++) {
    if(!counts[i]) continue;
    yg_cstring(rb_type_str((int)i));
    yg_int(counts[i]);
  }
  yg_map_end();

  yg_cstring("user_types");
  yg_map();
  rb_hash_foreach(hash, iterate_user_type_counts, (VALUE)yajl);
  yg_map_end();

  yg_map_end(); //all document

  //flush yajl:
  const unsigned char* buf;
  unsigned int len;
  if(yajl_gen_get_buf(yajl, &buf, &len) == yajl_gen_status_ok){
    //fwrite(buf, len, 1, ctx->file);
    VALUE res = rb_str_new(buf, len);
    yajl_gen_clear(yajl);
    yajl_gen_free(yajl);
    return res;
  } else {
    return Qnil;
  }
#undef YAJL
}

void Init_heap_dump(){
  //ruby-internal need to be required before linking us, but just in case..
  ID require, gem;
  CONST_ID(require, "require");
  CONST_ID(gem, "gem");
  CONST_ID(classid, "__classid__");

  rb_require("rubygems");
  rb_funcall(rb_mKernel, gem, 1, rb_str_new2("yajl-ruby"));
  rb_require("yajl");

  rb_mHeapDumpModule = rb_define_module("HeapDump");
  rb_define_singleton_method(rb_mHeapDumpModule, "dump_ext", rb_heapdump_dump, 1);
  rb_define_singleton_method(rb_mHeapDumpModule, "count_objects_ext", rb_heapdump_count_objects, 2);
}
