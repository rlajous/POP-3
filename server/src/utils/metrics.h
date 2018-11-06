#ifndef METRICS_H
#define METRICS_H

/**
 * Estructura utilizada para guardar
 * las metricas volatiles que mantiene
 * el proxy.
 * */
typedef struct {
    unsigned concurrent_connections;
    unsigned long long bytes;
    unsigned long historic_connections;
} metrics;

#endif
