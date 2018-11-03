#ifndef METRICS_H
#define METRICS_H

typedef struct {
    unsigned concurrent_connections;
    unsigned long long bytes;
    unsigned long historic_connections;
} metrics;

#endif
