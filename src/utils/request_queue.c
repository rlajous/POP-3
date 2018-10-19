//
// Created by francisco on 18/10/18.
//

#include <stdlib.h>
#include <string.h>
#include "request_queue.h"

void
queue_init(struct request_queue *q) {
    q->first = NULL;
    q->last = NULL;
    q->size = 0;
};

void
queue_request(struct request_queue *q, struct request *request) {
    struct node *node = malloc(sizeof(struct node));
    node->request = malloc(sizeof(struct request));
    memcpy(node->request, request, sizeof(struct request));

    if(q->first == NULL) {
        q->first = node;
    } else {
        q->last->next = node;
    }
    q->last = node;
}

struct request*
pop_request(struct request_queue *q) {
    struct node *node = q->first;
    struct request *request = node->request;
    q->first = node->next;
    free(node);
    return request;
}

extern bool
queue_is_empty(struct request_queue *q) {
    return q->first == NULL;
}