#include "ruby.h"
#include <stdlib.h>
#include <stdio.h>


#include "yarv-headers/constant.h"
#include "yarv-headers/node.h"
#include "yarv-headers/vm_core.h"
#include "yarv-headers/atomic.h"
#include "yarv-headers/iseq.h"

//#undef RCLASS_IV_TBL
//#include "yarv-headers/internal.h"
#define RCLASS_EXT(c) (RCLASS(c)->ptr)


#include "yarv-headers/method.h"
#include "method/internal_method.h"

#include "ruby_io.h" // need rb_io_t

#include "node/ruby_internal_node.h"
#include "node/node_type_descrip.c"

#include "api/yajl_gen.h"

#ifndef RUBY_VM
#error No RUBY_VM, old rubies not supported
#endif

// simple test - rake compile && bundle exec ruby -e 'require "heap_dump"; HeapDump.dump'

static VALUE rb_mHeapDumpModule;

static ID classid;

//shortcuts to yajl
#define yg_string(str,len) yajl_gen_string(ctx->yajl, str, len)
#define yg_cstring(str) yg_string(str, (unsigned int)strlen(str))
#define yg_rstring(str) yg_string(RSTRING_PTR(str), (unsigned int)RSTRING_LEN(str))
#define yg_int(i) yajl_gen_integer(ctx->yajl, i)
#define yg_double(d) (yajl_gen_double(ctx->yajl, d)==yajl_gen_invalid_number? yg_cstring("inf|NaN") : true)

//#define yg_id(obj) yg_int(NUM2LONG(rb_obj_id(obj)))
#define yg_id(obj) yg_id1(obj,ctx)


#define ygh_id(key,obj) {yg_cstring(key); yg_id(obj);}
#define ygh_int(key,i) {yg_cstring(key); yg_int((long int)(i));}
#define ygh_double(key,d) {yg_cstring(key); yg_double(d);}
#define ygh_string(key,str,len) {yg_cstring(key); yg_string(str,len);}
#define ygh_cstring(key,str) {yg_cstring(key); yg_cstring(str);}
#define ygh_rstring(key,str) {yg_cstring(key); yg_rstring(str);}

#define yg_map() yajl_gen_map_open(ctx->yajl);
#define yg_map_end() yajl_gen_map_close(ctx->yajl);
#define yg_array() yajl_gen_array_open(ctx->yajl);
#define yg_array_end() yajl_gen_array_close(ctx->yajl);


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

static inline int is_pointer_to_heap(void *ptr, void* osp);

