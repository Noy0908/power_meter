/*
 * Copyright (c) 2017 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <stdlib.h>
#include <zephyr/posix/fcntl.h>
#include <zephyr/net/socket.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(tcp_sample, CONFIG_TCP_LOG_LEVEL);

#include "app.h"

// #ifdef CONFIG_NET_IPV6
// #define USE_IPV6
// #endif

#define TCP_THREAD_STACK_SIZE               4096
#define TCP_THREAD_PRIORITY 				5

#define SERVER_PORT 						12345
#define LISTEN_COUNT 						5

#define TX_QUEUE_COUNT						20

static k_tid_t tcp_server_tid;
static struct k_thread tcp_server;
static K_THREAD_STACK_DEFINE(tcp_server_thread_stack, TCP_THREAD_STACK_SIZE);


K_MSGQ_DEFINE(tx_send_queue, sizeof(socket_data_t), TX_QUEUE_COUNT, 4);

bool reconnect = false;


static int do_tcp_send(int sock, const uint8_t *data, int datalen)
{
	int ret = 0;
	uint32_t offset = 0;

	while (offset < datalen) {
		ret = send(sock, data + offset, datalen - offset, 0);
		if (ret < 0) {
			LOG_ERR("send() failed: %d, sent: %d", -errno, offset);
			ret = -errno;
			break;
		} else {
			// LOG_INF("Socket send data length = %d\n", ret);
			offset += ret;
		}
	}

	return ret;
}


static void receive_data_handle(uint8_t *data, uint16_t length)
{
	LOG_INF("Received %d bytes data from tcp server, data_segment:\"%s\"\n", length, data);
	uart_send_data(data, length);		//uart send back the received data with slip protocal, just for test
}


/* tcp client task */
static void tcp_client_thread(void)
{
	int ret = 0;
	struct pollfd fds[2];
	int client_socket = 0;
	uint8_t rec_buf[512] = {0};  /* For socket receive data. */
	
	k_sem_take(&lte_connected_sem, K_FOREVER);

	create_tcp_server_thread();

retry:
	LOG_WRN("LTE connected successfully, now we start tcp task!!!\n");

#if !defined(USE_IPV6)
	struct sockaddr_in server_addr = {0};

	client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (client_socket < 0) {
		LOG_ERR("error: socket(AF_INET): %d\n", errno);
		goto error_exit;
	}
	struct timeval socket_timeout = {.tv_sec = 3};

	ret = setsockopt(client_socket, SOL_SOCKET, SO_SNDTIMEO, &socket_timeout, sizeof(socket_timeout));
	if (ret) {
		LOG_ERR("setsockopt(%d) error: %d", SO_SNDTIMEO, -errno);
		ret = -errno;
		goto error_exit;
	}

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(CONFIG_TCP_SERVER_PORT);
	inet_pton(AF_INET, CONFIG_TCP_SERVER_ADDRESS_STATIC,&server_addr.sin_addr);
	//if we use the domain name, we should use the getaddrinfo() to get the server address
	// struct addrinfo *result;
	// struct addrinfo hints = {
	// 	.ai_family = AF_INET,
	// };
	// char ipv4_addr[NET_IPV4_ADDR_LEN];
	// ret = getaddrinfo(CONFIG_TCP_SERVER_ADDRESS_STATIC, NULL, &hints, &result);
	// if (ret) {
	// 	LOG_ERR("getaddrinfo, error: %d", ret);
	// 	goto error_exit;
	// }
	// server_addr.sin_addr.s_addr = ((struct sockaddr_in *)result->ai_addr)->sin_addr.s_addr;

	// inet_ntop(AF_INET, &server_addr.sin_addr.s_addr, ipv4_addr, sizeof(ipv4_addr));
	// LOG_INF("IPv4 Address found %s", ipv4_addr);
	// /* Free the address. */
	// freeaddrinfo(result);

	ret = connect(client_socket, (struct sockaddr *)&server_addr, sizeof(struct sockaddr_in));
	if (ret) {
		LOG_ERR("AF_INET connect() failed: %d", -errno);
		goto error_exit;
	}
#else
	struct sockaddr_in6 server_addr = {0};

	client_socket = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
	if (client_socket < 0) {
		printf("error: socket(AF_INET6): %d\n", errno);
		goto error_exit;
	}
	server_addr.sin6_family = AF_INET6;
	server_addr.sin6_port = htons(CONFIG_TCP_SERVER_PORT);
	inet_pton(AF_INET6, CONFIG_TCP_SERVER_ADDRESS_STATIC,&server_addr.sin6_addr);

	ret = connect(client_socket, (struct sockaddr *)&server_addr, sizeof(struct sockaddr_in6));
	if (ret) {
		LOG_ERR("AF_INET6 connect() failed: %d", -errno);
		goto error_exit;
	}
#endif
	
	fds[0].fd = client_socket;
	fds[0].events = POLLIN;

	while (1) {
		// ret = poll(fds, ARRAY_SIZE(fds), MSEC_PER_SEC * CONFIG_TCP_POLL_TIME);
		ret = poll(fds, ARRAY_SIZE(fds), 200);
		if (ret < 0) {
			LOG_WRN("poll() error: %d", ret);
			break;
		}
		else if (ret == 0) {
			/* timeout */
			// LOG_INF("TCP wait for poll event timeout at %lld.\n",k_ticks_to_ms_floor64(k_uptime_ticks()));
		}
		else{
			// LOG_DBG("[%d]sock events 0x%08x at %lld.\n", ret,fds[0].revents,k_ticks_to_ms_floor64(k_uptime_ticks()));
			if ((fds[0].revents & POLLIN) != 0) {
				/* receive data from server */
				memset(rec_buf, 0, sizeof(rec_buf));
				ret = recv(fds[0].fd, (void *)rec_buf, sizeof(rec_buf), MSG_DONTWAIT);
				if (ret < 0 && errno != EAGAIN) {
					LOG_WRN("recv() error: %d", -errno);
				} else if (ret > 0) {
					/** handle the received data from tcp server */
					receive_data_handle(rec_buf, ret);
				}
			}
			if ((fds[0].revents & POLLERR) != 0) {
				LOG_WRN("SOCK (%d): POLLERR", fds[0].fd);
				break;
			}
			if ((fds[0].revents & POLLNVAL) != 0) {
				LOG_WRN("SOCK (%d): POLLNVAL", fds[0].fd);
				break;
			}
			if ((fds[0].revents & POLLHUP) != 0) {
				/* Lose LTE connection / remote end close */
				LOG_WRN("SOCK (%d): POLLHUP", fds[0].fd);
				reconnect = true;
				k_sem_take(&lte_connected_sem, K_FOREVER);
				break;
			}
		}

		/***************** Events to send message queue. *****************************/
		{
			static uint8_t resend_count = 0;
			socket_data_t send_packet = {0};
			if (k_msgq_peek(&tx_send_queue, &send_packet) == 0)  
			{
				// LOG_INF("[%d]:%s\n",send_packet.length, send_packet.data);
				ret = do_tcp_send(client_socket, send_packet.data, send_packet.length);
				if(ret == send_packet.length)
				{
					/* send successfully, delete the queue message and free memory */
					k_msgq_get(&tx_send_queue, &send_packet, K_NO_WAIT);
					k_free(send_packet.data);
					resend_count = 0;
					LOG_DBG("Socket send [%d] successfully , the data is: \"%s\"\n",ret, send_packet.data);
				}
				else if(ret <= 0)
				{
					if(resend_count++ >= 3)
					{
						LOG_WRN("Socket has something wrong, we'd better reconnect it.\n");
						break;
					}
				}
			}
		}
	}

error_exit:
	if(client_socket)
	{
		close(client_socket);
	}
	k_sleep(K_SECONDS(3));	
	goto retry;
}


