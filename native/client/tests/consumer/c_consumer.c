/* SPDX-License-Identifier: MIT */
#include <suitecut/client.h>
int main(void) {
  sc_launch_options options;
  sc_launch_options_init(&options);
  if (sc_abi_version() != SC_ABI_VERSION || options.struct_size != sizeof(options) ||
      options.abi_version != SC_ABI_VERSION || options.width != 1280) return 1;
  sc_error error;
  sc_client *client = NULL;
  if (sc_client_launch(&options, &client, &error) != SC_INVALID_ARGUMENT || client != NULL) return 2;
  return 0;
}
