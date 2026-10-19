/* SPDX-License-Identifier: MIT */
#include <suitecut/client.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expr); return 1; } } while (0)
static int packet(const char *header) {
  const uint32_t size = (uint32_t)strlen(header);
  const unsigned char prefix[4] = {(unsigned char)size, (unsigned char)(size >> 8),
    (unsigned char)(size >> 16), (unsigned char)(size >> 24)};
  return fwrite(prefix, 1, 4, stdout) == 4 && fwrite(header, 1, size, stdout) == size && fflush(stdout) == 0;
}
static int fake_engine(void) {
  if (!packet("{\"version\":1,\"type\":\"ready\",\"pageId\":1,\"bytes\":0}")) return 2;
  char *line = NULL; size_t capacity = 0;
  while (getline(&line, &capacity, stdin) >= 0) {
    const char *id = strstr(line, "\"id\":");
    if (!id) { free(line); return 3; }
    const int value = atoi(id + 5);
    char header[256];
    if (strstr(line, "reject"))
      snprintf(header, sizeof(header), "{\"version\":1,\"type\":\"response\",\"id\":%d,\"pageId\":1,\"bytes\":0,\"ok\":false,\"error\":\"expected failure\",\"detail\":42}", value);
    else
      snprintf(header, sizeof(header), "{\"version\":1,\"type\":\"response\",\"id\":%d,\"pageId\":1,\"bytes\":0,\"ok\":true}", value);
    if (!packet(header)) { free(line); return 4; }
  }
  free(line); return 0;
}
int main(int argc, char **argv) {
  for (int i = 1; i < argc; ++i) if (strcmp(argv[i], "--fake-engine") == 0) return fake_engine();
  sc_launch_options options;
  sc_launch_options_init(&options);
  CHECK(sc_abi_version() == SC_ABI_VERSION);
  CHECK(options.struct_size == sizeof(options));
  CHECK(options.abi_version == SC_ABI_VERSION);
  CHECK(options.width == 1280 && options.height == 720);
  sc_error error;
  sc_client *client = (sc_client *)1;
  CHECK(sc_client_launch(&options, &client, &error) == SC_INVALID_ARGUMENT);
  CHECK(client == NULL && error.code == SC_INVALID_ARGUMENT && strlen(error.message) > 0);
  options.executable = "/path/that/does/not/exist";
  options.abi_version++;
  CHECK(sc_client_launch(&options, &client, &error) == SC_INVALID_ARGUMENT);
  CHECK(client == NULL);
  options.abi_version = SC_ABI_VERSION;
  options.struct_size--;
  CHECK(sc_client_launch(&options, &client, &error) == SC_INVALID_ARGUMENT);
  sc_request *request = (sc_request *)1;
  CHECK(sc_client_submit(NULL, "{}", 2, 10, &request, &error) == SC_INVALID_ARGUMENT);
  CHECK(request == NULL);
  sc_packet *packet = (sc_packet *)1;
  CHECK(sc_client_call(NULL, "{}", 2, 10, &packet, &error) == SC_INVALID_ARGUMENT && !packet);
  CHECK(sc_request_wait(NULL, 0, &packet, &error) == SC_INVALID_ARGUMENT && !packet);
  CHECK(sc_client_poll(NULL, 0, &packet, &error) == SC_INVALID_ARGUMENT && !packet);
  CHECK(sc_client_ready(NULL, &packet, &error) == SC_INVALID_ARGUMENT && !packet);
  CHECK(sc_request_cancel(NULL, &error) == SC_INVALID_ARGUMENT);
  CHECK(sc_client_stats(NULL, NULL, &error) == SC_INVALID_ARGUMENT);
  CHECK(sc_client_close(NULL, &error) == SC_INVALID_ARGUMENT);
  size_t size = 1;
  CHECK(sc_packet_header(NULL, &size) == NULL && size == 0);
  size = 1;
  CHECK(sc_packet_payload(NULL, &size) == NULL && size == 0);
  CHECK(sc_request_id(NULL) == 0);
  sc_packet_destroy(NULL); sc_request_destroy(NULL); sc_client_destroy(NULL);
  sc_connection_options connection;
  sc_connection_options_init(&connection);
  CHECK(connection.struct_size==sizeof(connection) && connection.abi_version==SC_ABI_VERSION);
  CHECK(connection.headers_json==NULL && connection.tls_ca_file==NULL);
  CHECK(sc_client_connect_remote((sc_remote_backend)99,"ws://127.0.0.1:1/session",NULL,&connection,&client,&error)==SC_INVALID_ARGUMENT && client==NULL);
  CHECK(sc_client_connect_firefox(NULL,NULL,&client,&error)==SC_INVALID_ARGUMENT && client==NULL);
  sc_connection_options_init(NULL);
  sc_launch_options_init(NULL);
  sc_launch_options_init(&options);
  options.executable = "/proc/self/exe";
  const char *arguments[] = {"--fake-engine"};
  options.arguments = arguments; options.argument_count = 1;
  CHECK(sc_client_launch(&options, &client, &error) == SC_OK);
  CHECK(sc_client_ready(client, &packet, &error) == SC_OK);
  const char *header = sc_packet_header(packet, &size);
  CHECK(header && size && strstr(header, "ready"));
  sc_packet_destroy(packet);
  const char command[] = "{\"command\":\"echo\",\"pageId\":1}";
  CHECK(sc_client_submit(client, command, sizeof(command) - 1, 1000, &request, &error) == SC_OK);
  CHECK(sc_request_id(request) > 0);
  CHECK(sc_request_wait(request, 1000, &packet, &error) == SC_OK);
  CHECK(strstr(sc_packet_header(packet, NULL), "response"));
  sc_packet_destroy(packet);
  CHECK(sc_request_wait(request, 0, &packet, &error) == SC_ALREADY_CONSUMED && packet == NULL);
  sc_request_destroy(request);
  CHECK(sc_client_call(client, command, sizeof(command) - 1, 1000, &packet, &error) == SC_OK);
  sc_packet_destroy(packet);
  sc_stats stats;
  CHECK(sc_client_stats(client, &stats, &error) == SC_OK && stats.pending == 0 && stats.responses == 2);
  const char rejected[] = "{\"command\":\"reject\",\"pageId\":1}";
  CHECK(sc_client_call(client, rejected, sizeof(rejected) - 1, 1000, &packet, &error) == SC_OK);
  CHECK(strstr(sc_packet_header(packet, NULL), "expected failure") && strstr(sc_packet_header(packet, NULL), "\"detail\":42"));
  sc_packet_destroy(packet);
  CHECK(sc_client_submit(client, command, sizeof(command) - 1, 1000, &request, &error) == SC_OK);
  CHECK(sc_request_wait(request, 1000, &packet, &error) == SC_OK);
  sc_client_destroy(client); /* packet/request ownership survives client destruction */
  CHECK(sc_packet_header(packet, &size) != NULL && size > 0);
  sc_packet_destroy(packet); sc_request_destroy(request);
  puts("C ABI validation passed");
  return 0;
}