static inline const char* rb_builtin_type(VALUE obj){
  switch(BUILTIN_TYPE(obj)){
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
  }
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
    //printf("immediate\n");
    if (FIXNUM_P(obj)) { /*ignore immediate fixnum*/
      //fixme: output some readable info
      //yajl_gen_null(ctx->yajl);
      yg_int(FIX2LONG(obj));
      return;
    }
    if (obj == Qtrue){ yajl_gen_bool(ctx->yajl, true); return; }
    if (SYMBOL_P(obj)) {
      //printf("symbol\n");
      yg_cstring(rb_id2name(SYM2ID(obj)));
      //printf("symbol %s\n", rb_id2name(SYM2ID(obj)));
      return;
    }
    if (obj == Qundef) { yg_cstring("(undef)"); return; }

    printf("immediate p %p?\n", (void*)obj);
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
      //printf("non r-test\n");
    }
  }

  //25116(0x621c aka 0b110001000011100), 28(0x) - wtf?
  //28 = 0x1c
  //1c= TNODE? or other flags combination

  //also 30(x1e) ? - some internal symbols?

  // if((obj & ~(~(VALUE)0 << RUBY_SPECIAL_SHIFT)) == 0x1c){
  //   printf("!!!!! special shift flags is 0x1c: %p\n", obj);
  //   yg_cstring("(unknown or internal 1c)");
  //   return;
  // }

  if(BUILTIN_TYPE(obj) == T_STRING && (!(RBASIC(obj)->flags & RSTRING_NOEMBED))){
    //printf("embedded string\n");
    //yajl_gen_null(ctx->yajl);
    yg_rstring(obj);
    return;
  }

  yg_int(NUM2LONG(rb_obj_id(obj)));
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
      //printf("node scope\n");
      if(obj->nd_tbl){
        ID *tbl = RNODE(obj)->nd_tbl;
        unsigned long i = 0, size = tbl[0];
        tbl++;
        for (; i < size; i++) {
          //TODO: dump local var names?
          //printf("%s\n", rb_id2name(tbl[i]));
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
         // if (is_pointer_to_heap(objspace, (void *)v)) {
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
      //printf("MEMO NODE: %p %p %p\n", obj->u1.node, obj->u2.node, obj->u3.node);
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
      //printf("IFUNC NODE: %p %p %p\n", obj->nd_cfnc, obj->u2.node, (void*)obj->nd_aid /*u3 - aid id- - aka frame_this_func?*/);
      if(is_pointer_to_heap(obj->u2.node, 0)){
        //printf("in heap: %p\n", obj->u2.node);
        //TODO: do we need to dump it inline?
        yg_id((VALUE)obj->u2.node);
      }
      if(is_pointer_to_heap( (void*)obj->nd_aid, 0)){
        //printf("in heap: %p\n", (void*)obj->nd_aid);
        yg_id(obj->nd_aid);
      }
      break;

    //empty:
    case NODE_BEGIN: break;
    default:    /* unlisted NODE */
      //FIXME: check pointers!

      {const Node_Type_Descrip* descrip = node_type_descrips[nd_type(obj)];

      printf("UNKNOWN NODE TYPE %d(%s): %p %p %p\n", nd_type(obj), descrip ? descrip->name : "unknown", (void*)obj->u1.node, (void*)obj->u2.node, (void*)obj->u3.node);
      }

      // if (is_pointer_to_heap(objspace, obj->as.node.u1.node)) { gc_mark(objspace, (VALUE)obj->as.node.u1.node, lev); }
      // if (is_pointer_to_heap(objspace, obj->as.node.u2.node)) { gc_mark(objspace, (VALUE)obj->as.node.u2.node, lev); }
      // if (is_pointer_to_heap(objspace, obj->as.node.u3.node)) { gc_mark(objspace, (VALUE)obj->as.node.u3.node, lev); }

      //yg_id((VALUE)obj->u1.node);
      //yg_id((VALUE)obj->u2.node);
      //yg_id((VALUE)obj->u3.node);
  }
}

static inline void dump_node(NODE* obj, walk_ctx_t *ctx){
  const Node_Type_Descrip* descrip = node_type_descrips[nd_type(obj)]; // node_type_descrip(nd_type(obj)) raises exception on unknown 65, 66 and 92

  ygh_int("nd_type", nd_type(obj));
  ygh_cstring("nd_type_str", descrip ? descrip->name : "unknown");

  yg_cstring("refs");
  yajl_gen_array_open(ctx->yajl);
  dump_node_refs(obj, ctx);
  yajl_gen_array_close(ctx->yajl);
}

