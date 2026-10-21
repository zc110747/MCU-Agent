/**
  ******************************************************************************
  * @file    web_server.c
  * @brief   Minimal HTTP/1.1 server on port 80 (Zephyr BSD socket API).
  *
  *  Routes
  *    GET  /             -> index.html (compiled into flash, web_index.h)
  *    GET  /api/status   -> {"led":0|1,"beep":0|1,"uptime_ms":N,"ip":"..."}
  *    POST /api/led      -> body "toggle" | "1" | "0"
  *    POST /api/beep     -> body "toggle" | "1" | "0"
  *
  *  Deliberately hand written instead of using the Zephyr HTTP server: the
  *  whole behaviour (and therefore the RAM/flash footprint) stays visible, and
  *  the response format is fully under our control for the JSON API.
  ******************************************************************************
  */
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/logging/log.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "web_server.h"
#include "board_io.h"
#include "sdram.h"
#include "web_index.h"

LOG_MODULE_REGISTER(web_server, LOG_LEVEL_INF);

#define HTTP_PORT           80
#define HTTP_RX_BUF         1024
#define HTTP_THREAD_STACK   4096
#define HTTP_THREAD_PRIO    5
#define HTTP_BACKLOG        4
#define HTTP_RECV_TIMEOUT   3000 /* ms, keeps one stuck client from blocking */

/* Zephyr requires the thread stack object to be aligned to
 * Z_THREAD_STACK_OBJ_ALIGN (32 bytes with the MPU enabled on Cortex-M), while
 * k_heap only guarantees 8. Over-allocate and align by hand. */
#define HTTP_STACK_ALIGN 32U

K_THREAD_STACK_DEFINE(http_stack_fallback, HTTP_THREAD_STACK);
static struct k_thread http_thread;

static k_thread_stack_t *http_stack = http_stack_fallback;
static size_t http_stack_size = K_THREAD_STACK_SIZEOF(http_stack_fallback);
static void *http_stack_raw;

/* Prefer the external SDRAM for the thread stack: it keeps 4 KB of internal
 * SRAM free and exercises the FMC path with a real workload. */
static void prepare_stack(void)
{
	const size_t need = K_THREAD_STACK_LEN(HTTP_THREAD_STACK) + HTTP_STACK_ALIGN;
	uintptr_t aligned;

	http_stack_raw = sdram_alloc(need);
	if (http_stack_raw == NULL) {
		LOG_INF("HTTP thread stack: internal SRAM (%u B)", (unsigned int)http_stack_size);
		return;
	}

	aligned = ((uintptr_t)http_stack_raw + (HTTP_STACK_ALIGN - 1U)) &
		  ~(uintptr_t)(HTTP_STACK_ALIGN - 1U);

	http_stack = (k_thread_stack_t *)aligned;
	http_stack_size = K_THREAD_STACK_LEN(HTTP_THREAD_STACK);

	LOG_INF("HTTP thread stack: SDRAM (%u B)", (unsigned int)http_stack_size);
}

static void send_all(int fd, const void *data, size_t len)
{
	const char *p = (const char *)data;

	while (len > 0) {
		ssize_t sent = zsock_send(fd, p, len, 0);

		if (sent <= 0) {
			return;
		}
		p += sent;
		len -= (size_t)sent;
	}
}

static void send_headers(int fd, int code, const char *reason, const char *ctype,
			 size_t body_len)
{
	char hdr[192];
	int n = snprintf(hdr, sizeof(hdr),
			 "HTTP/1.1 %d %s\r\n"
			 "Content-Type: %s\r\n"
			 "Content-Length: %u\r\n"
			 "Cache-Control: no-store\r\n"
			 "Connection: close\r\n"
			 "\r\n",
			 code, reason, ctype, (unsigned int)body_len);

	if (n > 0) {
		send_all(fd, hdr, (size_t)n);
	}
}

