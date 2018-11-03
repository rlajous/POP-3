//
// Created by francisco on 18/10/18.
//

#include <stdlib.h>
#include <string.h>
#include "request_queue.h"
#include "../pop3Parsers/pop3request.h"

void
queue_init(struct request_queue *q) {
    q->first = NULL;
    q->last = NULL;
    q->size = 0;
};

void
queue_request(struct request_queue *q, struct request *request) {
    struct node *node = malloc(sizeof(struct node));
    node->next = NULL;
    node->request = malloc(sizeof(struct request));
    memcpy(node->request, request, sizeof(struct request));

    if(q->first == NULL) {
        q->first = node;
    } else {
        q->last->next = node;
    }
    q->last  = node;
    q->size++;
}

struct request*
pop_request(struct request_queue *q) {
    struct node *node = q->first;
    struct request *request = node->request;
    q->first = node->next;
    if(q->first == NULL) {
        q->last = NULL;
    }
    q->size--;
    free(node);
    return request;
}

struct request*
peek_request(struct request_queue *q) {
    return q->first->request;
}

struct request*
peek_next_unsent(struct request_queue *q) {
    struct node *node = q->first;
    struct request *request = NULL;
    ssize_t size = -1;
    while(node != NULL && size < 0) {
        request = node->request;
        size = request->length;
        node = node->next;
    }
    return request;
}

extern bool
queue_is_empty(struct request_queue *q) {
    return q->first == NULL;
}