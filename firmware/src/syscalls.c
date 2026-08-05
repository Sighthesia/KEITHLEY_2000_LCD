#include <stddef.h>
#include <stdint.h>

void _exit(int status)
{
    (void)status;
    for (;;) {
    }
}

int _close(int fd)
{
    (void)fd;
    return -1;
}

int _lseek(int fd, int offset, int whence)
{
    (void)fd;
    (void)offset;
    (void)whence;
    return -1;
}

int _read(int fd, char *buf, int count)
{
    (void)fd;
    (void)buf;
    (void)count;
    return 0;
}

int _write(int fd, const char *buf, int count)
{
    (void)fd;
    (void)buf;
    (void)count;
    return 0;
}

void *_sbrk(ptrdiff_t incr)
{
    extern char _end;
    static char *heap_end;
    char *prev;
    (void)incr;
    if (heap_end == 0) {
        heap_end = &_end;
    }
    prev = heap_end;
    return prev;
}
