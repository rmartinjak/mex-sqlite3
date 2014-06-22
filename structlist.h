#ifndef STRUCTLIST_H
#define STRUCTLIST_H

#include "mex.h"

struct structlist
{
    struct node {
        mxArray *array;
        struct node *next;
    } *head, *tail;
    int size;
    int num_fields;
};

void structlist_init(struct structlist *l);
void structlist_free(struct structlist *l);
int structlist_add(struct structlist *l, mxArray *a);
mxArray *structlist_collapse(struct structlist *l);
#endif
