/* SPDX-License-Identifier: MIT */
/* embed.h first and alone: it must compile as C and link from an installed SDK. */
#include <suitecut/embed.h>
#include <string.h>
int main(void) {
  sc_runtime_options options;
  sc_runtime_info info;
  sc_session_options session_options;
  sc_error error;
  sc_session *session = NULL;
  char code[32];
  if (sc_embed_abi_version() != SC_EMBED_ABI_VERSION || sc_abi_version() != SC_ABI_VERSION) return 1;
  sc_runtime_options_init(&options);
  sc_runtime_info_init(&info);
  if (options.struct_size != sizeof(options) || options.abi_version != SC_EMBED_ABI_VERSION ||
      options.protocol_min != 1 || options.protocol_max != 1) return 2;
  options.path = "relative/runtime";
  if (sc_runtime_inspect(&options, &info, &error) != SC_INVALID_ARGUMENT) return 3;
  sc_session_options_init(&session_options);
  if (session_options.role != SC_ROLE_HOST) return 4;
  if (sc_session_open(&session_options, &session, &error) != SC_INVALID_ARGUMENT || session != NULL) return 5;
  /* Only SC_REFUSED errors carry a refusal code. */
  if (sc_error_refusal(&error, code, sizeof code) != 0) return 6;
  if (sc_session_close(NULL, 0, &error) != SC_INVALID_ARGUMENT) return 7;
  sc_session_destroy(NULL);
  return strlen(error.message) ? 0 : 8;
}