static int
dump_keyvalue(st_data_t key, st_data_t value, walk_ctx_t *ctx){
    yg_id((VALUE)key);
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
  //printf("mdef %d\n", def->type);

  switch (def->type) {
    case VM_METHOD_TYPE_ISEQ:
      //printf("method iseq %p\n", def->body.iseq);
      //printf("self %p\n", def->body.iseq->self);
      yg_id(def->body.iseq->self);
      break;
    case VM_METHOD_TYPE_CFUNC: yg_cstring("(CFUNC)"); break;
    case VM_METHOD_TYPE_ATTRSET:
    case VM_METHOD_TYPE_IVAR:
      //printf("method ivar\n");
      yg_id(def->body.attr.location);
      break;
    case VM_METHOD_TYPE_BMETHOD:
      //printf("method binary\n");
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
  //printf("method entry\n");
  dump_method_definition_as_value(me->def, ctx);
  return ST_CONTINUE;
}

static int dump_iv_entry(ID key, VALUE value, walk_ctx_t *ctx){
  yg_cstring(rb_id2name(key));
  yg_id(value);
  return ST_CONTINUE;
}

static int dump_const_entry_i(ID key, const rb_const_entry_t *ce, walk_ctx_t *ctx){

//was(ID key, VALUE value, walk_ctx_t *ctx){

  printf("const entry\n");

  VALUE value = ce->value;
  //file = ce->file

  printf("const key %p\n", (void*)key);
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
  printf("unknown iseq type %p!\n", (void*)type);
  return "unknown";
}

static void dump_iseq(const rb_iseq_t* iseq, walk_ctx_t *ctx){
  if(iseq->name) ygh_rstring("name", iseq->name);
  if(iseq->filename) ygh_rstring("filename", iseq->filename);
  ygh_int("line", FIX2INT(iseq->line_no));

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
      ygh_id("id", block->iseq);
      dump_iseq(block->iseq, ctx);
      yajl_gen_map_close(ctx->yajl);
    } else {
      ygh_id("iseq", block->iseq);
    }

  ygh_id("self", block->self);

  ygh_id("lfp", block->lfp);
  ygh_id("dfp", block->dfp);
  //lfp = local frame pointer? local_num elems?
  // dfp = ?
}


#define CAPTURE_JUST_VALID_VM_STACK 1

enum context_type {
    CONTINUATION_CONTEXT = 0,
    FIBER_CONTEXT = 1,
    ROOT_FIBER_CONTEXT = 2
};

typedef struct rb_context_struct {
    enum context_type type;
    VALUE self;
    int argc;
    VALUE value;
    VALUE *vm_stack;
#ifdef CAPTURE_JUST_VALID_VM_STACK
    size_t vm_stack_slen;  /* length of stack (head of th->stack) */
    size_t vm_stack_clen;  /* length of control frames (tail of th->stack) */
#endif
    VALUE *machine_stack;
    VALUE *machine_stack_src;
#ifdef __ia64
    VALUE *machine_register_stack;
    VALUE *machine_register_stack_src;
    int machine_register_stack_size;
#endif
    rb_thread_t saved_thread;
    rb_jmpbuf_t jmpbuf;
    size_t machine_stack_size;
} rb_context_t;



enum fiber_status {
    CREATED,
    RUNNING,
    TERMINATED
};

///TODO: move this out
typedef struct rb_fiber_struct {
    rb_context_t cont;
    VALUE prev;
    enum fiber_status status;
    struct rb_fiber_struct *prev_fiber;
    struct rb_fiber_struct *next_fiber;
} rb_fiber_t;







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

static void dump_locations(VALUE* p, int n, walk_ctx_t *ctx){
  if(n > 0){
    VALUE* x = p;
    while(n--){
      VALUE v = *x;
      if(is_pointer_to_heap(v, NULL)) //TODO: sometimes thread is known, may get its th->vm->objspace (in case there's a few)
        yg_id(v);
      x++;
    }
  }
}

