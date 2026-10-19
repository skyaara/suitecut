/* SPDX-License-Identifier: MIT */
#include <suitecut/client.h>
#include <stdio.h>
#include <string.h>

/* A plain C consumer. Pass any native engine command object as argv[2]. */
int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "Usage: suitecut-native-command /absolute/suitecut-browser '{\"command\":\"evaluate\",\"expression\":\"40+2\"}'\n");
    return 2;
  }
  sc_launch_options options;
  sc_launch_options_init(&options);
  options.executable = argv[1];
  sc_error error;
  sc_client *client = NULL;
  if (sc_client_launch(&options, &client, &error) != SC_OK) {
    fprintf(stderr, "%s\n", error.message);
    return 1;
  }
  sc_packet *packet = NULL;
  int failed = sc_client_call(client, argv[2], strlen(argv[2]), 30000, &packet, &error) != SC_OK;
  if (failed) fprintf(stderr, "%s\n", error.message);
  else {
    size_t size;
    const char *header = sc_packet_header(packet, &size);
    fwrite(header, 1, size, stdout);
    putchar('\n');
    const unsigned char *payload = sc_packet_payload(packet, &size);
    if (size) { fwrite(payload, 1, size, stdout); putchar('\n'); }
  }
  sc_packet_destroy(packet);
  if (sc_client_close(client, &error) != SC_OK) {
    fprintf(stderr, "%s\n", error.message);
    failed = 1;
  }
  sc_client_destroy(client);
  return failed;
}
