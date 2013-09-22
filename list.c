/*
  Copyright (C) 2013 batiste.bieler@gmail.com 
    
  Generic double linked list implementation
 */
#ifndef LINKED_LIST
#define LINKED_LIST 1

#include <stdlib.h>
#include <stdio.h>

void assert(int value) {
    if(value)
        return;
    printf("List assert fail, quick exit\n");
    exit(1);
}

//Generic list Element
struct ListElement {
    struct ListElement *next;
    struct ListElement *prev;
    int refcount;
    void *data;
};
typedef struct ListElement ListElement;

//Generic List Structure
struct GenericList {
    int length;      //Number of elements in list
    struct ListElement *first;  //Ptr to first element in list
    struct ListElement *last;   //Ptr to last element in list
};
typedef struct GenericList GenericList;

void initList(GenericList *list) {
  list->length = 0;
  list->first = NULL;
  list->last = NULL;
}

GenericList * createList() {
  GenericList * list = (GenericList *)malloc(sizeof(GenericList));
  initList(list);
  return list;
}

ListElement * 
getFromList(GenericList *list, int n) {
  assert(list!=NULL);
  ListElement *el = NULL;
  int i = 0;
  for(el = list->first; el != NULL; el=el->next) {
    if(i==n) {
      return el;
    }
    i = i + 1;
  }
  // last will be returned
  return el;
}

ListElement *
addToList(GenericList *list, void *item) {
    //check inputs
    assert(item!=NULL);
    assert(list!=NULL);
    //Create generic element to hold item ptr
    ListElement *newElement = (ListElement *)malloc(sizeof(ListElement));
    assert(newElement != NULL);
    newElement->refcount = 1;
    list->length = list->length + 1;
    newElement->data = item;
    if (list->length == 1)
    {
      list->first = newElement;
      list->last = newElement;
      newElement->prev = NULL;
      newElement->next = NULL;
    }
    else
    {
      newElement->prev = list->last;
      newElement->next = NULL;
      list->last->next = newElement;
      list->last = newElement;
    }
    return newElement;
}

int 
removeFromList(GenericList *list, ListElement *toRemove) {
    // TODO: find a mechanisms to free deleted items

    //check inputs
    if(toRemove==NULL) {
        return 0;
    }
    assert(list!=NULL);
    ListElement *el;
    if (list->length == 0) {
        return 0;
    }

    if (list->length == 1) {
        if(toRemove == list->first) {
            list->length = 0;
            list->first = NULL;
            list->last = NULL;
            return 1;
        }
        return 0;
    }

    // there is at least 2 items in the list
    for(el = list->first; el != NULL; el=el->next) {
        // found a matching item
        if(el == toRemove) {
            // this is the first item
            if(el == list->first) {
                list->first = el->next;
                list->first->prev = NULL;
            // this is the last item
            } else if (el == list->last) {
                list->last = el->prev;
                list->last->next = NULL;
            } else {
                el->prev->next = el->next;
                el->next->prev = el->prev;
            }
            list->length = list->length - 1;
            return 1;
        }
    }
    return 0;
}

int 
emptyList(GenericList *list) {

    assert(list!=NULL);
    ListElement *el;
    ListElement *el2;

    for(el = list->first; el != NULL; el = el2) {
       el2 = el->next;
       free(el);
    }

    initList(list);
    return 0;
}

int 
destroyList(GenericList *list) {
  emptyList(list);
  free(list);
  return 0;
}

void 
displayList(GenericList *list) {
    ListElement *el;
    printf("List length %d\n", list->length);
    int i = 1;
    for(el = list->first; el != NULL; el=el->next, i=i+1) {
        printf("Item %d address %p\n", i, el->data);
    }
}

#endif
