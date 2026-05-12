#ifndef TCP_TX_H
#define TCP_TX_H

void tcp_tx_begin(void);
void tcp_tx_stop(void);
void tcp_tx_thread_entry(void *p1, void *p2, void *p3);

#endif /* TCP_TX_H */