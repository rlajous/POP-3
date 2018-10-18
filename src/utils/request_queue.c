//
// Created by francisco on 18/10/18.
//

#include <stdlib.h>
#include "request_queue.h"

struct Node {
    struct request request;
    struct Node *next;
}Node;

struct Queue {
    struct Node *first;
    struct Node *last;
    unsigned int size;
}Queue;

void
init(struct Queue *q) {
    q->first = NULL;
    q->last = NULL;
    q->size = 0;
};

void
queue(struct Queue *q, struct request request) {
    struct Node *node = malloc(sizeof(Node));
    node->request = request;
    q->last->next = node;
    q->last = node;
}

struct request
pop(struct Queue *q) {
    struct Node *node = q->first;
    struct request request = node->request;
    q->first = node->next;
    free(node);
    return request;
}