static void dump_thread(rb_thread_t* th, walk_ctx_t *ctx);



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
    st_foreach(tbl, dump_method_entry_i, (st_data_t)ctx);
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
    //printf("method\n");
    struct METHOD *data = RTYPEDDATA_DATA(obj);
    //printf("method %p: %p %p\n", data, data->rclass, data->recv);
    ygh_id("rclass", data->rclass);
    ygh_id("recv", data->recv);
    ygh_int("method_id", data->id);

    yg_cstring("method");
    if(data->me.def){
      //printf("methof def %p\n", &data->me);
      dump_method_definition_as_value(data->me.def, ctx);
      //printf("meth end\n");
    }
    return;
  }

  if(!strcmp("binding", typename)){
    //printf("binding\n");
    rb_binding_t *bind = RTYPEDDATA_DATA(obj);
    //printf("binding %p\n", bind);
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

    //FIXME: autogen this:
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
// end of fixme

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
    // if (vm->living_threads) {
    //       st_foreach(vm->living_threads, vm_mark_each_thread_func, 0);
    //   }
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
    //ygh_int("cont", fib->cont);
    yg_cstring("status");
    yg_fiber_status(fib->status, ctx);

    yg_cstring("cont");
    yg_map();
      //ygh_int("type", fib->cont.type);
      yg_cstring("type");
      yg_fiber_type(fib->cont.type, ctx);

      ygh_id("self", fib->cont.self);
      ygh_id("value", fib->cont.value);

      yg_cstring("saved_thread");
      yg_map();
      //rb_thread_mark(&fib->cont.saved_thread);
      dump_thread(&fib->cont.saved_thread, ctx);
      yg_map_end();

      //stacks:
      VALUE *vm_stack = fib->cont.vm_stack;
      int i = 0;
      for(; vm_stack && i < fib->cont.vm_stack_slen + fib->cont.vm_stack_clen; i++){
        yg_id(*(vm_stack++));
      }
      vm_stack = fib->cont.vm_stack;
      for(i = 0;vm_stack && i<fib->cont.machine_stack_size; i++){
        yg_id(*(vm_stack++));
      }
      yg_cstring("stack");
      yg_array();

      yg_array_end();
    yg_map_end();
    return;
  }

  if(!strcmp("VM/thread", typename)){
    const rb_thread_t *th = RTYPEDDATA_DATA(obj);
    dump_thread(th, ctx);
    return;
  }

  //FIXME: autogen this from ruby (this copied from 1.9.2p290)
  struct thgroup {
    int enclosed;
    VALUE group;
  };

  if(!strcmp("thgroup", typename)){
    const struct thgroup* gr = RTYPEDDATA_DATA(obj);
    ygh_id("group", gr->group);
    ygh_int("enclosed", gr->enclosed);
    return;
  }


}

