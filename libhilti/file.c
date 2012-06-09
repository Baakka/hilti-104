
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <pthread.h>

#include "file.h"
#include "memory_.h"
#include "autogen/hilti-hlt.h"

// This struct describes one currently open file. We memory-manage this ourselves.
typedef struct __hlt_file_info {
    hlt_string path;    // The path of the file.
    int fd;             // The file descriptor.
    int writers;        // The number of file objects having the file open.

    struct __hlt_file_info* next; // We keep them in a list.
    struct __hlt_file_info* prev;
} __hlt_file_info;

// A HILTI file object. This is one reference-counted.
struct __hlt_file {
    __hlt_gchdr __gchdr;   // Header for memory management.
    __hlt_file_info* info; // The internal file object.
    hlt_enum type;         // Hilti_FileType_* constant.
    hlt_string charset;    // The charset for a text file.
    hlt_string path;       // The path of the file; keep here as well for threads.
    int8_t open;           // 1 if open, 0 if closed.
};

// A single write command inserted into the command queue.
typedef struct __hlt_cmd_file {
    __hlt_cmd cmd;             // The common header for all commands.
    hlt_file* file;            // The file to write to.
    int type;                  // 1 if it's a string; 0 if it's a bytes; and 2 if it's a close command.
    union {
        void* bytes;           // The bytes to write.
        void* string;          // The string to write.
    } data;
} __hlt_cmd_file;

// The list of currently open files. Read and write accesses to this list
// must be protected via the lock below.
static __hlt_file_info* files = 0;

// Lock to protect access to the list of open files;
pthread_mutex_t files_lock;

void hlt_file_dtor(hlt_type_info* ti, hlt_file* f)
{
    GC_DTOR(f->path, hlt_string);
}

static void fatal_error(const char* msg)
{
    fprintf(stderr, "file manager: %s\n", msg);
    exit(1);
}

static inline void acqire_lock(int* i)
{
    if ( ! hlt_is_multi_threaded() )
        return;

    hlt_pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, i);

    if ( pthread_mutex_lock(&files_lock) != 0 )
        fatal_error("cannot lock mutex");
}

static inline void release_lock(int i)
{
    if ( ! hlt_is_multi_threaded() )
        return;

    if ( pthread_mutex_unlock(&files_lock) != 0 )
        fatal_error("cannot unlock mutex");

    hlt_pthread_setcancelstate(i, NULL);
}

void __hlt_files_init()
{
    if ( hlt_is_multi_threaded() && pthread_mutex_init(&files_lock, 0) != 0 )
        fatal_error("cannot init mutex");
}

void __hlt_files_done()
{
    for ( __hlt_file_info* info = files; info; info = info->next ) {
        fsync(info->fd);

        GC_DTOR(info->path, hlt_string);
        hlt_free(info);
    }

    // if ( hlt_is_multi_threaded() && pthread_mutex_destroy(&files_lock) != 0 )
    //    fatal_error("cannot destroy mutex");

    // TODO: We don't destroy the mutex for now because the cmd-queue might
    // still need it when writing stuff out. The question is when can we
    // safely destroy it?
}

hlt_file* hlt_file_new(hlt_exception** excpt, hlt_execution_context* ctx)
{
    hlt_file* file = GC_NEW(hlt_file);
    if ( ! file ) {
        hlt_set_exception(excpt, &hlt_exception_out_of_memory, 0);
        return 0;
    }

    file->info = 0;
    file->open = 0;
    return file;
}

