#include "cs402.h"
#include "my402list.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int My402ListLength(My402List *list)
{
    return list->num_members;
}

int My402ListEmpty(My402List *list)
{
    if (list->num_members == 0)
        return TRUE;
    else
        return FALSE;
}

int My402ListAppend(My402List *list, void *obj)
{
    My402ListElem *newElem = (My402ListElem *)malloc(sizeof(My402ListElem));
    if (newElem == NULL)
        return FALSE;
    newElem->obj = obj;
    if (list->num_members == 0)
    {
        list->anchor.next = newElem;
        list->anchor.prev = newElem;
        newElem->next = &(list->anchor);
        newElem->prev = &(list->anchor);
        list->num_members++;
        return TRUE;
    }
    else
    {
        My402ListElem *lastElem = list->anchor.prev;
        lastElem->next = newElem;
        newElem->prev = lastElem;
        newElem->next = &(list->anchor);
        list->anchor.prev = newElem;
        list->num_members++;
        return TRUE;
    }
    return FALSE;
}

int My402ListPrepend(My402List *list, void *obj)
{
    My402ListElem *newElem = (My402ListElem *)malloc(sizeof(My402ListElem));
    if (newElem == NULL)
        return FALSE;
    newElem->obj = obj;

    // check to see if list is empty
    if (list->num_members == 0)
    {
        list->anchor.next = newElem;
        list->anchor.prev = newElem;
        newElem->next = &(list->anchor);
        newElem->prev = &(list->anchor);
        list->num_members++;
        return TRUE;
    }
    // if it's not empty then prepend to the first element in the list
    else
    {
        My402ListElem *firstElem = list->anchor.next;
        firstElem->prev = newElem;
        newElem->next = firstElem;
        newElem->prev = &(list->anchor);
        list->anchor.next = newElem;
        list->num_members++;
        return TRUE;
    }
    return FALSE;
}

void My402ListUnlink(My402List *list, My402ListElem *elem)
{
    if (list->num_members == 0)
        return;
    if (elem == NULL)
        return;
    My402ListElem *prevElem = elem->prev;
    My402ListElem *nextElem = elem->next;
    prevElem->next = nextElem;
    nextElem->prev = prevElem;
    free(elem);
    list->num_members--;
}

void My402ListUnlinkAll(My402List *list)
{
    if (list->num_members == 0)
        return;
    My402ListElem *elem = list->anchor.next;
    while (elem != &(list->anchor))
    {
        My402ListElem *nextElem = elem->next;
        free(elem);
        elem = nextElem;
    }
    list->anchor.next = &(list->anchor);
    list->anchor.prev = &(list->anchor);
    list->num_members = 0;
}

int My402ListInsertAfter(My402List *list, void *obj, My402ListElem *elem)
{
    if (elem == NULL)
        return My402ListAppend(list, obj);
    My402ListElem *newElem = (My402ListElem *)malloc(sizeof(My402ListElem));
    if (newElem == NULL)
        return FALSE;
    newElem->obj = obj;
    My402ListElem *nextElem = elem->next;
    elem->next = newElem;
    newElem->prev = elem;
    newElem->next = nextElem;
    nextElem->prev = newElem;
    list->num_members++;
    return TRUE;
}

int My402ListInsertBefore(My402List *list, void *obj, My402ListElem *elem)
{
    if (elem == NULL)
        return My402ListPrepend(list, obj);
    My402ListElem *newElem = (My402ListElem *)malloc(sizeof(My402ListElem));
    if (newElem == NULL)
        return FALSE;
    newElem->obj = obj;
    My402ListElem *prevElem = elem->prev;
    elem->prev = newElem;
    newElem->next = elem;
    newElem->prev = prevElem;
    prevElem->next = newElem;
    list->num_members++;
    return TRUE;
}

My402ListElem *My402ListFirst(My402List *list)
{
    if (list->num_members == 0)
        return NULL;
    return list->anchor.next;
}

My402ListElem *My402ListLast(My402List *list)
{
    if (list->num_members == 0)
        return NULL;
    return list->anchor.prev;
}

My402ListElem *My402ListNext(My402List *list, My402ListElem *elem)
{
    if (elem == NULL)
        return NULL;
    if (elem->next == &(list->anchor))
        return NULL;
    return elem->next;
}

My402ListElem *My402ListPrev(My402List *list, My402ListElem *elem)
{
    if (elem == NULL)
        return NULL;
    if (elem->prev == &(list->anchor))
        return NULL;
    return elem->prev;
}

My402ListElem *My402ListFind(My402List *list, void *obj)
{
    My402ListElem *elem = list->anchor.next;
    while (elem != &(list->anchor))
    {
        if (elem->obj == obj)
            return elem;
        elem = elem->next;
    }
    return NULL;
}

int My402ListInit(My402List *list)
{
    if (list == NULL)
        return FALSE;
    list->num_members = 0;
    list->anchor.next = &(list->anchor);
    list->anchor.prev = &(list->anchor);
    return TRUE;
}