void handle_client_request(int client_sock, char *recv_buffer, int len) {
	if (strncmp(recv_buffer, "read power", strlen("read power")) == 0) {
		char *response = "power: 100W";
		send(client_sock, response, strlen(response), 0);
	} else {
		char *response = "error: unknown command";
		send(client_sock, response, strlen(response), 0);
	}
	// close(client_sock);
}


static void tcp_server_thread(void *arg1, void *arg2, void *arg3) {
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    int server_sock, client_sock;
    struct sockaddr_in addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    char recv_buffer[256];

    server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_sock < 0) {
        LOG_ERR("Failed to create server socket");
        return;
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERR("Bind failed");
        close(server_sock);
        return;
    }

    if (listen(server_sock, LISTEN_COUNT) < 0) {
        LOG_ERR("Listen failed");
        close(server_sock);
        return;
    }

    LOG_INF("TCP Server listening on port %d", SERVER_PORT);

    while (1) {
        client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_sock < 0) {
            LOG_ERR("Accept failed");
            continue;
        }

        LOG_INF("Client connected");
        int len = recv(client_sock, recv_buffer, sizeof(recv_buffer) - 1, 0);
        if (len > 0) {
            recv_buffer[len] = '\0';
            LOG_INF("Received: %s", recv_buffer);

			handle_client_request(client_sock, recv_buffer, len);
        }
        // close(client_sock);
    }
}

void create_tcp_server_thread(void) {
	tcp_server_tid = k_thread_create(&tcp_server, tcp_server_thread_stack, 
		TCP_THREAD_STACK_SIZE, tcp_server_thread, NULL, NULL, NULL, 
		TCP_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(tcp_server_tid, "tcp_server");
}


void kill_tcp_server_thread(void) {
	k_thread_abort(tcp_server_tid);
}



/** TCP client thread used to transparent transport data between tcp server and uart */
K_THREAD_DEFINE(tcp_client_tid, TCP_THREAD_STACK_SIZE,
		tcp_client_thread, NULL, NULL, NULL,
		TCP_THREAD_PRIORITY, 0, 0);

/** TCP server will be create from the tcp client thread */
// K_THREAD_DEFINE(tcp_server_tid, TCP_THREAD_STACK_SIZE, 
// 		tcp_server_thread, NULL, NULL, NULL, 
// 		TCP_THREAD_PRIORITY, 0, 0);