//1.9.2

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

typedef struct rb_fiber_struct {
    rb_context_t cont;
    VALUE prev;
    enum fiber_status status;
    struct rb_fiber_struct *prev_fiber;
    struct rb_fiber_struct *next_fiber;
} rb_fiber_t;

