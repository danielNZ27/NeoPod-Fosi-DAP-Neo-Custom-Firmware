#include <stddef.h>

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *pointer, size_t size);
void free(void *pointer);

void *malloc(size_t size)
{
    (void)size;
    return NULL;
}

void *calloc(size_t count, size_t size)
{
    (void)count;
    (void)size;
    return NULL;
}

void *realloc(void *pointer, size_t size)
{
    (void)pointer;
    (void)size;
    return NULL;
}

void free(void *pointer)
{
    (void)pointer;
}
