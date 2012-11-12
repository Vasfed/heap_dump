//FIXME:!!!!! get this file from ruby source!


typedef struct {
    char *ptr;                  /* off + len <= capa */
    int off;
    int len;
    int capa;
} rb_io_buffer_t;


//#include "encoding.h" // and this also (rb_encoding + rb_econv_t)
//FIXME: nasty:
// typedef int rb_encoding;
// typedef struct rb_econv rb_econv_t;

typedef struct rb_io_t {
    int fd;                     /* file descriptor */
    FILE *stdio_file;       /* stdio ptr for read/write if available */
    int mode;           /* mode flags: FMODE_XXXs */
    rb_pid_t pid;       /* child's pid (for pipes) */
    int lineno;         /* number of lines read */
    VALUE pathv;        /* pathname for file */
    void (*finalize)(struct rb_io_t*,int); /* finalize proc */

    rb_io_buffer_t wbuf, rbuf;

    VALUE tied_io_for_writing;

    /*
     * enc  enc2 read action                      write action
     * NULL NULL force_encoding(default_external) write the byte sequence of str
     * e1   NULL force_encoding(e1)               convert str.encoding to e1
     * e1   e2   convert from e2 to e1            convert str.encoding to e2
     */
    struct rb_io_enc_t {
        rb_encoding *enc;
        rb_encoding *enc2;
        int ecflags;
        VALUE ecopts;
    } encs;

    rb_econv_t *readconv;
    rb_io_buffer_t cbuf;

    rb_econv_t *writeconv;
    VALUE writeconv_asciicompat;
    int writeconv_pre_ecflags;
    VALUE writeconv_pre_ecopts;
    int writeconv_initialized;

    VALUE write_lock;
} rb_io_t;