static void send_body(int fd, int code, const char *reason, const char *ctype,
		      const char *body, size_t len)
{
	send_headers(fd, code, reason, ctype, len);
	if (len > 0) {
		send_all(fd, body, len);
	}
}

static void send_text(int fd, int code, const char *reason, const char *body)
{
	send_body(fd, code, reason, "text/plain; charset=utf-8", body, strlen(body));
}

static void format_mac(const struct net_linkaddr *addr, char *out, size_t out_len)
{
	if (addr == NULL || addr->len < 6U || out_len < 18U) {
		snprintf(out, out_len, "00:00:00:00:00:00");
		return;
	}

	snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X",
		 addr->addr[0], addr->addr[1], addr->addr[2],
		 addr->addr[3], addr->addr[4], addr->addr[5]);
}

static void send_status_json(int fd)
{
	char json[192];
	char mac[18] = "00:00:00:00:00:00";
	struct net_if *iface = net_if_get_default();

	if (iface != NULL) {
		format_mac(net_if_get_link_addr(iface), mac, sizeof(mac));
	}

	snprintf(json, sizeof(json),
		 "{\"led\":%d,\"beep\":%d,\"uptime_ms\":%u,"
		 "\"ip\":\"%s\",\"mac\":\"%s\",\"sdram_free_kb\":%u}",
		 board_led_get() ? 1 : 0,
		 board_beep_get() ? 1 : 0,
		 (unsigned int)k_uptime_get_32(),
		 CONFIG_NET_CONFIG_MY_IPV4_ADDR, mac,
		 (unsigned int)(sdram_free_bytes() / 1024U));

	send_body(fd, 200, "OK", "application/json", json, strlen(json));
}

/* "toggle" (or anything unrecognised) flips the current state. */
static bool next_state(const char *body, size_t len, bool current)
{
	if (len == 0U) {
		return !current;
	}

	switch (body[0]) {
	case '1':
	case 't':
	case 'T':
		return true;
	case '0':
	case 'f':
	case 'F':
		return false;
	default:
		return !current;
	}
}

/* The minimal libc has no sscanf(), so the request line is split by hand:
 * "METHOD SP PATH SP VERSION". */
static bool parse_request_line(const char *rx, char *method, size_t method_len,
			       char *path, size_t path_len)
{
	const char *sp1 = strchr(rx, ' ');
	const char *sp2;
	size_t mlen;
	size_t plen;

	if (sp1 == NULL) {
		return false;
	}

	sp2 = strchr(sp1 + 1, ' ');
	if (sp2 == NULL) {
		return false;
	}

	mlen = (size_t)(sp1 - rx);
	plen = (size_t)(sp2 - (sp1 + 1));

	if (mlen == 0U || mlen >= method_len || plen == 0U || plen >= path_len) {
		return false;
	}

	memcpy(method, rx, mlen);
	method[mlen] = '\0';
	memcpy(path, sp1 + 1, plen);
	path[plen] = '\0';
	return true;
}

