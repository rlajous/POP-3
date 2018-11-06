//
// Created by francisco on 18/10/18.
//

#ifndef PROJECT_REQUEST_QUEUE_H
#define PROJECT_REQUEST_QUEUE_H

#include <stdbool.h>
#include <unistd.h>

/**
 * Una simple cola de requests que mantiene
 * la informacion relevante que se parseo
 * de cada request
 * */
struct node {
    struct request *request;
    struct node *next;
}node;

struct request_queue {
    struct node *first;
    struct node *last;
    unsigned int size;
}request_queue;

/** Inicializa la cola*/
void
queue_init(struct request_queue *q);

/** Encola un request */
void
queue_request(struct request_queue *q, struct request *request);

/** Remueve un request del principio
 *  de la cola*/
struct request*
pop_request(struct request_queue *q);

/** Devuelve el primer request de la cola
 *  sin removerlo de la misma*/
struct request*
peek_request(struct request_queue *q);

/** devuelve el primer request que no tenga
 *  longitud negativa (ya se mando en su totalidad)
*/
struct request*
peek_next_unsent(struct request_queue *q);

/** Verdadero si la cola esta vacía,
 *  falso de lo contrario*/
extern bool
queue_is_empty(struct request_queue *q);


#endif //PROJECT_REQUEST_QUEUE_H
