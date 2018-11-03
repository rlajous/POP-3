#ifndef PROXYPOP3NIO_H
#define PROXYPOP3NIO_H

void
proxyPop3_passive_accept(struct selector_key *key);

void
pop3_pool_destroy(void);

#endif
