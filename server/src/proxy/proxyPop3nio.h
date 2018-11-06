#ifndef PROXYPOP3NIO_H
#define PROXYPOP3NIO_H
/** Handler de socket pasivo que atiende conexiones TCP para pop3 */
void
proxyPop3_passive_accept(struct selector_key *key);

/** Libera pools internos */
void
pop3_pool_destroy(void);

#endif