void hlt_file_open(hlt_file* file, hlt_string path, hlt_enum type, hlt_enum mode, hlt_string charset, hlt_exception** excpt, hlt_execution_context* ctx)
{
    if ( file->open ) {
        hlt_string err = hlt_string_from_asciiz("file already open", excpt, ctx);
        hlt_set_exception(excpt, &hlt_exception_io_error, err);
        return;
    }

    if ( *excpt )
        return;

    int s;
    acqire_lock(&s);

    // First see whether we know this file already.
    //
    // TODO: We do a simple string comparision here. Should come up with
    // something smarter ...

    __hlt_file_info* info;
    for ( info = files; info; info = info->next ) {
        if ( hlt_string_cmp(path, info->path, excpt, ctx) == 0 ) {
            // Already open.
            ++info->writers;
            goto init_instance;
        }
    }

    if ( *excpt )
        return;

    // Not open yet.

    char* fn = hlt_string_to_native(path, excpt, ctx);
    if ( *excpt )
        return;

    int8_t append = hlt_enum_equal(mode, Hilti_FileMode_Append, excpt, ctx);
    int oflags = O_CREAT | O_WRONLY | (append ? O_APPEND : O_TRUNC);
    int fd = open(fn, oflags, 0777);

    hlt_free(fn);

    if ( fd < 0 ) {
        hlt_string err = hlt_string_from_asciiz(strerror(errno), excpt, ctx);
        hlt_set_exception(excpt, &hlt_exception_io_error, err);
        GC_DTOR(err, hlt_string);
        goto error;
    }

    info = hlt_malloc(sizeof(__hlt_file_info));

    info->fd = fd;
    info->path = hlt_string_copy(path, excpt, ctx);
    info->writers = 1;
    info->prev = 0;
    info->next = files;

    if ( files )
        files->prev = info;

    files = info;

init_instance:
    file->info = info;
    file->type = type;
    file->charset = charset;
    file->path = path;
    file->open = 1;

    GC_CCTOR(file->path, hlt_string);

    goto done;

error:
    if ( file && file->info )
        --info->writers;

    file->info = 0;
    file->open = 0;

done:
    release_lock(s);
}

void hlt_file_close(hlt_file* file, hlt_exception** excpt, hlt_execution_context* ctx)
{
    if ( ! file->open ) {
        hlt_string err = hlt_string_from_asciiz("file not open", excpt, ctx);
        hlt_set_exception(excpt, &hlt_exception_io_error, err);
        GC_DTOR(err, hlt_string);
        return;
    }

    __hlt_cmd_file* cmd = hlt_malloc(sizeof(__hlt_cmd_file));
    __hlt_cmdqueue_init_cmd((__hlt_cmd*) cmd, __HLT_CMD_FILE);
    cmd->file = file;
    cmd->type = 2; // Close.
    __hlt_cmdqueue_push((__hlt_cmd*) cmd, excpt, ctx);

    file->open = 0;
    GC_CLEAR(file->path, hlt_string);
}

void hlt_file_write_string(hlt_file* file, hlt_string str, hlt_exception** excpt, hlt_execution_context* ctx)
{
    if ( ! file->open ) {
        hlt_string err = hlt_string_from_asciiz("file not open", excpt, ctx);
        hlt_set_exception(excpt, &hlt_exception_io_error, err);
        GC_DTOR(err, hlt_string);
        return;
    }

    hlt_string copy = hlt_string_copy(str, excpt, ctx);
    if ( *excpt )
        return;

    __hlt_cmd_file* cmd = hlt_malloc(sizeof(__hlt_cmd_file));
    __hlt_cmdqueue_init_cmd((__hlt_cmd*) cmd, __HLT_CMD_FILE);
    cmd->file = file;
    cmd->type = 1; // String.
    // Don't need to copy the string because it's not mutable.
    cmd->data.string = copy;
    __hlt_cmdqueue_push((__hlt_cmd*) cmd, excpt, ctx);
}

void hlt_file_write_bytes(hlt_file* file, hlt_bytes* bytes, hlt_exception** excpt, hlt_execution_context* ctx)
{
    if ( ! file->open ) {
        hlt_string err = hlt_string_from_asciiz("file not open", excpt, ctx);
        hlt_set_exception(excpt, &hlt_exception_io_error, err);
        return;
    }

    hlt_bytes* copy = hlt_bytes_copy(bytes, excpt, ctx);
    if ( *excpt )
        return;

    __hlt_cmd_file* cmd = hlt_malloc(sizeof(__hlt_cmd_file));
    __hlt_cmdqueue_init_cmd((__hlt_cmd*) cmd, __HLT_CMD_FILE);
    cmd->file = file;
    cmd->type = 0; // Bytes.
    cmd->data.bytes = copy;
    __hlt_cmdqueue_push((__hlt_cmd*) cmd, excpt, ctx);
}

