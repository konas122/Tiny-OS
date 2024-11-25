#include "list.h"
#include "interrupt.h"


void list_init (struct list* list) {
    list->head.prev = NULL;
    list->head.next = &list->tail;
    list->tail.prev = &list->head;
    list->tail.next = NULL;
}


void list_insert_before(struct list_elem* before, struct list_elem* elem) { 
   enum intr_status old_status = intr_disable();

   before->prev->next = elem; 

   elem->prev = before->prev;
   elem->next = before;

   before->prev = elem;

   intr_set_status(old_status);
}


void list_push(list* plist, list_elem* elem) {
    list_insert_before(plist->head.next, elem);
}


void list_append(list* plist, list_elem* elem) {
    list_insert_before(&plist->tail, elem);
}

/* 使元素pelem脱离链表 */
void list_remove(list_elem* pelem) {
    enum intr_status old_status = intr_disable();

    pelem->prev->next = pelem->next;
    pelem->next->prev = pelem->prev;

    intr_set_status(old_status);
}


list_elem* list_pop(list* plist) {
    list_elem* elem = plist->head.next;
    list_remove(elem);
    return elem;
} 


bool elem_find(list* plist, list_elem* obj_elem) {
    list_elem* elem = plist->head.next;
    while (elem != &plist->tail) {
        if (elem == obj_elem) {
            return true;
        }
        elem = elem->next;
    }
    return false;
}


list_elem* list_traversal(list* plist, function func, int arg) {
    list_elem* elem = plist->head.next;
    if (list_empty(plist)) { 
        return NULL;
    }

    while (elem != &plist->tail) {
        if (func(elem, arg)) {
            return elem;
        }
        elem = elem->next;
    }
    return NULL;
}


uint32_t list_len(struct list* plist) {
    list_elem* elem = plist->head.next;
    uint32_t length = 0;
    while (elem != &plist->tail) {
        length++; 
        elem = elem->next;
    }
    return length;
}


bool list_empty(struct list* plist) {
    return (plist->head.next == &plist->tail ? true : false);
}
