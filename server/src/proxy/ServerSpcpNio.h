

#ifndef PROJECT_SERVERSPCPNIO_H
#define PROJECT_SERVERSPCPNIO_H

#include "../utils/selector.h"

/** Handler del socket pasivo SCTP que atiende conexiones SPCP */
void
spcp_passive_accept(struct selector_key *key);


#endif