static void _write_bytes(hlt_file* file, hlt_bytes* data, hlt_exception** excpt, hlt_execution_context* ctx)
{
    hlt_bytes_block block;
    hlt_iterator_bytes start = hlt_bytes_begin(data, excpt, ctx);
    hlt_iterator_bytes end = hlt_bytes_end(data, excpt, ctx);
    void* cookie = 0;
    char buf[5] = { '\\', 'x', 'X', 'X', '0' };

    assert(file->info);

    while ( 1 ) {
        cookie = hlt_bytes_iterate_raw(&block, cookie, start, end, excpt, ctx);

        if ( block.start == block.end )
            break;

        int8_t text = hlt_enum_equal(file->type, Hilti_FileType_Text, excpt, ctx);

        if ( text ) {
            // Need to escape unprintable characters. FIXME: We don't honor the
            // charset here yet, just encode everything that is not
            // representable in our current locale.
            const int8_t* s = block.start;
            const int8_t* e = s;

            while ( s < block.end ) {
                while ( e < block.end && isprint(*e) )
                    ++e;

                if ( e != s )
                    write(file->info->fd, s, e - s);

                if ( e < block.end ) {
                    // Unprintable character.
                    int n = hlt_util_uitoa_n(*e, buf + 2, 3, 16, 1);
                    write(file->info->fd, buf, n + 2);
                    ++e;
                }

                s = e;
            }
        }

        else if ( hlt_enum_equal(file->type, Hilti_FileType_Binary, excpt, ctx) )
            // Just write data directly.
            write(file->info->fd, block.start, block.end - block.start);

        else
            fatal_error("unknown file type");

        if ( ! cookie )
            break;
    }

    if ( hlt_enum_equal(file->type, Hilti_FileType_Text, excpt, ctx) )
        write(file->info->fd, "\n", 1);

    GC_DTOR(start, hlt_iterator_bytes);
    GC_DTOR(end, hlt_iterator_bytes);
}

void __hlt_file_cmd_internal(__hlt_cmd* c, hlt_exception** excpt, hlt_execution_context* ctx)
{
    __hlt_cmd_file* cmd = (__hlt_cmd_file*) c;

    // This will be called from the command queue thread only.
    switch ( cmd->type ) {

      case 0: {
          // Bytes.
          _write_bytes(cmd->file, cmd->data.bytes, excpt, ctx);
          GC_DTOR(cmd->data.bytes, hlt_bytes);
          break;
      }

      case 1: {
          // String.
          hlt_bytes* b = hlt_string_encode(cmd->data.string, cmd->file->charset, excpt, ctx);
          if ( *excpt )
              break;

          _write_bytes(cmd->file, b, excpt, ctx);
          GC_DTOR(b, hlt_bytes);
          GC_DTOR(cmd->data.string, hlt_string);
          break;
      }

      case 2: {
          // Close command.
          int s;
          acqire_lock(&s);

          assert(cmd->file->info->writers);

          if ( --cmd->file->info->writers == 0 ) {
              close(cmd->file->info->fd);

              // Delete from list.
              __hlt_file_info* cur;
              for ( cur = files; cur; cur = cur->next ) {
                  if ( cur != cmd->file->info )
                      continue;

                  if ( cur->prev )
                      cur->prev->next = cur->next;
                  else
                      files = cur->next;

                  if ( cur->next )
                      cur->next->prev = cur->prev;

                  break;
              }

              GC_DTOR(cmd->file->info->path, hlt_string);
              hlt_free(cmd->file->info);

              if ( ! cur )
                  fatal_error("file to close not found");
          }

          release_lock(s);
          break;
      }

      default:
        fatal_error("unknown data type in write");
    }
}

hlt_string hlt_file_to_string(const hlt_type_info* type, const void* obj, int32_t options, hlt_exception** excpt, hlt_execution_context* ctx)
{
    assert(type->type == HLT_TYPE_FILE);
    hlt_file* file = *((hlt_file **)obj);

    hlt_string prefix = hlt_string_from_asciiz("<file ", excpt, ctx);
    hlt_string postfix = hlt_string_from_asciiz(">", excpt, ctx);

    GC_CCTOR(file->path, hlt_string);
    hlt_string s = hlt_string_concat_and_unref(prefix, file->path, excpt, ctx);
    s = hlt_string_concat_and_unref(s, postfix, excpt, ctx);

    return s;
}

