#include "mex.h"

#include "structlist.h"

static struct node *node_create(mxArray *a) {
    struct node *n = mxMalloc(sizeof *n);
    n->array = mxDuplicateArray(a);
    n->next = NULL;
    return n;
}

static void node_free(struct node *n) {
    if (!n) {
        return;
    }
    node_free(n->next);
    mxFree(n);
}

void structlist_init(struct structlist *l)
{
    l->head = l->tail = NULL;
    l->num_fields = 0;
    l->size = 0;
}

void structlist_free(struct structlist *l)
{
    if (!l) {
        return;
    }
    node_free(l->head);
    l->size = 0;
}


int structlist_add(struct structlist *l, mxArray *a)
{
    struct node *n = node_create(a);
    if (!n) {
        return -1;
    }
    if (!l->tail) {
        l->head = l->tail = n;
        l->num_fields = mxGetNumberOfFields(a);
    } else {
        if (mxGetNumberOfFields(a) != l->num_fields) {
            node_free(n);
            return -2;
        }
        l->tail->next = n;
        l->tail = n;
    }
    l->size += mxGetN(a);
    return 0;
}

mxArray *structlist_collapse(struct structlist *l)
{
    mxArray *a = mxCreateStructMatrix(1, l->size, 0, NULL);
    if (!l->size) {
        return a;
    }
    for (int i = 0; i < mxGetNumberOfFields(l->head->array); i++) {
        const char *n = mxGetFieldNameByNumber(l->head->array, i);
        mxAddField(a, n);
    }

    mwIndex index = 0;
    for (struct node *n = l->head; n; n = n->next) {
        for (size_t i = 0; i < mxGetN(n->array); i++) {
            for (int field = 0; field < l->num_fields; field++) {
                mxArray *val = mxGetFieldByNumber(n->array, i, field);
                mxSetFieldByNumber(a, index, field, mxDuplicateArray(val));
            }
            index++;
        }
        mxDestroyArray(n->array);
    }
    structlist_free(l);
    return a;
}
