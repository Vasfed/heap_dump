// this is unused, just example

#define __asm(...)
#define __asm__(...) rb_bug("asm replaced")
#define __inline
#define __inline__

#define __extension__ //???
#define __attribute__(x)

// #define PRI_SIZE_PREFIX

#undef __BLOCKS__
#undef __GNUC__

//TODO: on other systems there may be other builtins etc..

//__builtin_va_list