static VALUE rb_class_real_checked(VALUE cl)
{
    if (cl == 0)
        return 0;
    while ((RBASIC(cl)->flags & FL_SINGLETON) || BUILTIN_TYPE(cl) == T_ICLASS) {
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
      ygh_string("val", RSTRING_PTR(obj), (unsigned int)RSTRING_LEN(obj));
      break;
    case T_SYMBOL:
      ygh_cstring("val", rb_id2name(SYM2ID(obj)));
      break;
    case T_REGEXP:
      ygh_string("val", RREGEXP_SRC_PTR(obj), (unsigned int)RREGEXP_SRC_LEN(obj));
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

        #if 0
        // this is for 1.9.3 or so - where rb_classext_t has const_tbl
        if(RCLASS_CONST_TBL(obj)){
          yg_cstring("consts");
          yajl_gen_map_open(ctx->yajl);
          flush_yajl(ctx); //for debug only
          st_foreach(RCLASS_CONST_TBL(obj), dump_const_entry_i, (st_data_t)ctx);
          yajl_gen_map_close(ctx->yajl);
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
      ygh_int("val", NUM2LONG(obj));
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
        if(RTYPEDDATA_TYPE(obj)->dsize){
          ygh_int("size", RTYPEDDATA_TYPE(obj)->dsize(RTYPEDDATA_DATA(obj)));
        }

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

extern st_table *rb_class_tbl;


/////////////
#define MARK_STACK_MAX 1024

#ifndef CALC_EXACT_MALLOC_SIZE
#define CALC_EXACT_MALLOC_SIZE 0
#endif
#include "ruby/re.h"

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

//FIXME: this should be autoextracted from ruby
// see how this is done in ruby-internal gem
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
    uintptr_t *bits;
    RVALUE *freelist;
    struct heaps_slot *next;
    struct heaps_slot *prev;
    struct heaps_slot *free_next;
};

struct heaps_header {
    struct heaps_slot *base;
    uintptr_t *bits;
};

struct sorted_heaps_slot {
    RVALUE *start;
    RVALUE *end;
    struct heaps_slot *slot;
};

struct heaps_free_bitmap {
    struct heaps_free_bitmap *next;
};

struct gc_list {
    VALUE *varptr;
    struct gc_list *next;
};


// typedef struct rb_objspace {
//   struct {
//     size_t limit;
//     size_t increase;
// //FIXME: this should match ruby settings
// //#if CALC_EXACT_MALLOC_SIZE
//     size_t allocated_size;
//     size_t allocations;
// //#endif
//   } malloc_params;

//   struct {
//       size_t increment;
//       struct heaps_slot *ptr;
//       struct heaps_slot *sweep_slots;
//       struct heaps_slot *free_slots;
//       struct sorted_heaps_slot *sorted;
//       size_t length;
//       size_t used;
//       struct heaps_free_bitmap *free_bitmap;
//       RVALUE *range[2];
//       RVALUE *freed;
//       size_t live_num;
//       size_t free_num;
//       size_t free_min;
//       size_t final_num;
//       size_t do_heap_free;
//   } heap;

//   struct {
//     int dont_gc;
//     int dont_lazy_sweep;
//     int during_gc;
//     rb_atomic_t finalizing;
//   } flags;

//   struct {
//     st_table *table;
//     RVALUE *deferred;
//   } final;

//   struct {
//     VALUE buffer[MARK_STACK_MAX];
//     VALUE *ptr;
//     int overflow;
//   } markstack;

//   struct {
//     int run;
//     gc_profile_record *record;
//     size_t count;
//     size_t size;
//     double invoke_time;
//   } profile;

//   struct gc_list *global_list;
//   size_t count;
//   int gc_stress;
// } rb_objspace_t;


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

#define RANY(o) ((RVALUE*)(o))

static inline int
is_pointer_to_heap(void *ptr, void* osp)
{
    rb_objspace_t *objspace = osp;
    if(!ptr) return false;
    if(!objspace) objspace = GET_THREAD()->vm->objspace;

    register RVALUE *p = RANY(ptr);
    //register struct sorted_heaps_slot *heap;
    register size_t hi, lo, mid;

    if (p < lomem || p > himem) {
      //printf("not in range %p - %p (objspace %p, l%u used %u)\n", lo, hi, objspace, heaps_length, heaps_used);
      return FALSE;
    }
    //printf("ptr in range\n");
    if ((VALUE)p % sizeof(RVALUE) != 0) return FALSE;
    //printf("ptr %p align correct\n", ptr);

  //1.9.2-p290
  /* check if p looks like a pointer using bsearch*/
      lo = 0;
      hi = heaps_used;
      while (lo < hi) {
    mid = (lo + hi) / 2;
    register struct heaps_slot *heap;
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
    //printf("not found");
    return FALSE;
}



static int
dump_backtrace(walk_ctx_t *ctx, VALUE file, int line, VALUE method)
{
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
    yg_map_end();
    return FALSE;
}

//TODO: autogen, this func is just copied from vm.c
//typedef int rb_backtrace_iter_func(void *, VALUE, int, VALUE);
static int
vm_backtrace_each(rb_thread_t *th, int lev, void (*init)(void *), rb_backtrace_iter_func *iter, void *arg)
{
    const rb_control_frame_t *limit_cfp = th->cfp;
    const rb_control_frame_t *cfp = (void *)(th->stack + th->stack_size);
    VALUE file = Qnil;
    int line_no = 0;

    cfp -= 2;
    while (lev-- >= 0) {
  if (++limit_cfp > cfp) {
      return FALSE;
  }
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
    if ((*iter)(arg, file, line_no, iseq->name)) break;
      }
  }
  else if (RUBYVM_CFUNC_FRAME_P(cfp)) {
      ID id;
      extern VALUE ruby_engine_name;

      if (NIL_P(file)) file = ruby_engine_name;
      if (cfp->me->def)
    id = cfp->me->def->original_id;
      else
    id = cfp->me->called_id;
      if (id != ID_ALLOCATOR && (*iter)(arg, file, line_no, rb_id2str(id)))
    break;
  }
  cfp = RUBY_VM_NEXT_CONTROL_FRAME(cfp);
    }
    return TRUE;
}

static void dump_thread(rb_thread_t* th, walk_ctx_t *ctx){
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
  vm_backtrace_each(th, -1, NULL, dump_backtrace, ctx);
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
  yajl_gen_array_open(ctx->yajl);
  if(th->local_storage){
    st_foreach(th->local_storage, dump_iv_entry, ctx); //?
  }
  yajl_gen_array_close(ctx->yajl);

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


  yg_cstring("registers");
  yajl_gen_array_open(ctx->yajl);

  FLUSH_REGISTER_WINDOWS;
  /* This assumes that all registers are saved into the jmp_buf (and stack) */
  rb_setjmp(save_regs_gc_mark.j);

  SET_STACK_END;
  GET_STACK_BOUNDS(stack_start, stack_end, 1);

  //mark_locations_array(objspace, save_regs_gc_mark.v, numberof(save_regs_gc_mark.v));
  VALUE* x = save_regs_gc_mark.v;
  unsigned long n = numberof(save_regs_gc_mark.v);
  //printf("registers\n");
  while (n--) {
    VALUE v = *(x++);
    if(is_pointer_to_heap((void*)v, NULL))
      yg_id(v);
  }
  yajl_gen_array_close(ctx->yajl);

  //printf("stack: %p %p\n", stack_start, stack_end);
  yg_cstring("stack");
  yajl_gen_array_open(ctx->yajl);
  //rb_gc_mark_locations(stack_start, stack_end);
  if(stack_start < stack_end){
    n = stack_end - stack_start;
    x = stack_start;
    while (n--) {
      VALUE v = *(x++);
      //printf("val: %p\n", (void*)v);
      //FIXME: other objspace (not default one?)
      if(is_pointer_to_heap((void*)v, NULL)) {
        //printf("ON heap\n");
        yg_id(v);
      }
    }
  }

  yajl_gen_array_close(ctx->yajl);
}

static int dump_iv_entry1(ID key, rb_const_entry_t* ce/*st_data_t val*/, walk_ctx_t *ctx){
  if (!rb_is_const_id(key)) return ST_CONTINUE; //?
  //rb_const_entry_t* ce = val;
  VALUE value = ce->value;

  //printf("cls %p\n", (void*)value);
  //printf("id: %s\n", rb_id2name(key));

  //val - damaged in some way?

  //printf("name %s\n", RSTRING_PTR(rb_class_path(rb_class_real_checked(value))));

  //if(is_pointer_to_heap(value, NULL)){
    //printf("on heap\n");
    yg_id(value);
  //}

  return ST_CONTINUE;
}


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
  fprintf(ctx->file, "\n");

  struct gc_list *list;
  /* mark protected global variables */
  printf("global_List\n");
  for (list = GET_THREAD()->vm->global_List; list; list = list->next) {
    VALUE v = *list->varptr;
    //printf("global %p\n", v);
  }

  yg_cstring("classes");
  yajl_gen_array_open(ctx->yajl);
  printf("classes\n");
  if (rb_class_tbl && rb_class_tbl->num_entries > 0)
    st_foreach(rb_class_tbl, dump_iv_entry1, (st_data_t)ctx);
  else printf("no classes\n");
  yajl_gen_array_close(ctx->yajl);
  flush_yajl(ctx);

  //TODO: other gc entry points - symbols, encodings, etc.

  yajl_gen_map_close(ctx->yajl); //id:roots
  flush_yajl(ctx);

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

void Init_heap_dump(){
  //ruby-internal need to be required before linking us, but just in case..
  ID require, gem;
  CONST_ID(require, "require");
  CONST_ID(gem, "gem");
  CONST_ID(classid, "__classid__");

  rb_require("rubygems");
  rb_funcall(rb_mKernel, gem, 1, rb_str_new2("yajl-ruby"));
  rb_funcall(rb_mKernel, gem, 1, rb_str_new2("ruby-internal")); //TODO: version requirements
  rb_require("internal/node");
  rb_require("yajl");

  init_node_type_descrips();

  rb_mHeapDumpModule = rb_define_module("HeapDump");
  rb_define_singleton_method(rb_mHeapDumpModule, "dump_ext", rb_heapdump_dump, 1);
}
