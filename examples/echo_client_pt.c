#include "../bq_websocket_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
	#define _WIN32_LEAN_AND_MEAN
	#include <Windows.h>
	#define os_sleep() Sleep(10)
#elif defined(__EMSCRIPTEN__)
	#include <emscripten.h>
#else
	 #include <unistd.h>
	#define os_sleep() usleep(10000)
#endif

bqws_socket *ws;
size_t num_recv;
size_t timer;
size_t counter;

bool g_verbose = false;

static void client_log(void *user, bqws_socket *ws, const char *line)
{
	printf("@@ %s\n", line);
}

static void log_pt_error()
{
	bqws_pt_error err;
	if (bqws_pt_get_error(&err)) {
		char desc[256];
		bqws_pt_get_error_desc(desc, sizeof(desc), &err);
		fprintf(stderr, "%s %s error %d / 0x%08x\n%s\n",
			err.function, bqws_pt_error_type_str(err.type),
			(int)err.data, (unsigned)err.data, desc);
	}
}

void main_loop()
{
	if (!ws) {
		exit(0);
	}

	bqws_update(ws);

	if (num_recv >= 3) {
		if (timer++ % 10 == 0) {
			counter++;
			char msg[32];
			snprintf(msg, sizeof(msg), "%zu", counter);
			if (g_verbose) printf("%s...\n", msg);
			bqws_send_text(ws, msg);
			bqws_update_io_write(ws);

			if (counter >= 5) {
				bqws_queue_close(ws, BQWS_CLOSE_NORMAL, NULL, 0);
			}
		}
	}

	bqws_msg *msg;
	while ((msg = bqws_recv(ws)) != NULL) {
		num_recv++;
		if (g_verbose) printf("-> %s\n", msg->data);
		bqws_free_msg(msg);
	}

	if (bqws_is_closed(ws)) {
		bqws_stats stats = bqws_get_stats(ws);
		printf("Sent %zu messages: %zu bytes\n", (size_t)stats.send.total_messages, (size_t)stats.send.total_bytes);
		printf("Received %zu messages: %zu bytes\n", (size_t)stats.recv.total_messages, (size_t)stats.recv.total_bytes);

		bqws_free_socket(ws);
		ws = NULL;

		log_pt_error();
	}
}

int main(int argc, char **argv)
{
	for (int argi = 1; argi < argc; argi++) {
		if (!strcmp(argv[argi], "-v")) {
			g_verbose = true;
		}
	}

	#if defined(__EMSCRIPTEN__)
		g_verbose = true;
	#endif

	bqws_pt_init_opts init_opts = { 0 };
	#if !defined(NO_TLS)
		init_opts.ca_filename = "cacert.pem";
	#endif
	if (!bqws_pt_init(&init_opts)) {
		fprintf(stderr, "bqws_pt_init() failed\n");
		log_pt_error();
		return 1;
	}

	bqws_opts opts = { 0 };
	if (g_verbose) {
		opts.log_fn = &client_log;
	}

	#if !defined(NO_TLS)
		ws = bqws_pt_connect("wss://echo.websocket.org", NULL, &opts, NULL);
	#else
		ws = bqws_pt_connect("ws://echo.websocket.org", NULL, &opts, NULL);
	#endif

	if (!ws) {
		fprintf(stderr, "bqws_pt_connect() failed\n");
		log_pt_error();
		return 1;
	}

	bqws_pt_address addr = bqws_pt_get_address(ws);
	char addr_str[BQWS_PT_MAX_ADDRESS_FORMAT_LENGTH];
	bqws_pt_format_address(addr_str, sizeof(addr_str), &addr);
	printf("Connected to %s\n", addr_str);

	bqws_send_text(ws, "Hello world!");

	bqws_msg *msg = bqws_allocate_msg(ws, BQWS_MSG_TEXT, 4);
	memcpy(msg->data, "Test", 4);
	bqws_send_msg(ws, msg);

	bqws_send_begin(ws, BQWS_MSG_TEXT);
	bqws_send_append_str(ws, "Multi");
	bqws_send_append_str(ws, "Part");
	bqws_send_append_str(ws, "Message");
	bqws_send_finish(ws);

#if defined(__EMSCRIPTEN__)
	emscripten_set_main_loop(&main_loop, 60, 1);
#else
	for (;;) {
		os_sleep();
		main_loop();
	}
#endif

	return 0;
}
