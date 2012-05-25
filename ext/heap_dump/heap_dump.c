#include "ruby.h"
#include <stdlib.h>
#include <stdio.h>


#include "yarv-headers/constant.h"
#include "yarv-headers/node.h"
#include "yarv-headers/vm_core.h"

//#undef RCLASS_IV_TBL
//#include "yarv-headers/internal.h"
#define RCLASS_EXT(c) (RCLASS(c)->ptr)


#include "yarv-headers/method.h"

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
#define yg_double(d) yajl_gen_double(ctx->yajl, d)

//#define yg_id(obj) yg_int(NUM2LONG(rb_obj_id(obj)))
#define yg_id(obj) yg_id1(obj,ctx)


#define ygh_id(key,obj) {yg_cstring(key); yg_id(obj);}
#define ygh_int(key,i) {yg_cstring(key); yg_int((long int)(i));}
#define ygh_double(key,d) {yg_cstring(key); yg_double(d);}
#define ygh_string(key,str,len) {yg_cstring(key); yg_string(str,len);}
#define ygh_cstring(key,str) {yg_cstring(key); yg_cstring(str);}
#define ygh_rstring(key,str) {yg_cstring(key); yg_rstring(str);}

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
  if(!obj) return;
  if (IMMEDIATE_P(obj)) {
    if (FIXNUM_P(obj)) { /*ignore immediate fixnum*/ return; }
    if (obj == Qtrue){ yajl_gen_bool(ctx->yajl, true); return; }
    if (SYMBOL_P(obj)) {
      //printf("symbol\n");
      yg_cstring(rb_id2name(SYM2ID(obj)));
      //printf("symbol %s\n", rb_id2name(SYM2ID(obj)));
      return;
    }
    if (obj == Qundef) { yg_cstring("(undef)"); return; }
    printf("immediate p\n");
  } else /*non-immediate*/ if (!RTEST(obj)) {
    if (obj == Qnil){
      yajl_gen_null(ctx->yajl);
      return;
    }
    if (obj == Qfalse) {
      yajl_gen_bool(ctx->yajl, false);
      return;
    }
  }

  if(BUILTIN_TYPE(obj) == T_STRING && (!(RBASIC(obj)->flags & RSTRING_NOEMBED))){
    //printf("embedded string\n");
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
      printf("IFUNC NODE: %p %p %p\n", obj->nd_cfnc, obj->u2.node, (void*)obj->nd_aid /*u3 - aid id- - aka frame_this_func?*/);
      //FIXME: closures may leak references?
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
  yg_cstring("refs");
  yajl_gen_array_open(ctx->yajl);
  if(RHASH_SIZE(obj) > 0){
    //TODO: mark keys and values separately?
    st_foreach(RHASH(obj)->ntbl, dump_keyvalue, (st_data_t)ctx);
  }
  yajl_gen_array_close(ctx->yajl);
}

static int dump_method_entry_i(ID key, const rb_method_entry_t *me, st_data_t data){
  walk_ctx_t *ctx = (void*)data;
  if(key == ID_ALLOCATOR) {
    yg_cstring("___allocator___");
  } else {
    yg_cstring(rb_id2name(key));
  }

  const rb_method_definition_t *def = me->def;

  //gc_mark(objspace, me->klass, lev);?
  if (!def) {
    yajl_gen_null(ctx->yajl);
    return ST_CONTINUE;
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
  return ST_CONTINUE;
}

static int dump_iv_entry(st_data_t key, VALUE value, walk_ctx_t *ctx){
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

  ygh_int("id", NUM2LONG(rb_obj_id(obj))); //TODO: object_id is value>>2 ?
  ygh_cstring("bt", rb_builtin_type(obj));

  switch(BUILTIN_TYPE(obj)){ // no need to call TYPE(), as value is on heap
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
      yg_cstring("refs");
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
      yg_cstring("class");
      yg_id(rb_class_of(obj));
      yg_cstring("refs"); //ivars
      yajl_gen_array_open(ctx->yajl);
      {
      long i, len = ROBJECT_NUMIV(obj);
      VALUE *ptr = ROBJECT_IVPTR(obj);
      for (i = 0; i < len; i++) yg_id(*ptr++);
      }
      yajl_gen_array_close(ctx->yajl);
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
          yg_cstring("refs");
          yajl_gen_array_open(ctx->yajl); //TODO: what are iv keys?
          st_foreach(RCLASS_IV_TBL(obj), dump_iv_entry, (st_data_t)ctx);
          yajl_gen_array_close(ctx->yajl);
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
    // T(T_BIGNUM);
    // T(T_RATIONAL); // refs too (num/den)...
    // T(T_COMPLEX);

    // T(T_DATA); // data of extensions + raw bytecode etc., undumpable? maybe in some way mess with mark callback? (need to intercept rb_gc_mark :( )
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


static VALUE
rb_heapdump_dump(VALUE self, VALUE filename)
{
  struct walk_ctx ctx;
  memset(&ctx, 0, sizeof(ctx));

  Check_Type(filename, T_STRING);

  printf("Dump should go to %s\n", RSTRING_PTR(filename));
  ctx.file = fopen(RSTRING_PTR(filename), "wt");
  ctx.yajl = yajl_gen_alloc(NULL,NULL);
  yajl_gen_array_open(ctx.yajl);

  rb_objspace_each_objects(objspace_walker, &ctx);

  yajl_gen_array_close(ctx.yajl);
  flush_yajl(&ctx);
  yajl_gen_free(ctx.yajl);
  fclose(ctx.file);

  printf("Walker called %d times, seen %d live objects.\n", ctx.walker_called, ctx.live_objects);

  return Qnil;
}


void Init_heap_dump(){
  printf("heap_dump extension loading\n");
  //ruby-internal need to be required before linking us, but just in case..
  rb_require("internal/node");
  rb_require("yajl");
  init_node_type_descrips();

  CONST_ID(classid, "__classid__");

  rb_mHeapDumpModule = rb_define_module("HeapDump");
  rb_define_singleton_method(rb_mHeapDumpModule, "dump_ext", rb_heapdump_dump, 1);
}
