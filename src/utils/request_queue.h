//
// Created by francisco on 18/10/18.
//

#ifndef PROJECT_REQUEST_QUEUE_H
#define PROJECT_REQUEST_QUEUE_H

#include "request.h"

/**
 * A simple queue holding the relevant
 * information parsed from the requests.
 * */
struct node {
    struct request request;
    struct node *next;
}node;

struct request_queue {
    struct node *first;
    struct node *last;
    unsigned int size;
}request_queue;

void
queue_init(struct request_queue *q);

void
queue_request(struct request_queue *q, struct request request);

struct request
pop_request(struct request_queue *q);

extern bool
queue_is_empty(struct request_queue *q);


#endif //PROJECT_REQUEST_QUEUE_H
