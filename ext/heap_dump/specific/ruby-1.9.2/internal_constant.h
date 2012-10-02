//from 1.9.2
typedef enum {
    CONST_PUBLIC    = 0x00,
    CONST_PRIVATE   = 0x01
} rb_const_flag_t;

typedef struct rb_const_entry_struct {
    rb_const_flag_t flag;
    VALUE value;            /* should be mark */
} rb_const_entry_t;
