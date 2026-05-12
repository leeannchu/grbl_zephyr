/*
 * Copyright (c) 2017 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "grbl.h"

#ifndef __ZEPHYR__

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#else

#include <zephyr/posix/netinet/in.h>
#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/arpa/inet.h>
#include <zephyr/posix/unistd.h>

#include <zephyr/net/socket.h>
#include <zephyr/kernel.h>

#endif

#include "tcp_rx.h"
#include "tcp_tx.h"

#define BIND_PORT 8500

static void feed_grbl_rx(const char *data, int length)
{
	for (int i = 0; i < length; i++) {
		serial_rx_irq((uint8_t)data[i]);
	}
}

void tcp_rx_session(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	int client = (int)(intptr_t)p1;

	while (1) {
		char buf[128];
		int len = recv(client, buf, sizeof(buf), 0);

		if (len <= 0) {
			if (len < 0) {
				printf("error: recv: %d\n", errno);
			}
			break;
		}

		feed_grbl_rx(buf, len);
	}
}

int main_tcp(void)
{
	int serv;
	struct sockaddr_in bind_addr = {
		.sin_family = AF_INET,
		.sin_port = htons(BIND_PORT),
		.sin_addr.s_addr = htonl(INADDR_ANY),
	};
	static int counter;

	serv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (serv < 0) {
		printf("error: socket: %d\n", errno);
		exit(1);
	}

	if (bind(serv, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
		printf("error: bind: %d\n", errno);
		exit(1);
	}

	if (listen(serv, 5) < 0) {
		printf("error: listen: %d\n", errno);
		exit(1);
	}

	printf("TCP server waits for a connection on port %d...\n", BIND_PORT);

	while (1) {
		struct sockaddr_in client_addr;
		socklen_t client_addr_len = sizeof(client_addr);
		char addr_str[32];
		int client = accept(serv, (struct sockaddr *)&client_addr,
				    &client_addr_len);

		if (client < 0) {
			printf("error: accept: %d\n", errno);
			continue;
		}

		inet_ntop(AF_INET, &client_addr.sin_addr,
			  addr_str, sizeof(addr_str));
		printf("Connection #%d from %s\n", counter++, addr_str);

		tcp_session_run(client);
		printf("Connection from %s closed\n", addr_str);
	}
	return 0;
}