/* TCP TX worker: send mirrored GRBL output back to the connected socket. */

#include <stdio.h>
#include <errno.h>

#include "grbl.h"

#include <zephyr/kernel.h>
#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/unistd.h>

#include "tcp_tx.h"

static volatile int tx_running = 0;

void tcp_tx_begin(void)
{
	tx_running = 1;
}

void tcp_tx_stop(void)
{
	tx_running = 0;
	serial_tx_notify();
}

void tcp_tx_thread_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	int client = (int)(intptr_t)p1;
	uint8_t out[256];
	int used = 0;
	uint8_t ch;

	while (1) {
		if (!serial_net_tx_pop(&ch)) {
			if (!tx_running) {
				break;
			}
			serial_net_tx_wait();
			continue;
		}

		out[used++] = ch;
		if (used == (int)sizeof(out) || ch == '\n') {
			if (send(client, out, used, 0) < 0) {
				printf("tcp_tx: send error: %d\n", errno);
				break;
			}
			used = 0;
		}
	}

	if (used > 0) {
		(void)send(client, out, used, 0);
	}
}