static void handle_client(int fd)
{
	char rx[HTTP_RX_BUF];
	size_t used = 0;
	char *hdr_end;
	char *body;
	size_t body_len;
	size_t content_length = 0;
	char method[8] = {0};
	char path[64] = {0};
	int rc;

	while (used < sizeof(rx) - 1U) {
		rc = zsock_recv(fd, rx + used, sizeof(rx) - 1U - used, 0);
		if (rc <= 0) {
			if (used == 0U) {
				return; /* client connected and vanished */
			}
			break;
		}
		used += (size_t)rc;
		rx[used] = '\0';
		if (strstr(rx, "\r\n\r\n") != NULL) {
			break;
		}
	}

	if (used == 0U) {
		return;
	}
	rx[used] = '\0';

	if (!parse_request_line(rx, method, sizeof(method), path, sizeof(path))) {
		send_text(fd, 400, "Bad Request", "bad request\n");
		return;
	}

	hdr_end = strstr(rx, "\r\n\r\n");
	if (hdr_end == NULL) {
		send_text(fd, 431, "Request Header Fields Too Large", "header too large\n");
		return;
	}

	body = hdr_end + 4;
	body_len = used - (size_t)(body - rx);

	{
		char *cl = strstr(rx, "Content-Length:");

		if (cl == NULL) {
			cl = strstr(rx, "content-length:");
		}
		if (cl != NULL) {
			content_length = (size_t)atoi(cl + strlen("Content-Length:"));
		}
	}

	/* Pull in a body that did not arrive with the headers. */
	while (body_len < content_length && (body - rx) + body_len < (long)sizeof(rx) - 1) {
		rc = zsock_recv(fd, body + body_len, (size_t)sizeof(rx) - 1U - (body_len + (size_t)(body - rx)), 0);
		if (rc <= 0) {
			break;
		}
		body_len += (size_t)rc;
	}
	body[body_len] = '\0';

	bool is_post = (strcmp(method, "POST") == 0);

	if (is_post && strcmp(path, "/api/led") == 0) {
		bool want = next_state(body, body_len, board_led_get());

		if (board_led_set(want) != 0) {
			send_text(fd, 500, "Internal Server Error", "led write failed\n");
			return;
		}
		send_status_json(fd);
		return;
	}

	if (is_post && strcmp(path, "/api/beep") == 0) {
		bool want = next_state(body, body_len, board_beep_get());

		if (board_beep_set(want) != 0) {
			send_text(fd, 500, "Internal Server Error", "beep write failed\n");
			return;
		}
		send_status_json(fd);
		return;
	}

	if (!is_post && strcmp(path, "/api/status") == 0) {
		send_status_json(fd);
		return;
	}

	if (!is_post && (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0)) {
		send_body(fd, 200, "OK", "text/html; charset=utf-8",
			  web_index_html, strlen(web_index_html));
		return;
	}

	if (strcmp(path, "/favicon.ico") == 0) {
		send_headers(fd, 204, "No Content", "image/x-icon", 0);
		return;
	}

	send_text(fd, 404, "Not Found", "not found\n");
}

static void http_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(HTTP_PORT),
		.sin_addr = { .s_addr = htonl(INADDR_ANY) },
	};
	struct timeval tv = {
		.tv_sec = HTTP_RECV_TIMEOUT / 1000,
		.tv_usec = (HTTP_RECV_TIMEOUT % 1000) * 1000,
	};
	int listen_fd;
	int opt = 1;

	listen_fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listen_fd < 0) {
		LOG_ERR("socket() failed (%d)", errno);
		return;
	}

	(void)zsock_setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	if (zsock_bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		LOG_ERR("bind() failed (%d)", errno);
		zsock_close(listen_fd);
		return;
	}

	if (zsock_listen(listen_fd, HTTP_BACKLOG) < 0) {
		LOG_ERR("listen() failed (%d)", errno);
		zsock_close(listen_fd);
		return;
	}

	LOG_INF("HTTP server listening on %s:%d", CONFIG_NET_CONFIG_MY_IPV4_ADDR, HTTP_PORT);

	for (;;) {
		struct sockaddr_in peer;
		socklen_t peer_len = sizeof(peer);
		int fd = zsock_accept(listen_fd, (struct sockaddr *)&peer, &peer_len);

		if (fd < 0) {
			LOG_WRN("accept() failed (%d)", errno);
			k_msleep(50);
			continue;
		}

		(void)zsock_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		(void)zsock_setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

		handle_client(fd);
		zsock_close(fd);
	}
}

int web_server_start(void)
{
	k_tid_t tid;

	prepare_stack();

	tid = k_thread_create(&http_thread, http_stack, http_stack_size,
			      http_thread_fn, NULL, NULL, NULL,
			      HTTP_THREAD_PRIO, 0, K_NO_WAIT);

	if (tid == NULL) {
		return -ENOMEM;
	}

	k_thread_name_set(tid, "httpd");
	return 0;
}
