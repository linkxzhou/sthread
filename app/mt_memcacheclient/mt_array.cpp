#include <stdlib.h>
#include "mt_array.h"

struct array * array_create(uint32_t n, size_t size)
{
    struct array *a;

    a = (struct array *)malloc(sizeof(*a));
    if (a == NULL) {
        return NULL;
    }

    a->elem = malloc(n * size);
    if (a->elem == NULL) 
    {
        free(a);
        return NULL;
    }

    a->nelem = 0;
    a->size = size;
    a->nalloc = n;

    return a;
}

void array_destroy(struct array *a)
{
    array_deinit(a);
    free(a);
}

rstatus_t array_init(struct array *a, uint32_t n, size_t size)
{
    a->elem = malloc(n * size);
    if (a->elem == NULL) 
    {
        return NC_ENOMEM;
    }

    a->nelem = 0;
    a->size = size;
    a->nalloc = n;

    return NC_OK;
}

void array_deinit(struct array *a)
{
    if (a->elem != NULL) 
    {
        free(a->elem);
    }
}

uint32_t array_idx(struct array *a, void *elem)
{
    uint8_t *p, *q;
    uint32_t off, idx;

    p = (uint8_t *)(a->elem);
    q = (uint8_t *)elem;
    off = (uint32_t)(q - p);

    idx = off / (uint32_t)a->size;

    return idx;
}

void * array_push(struct array *a)
{
    void *elem, *_new;
    size_t size;

    if (a->nelem == a->nalloc) 
    {
        size = a->size * a->nalloc;
        _new = realloc(a->elem, 2 * size);
        if (_new == NULL)
        {
            return NULL;
        }

        a->elem = _new;
        a->nalloc *= 2;
    }

    elem = (uint8_t *)a->elem + a->size * a->nelem;
    a->nelem++;

    return elem;
}

void * array_pop(struct array *a)
{
    void *elem;
    a->nelem--;
    elem = (uint8_t *)a->elem + a->size * a->nelem;

    return elem;
}

void * array_get(struct array *a, uint32_t idx)
{
    void *elem;
    elem = (uint8_t *)a->elem + (a->size * idx);

    return elem;
}

void * array_top(struct array *a)
{
    return array_get(a, a->nelem - 1);
}

void array_swap(struct array *a, struct array *b)
{
    struct array tmp;

    tmp = *a;
    *a = *b;
    *b = tmp;
}

void array_sort(struct array *a, array_compare_t compare)
{
    qsort(a->elem, a->nelem, a->size, compare);
}

rstatus_t array_each(struct array *a, array_each_t func, void *data)
{
    uint32_t i, nelem;

    for (i = 0, nelem = array_n(a); i < nelem; i++) 
    {
        void *elem = array_get(a, i);
        rstatus_t status;

        status = func(elem, data);
        if (status != NC_OK) 
        {
            return status;
        }
    }

    return NC_OK;
}
