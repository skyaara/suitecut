/* SPDX-License-Identifier: MIT */
/* embed.h compiles as C, and its init functions fill struct_size and the
 * embedding ABI version. */
#include <stdio.h>
#include <string.h>

#include "suitecut/embed.h"

#define EXPECT(condition)                                     \
  do {                                                        \
    if (!(condition)) {                                       \
      fprintf(stderr, "FAIL: %s\n", #condition);              \
      return 1;                                               \
    }                                                         \
  } while (0)

int main(void) {
  sc_runtime_options runtime;
  sc_session_options session;
  sc_view_options view;
  sc_surface_sink sink;
  sc_frame frame;
  sc_tab tab;
  sc_pointer_event pointer;
  sc_ime_composition composition;
  sc_error error;
  char code[32];
  sc_runtime_options_init(&runtime);
  sc_session_options_init(&session);
  sc_view_options_init(&view);
  sc_surface_sink_init(&sink);
  sc_frame_init(&frame);
  sc_tab_init(&tab);
  sc_pointer_event_init(&pointer);
  sc_ime_composition_init(&composition);
  EXPECT(composition.struct_size == sizeof composition && composition.abi_version == SC_EMBED_ABI_VERSION);
  EXPECT(composition.selection_start == -1 && composition.selection_end == -1 && composition.replace_start == -1 &&
         composition.replace_end == -1 && composition.text == NULL && composition.size == 0);
  EXPECT(SC_SESSION_PROMPTS == 2u && SC_EVENT_PROMPT_CLOSED == 10 && SC_EDIT_DELETE == 7);
  EXPECT(sc_embed_abi_version() == SC_EMBED_ABI_VERSION && sc_abi_version() == SC_ABI_VERSION);
  EXPECT(runtime.struct_size == sizeof runtime && runtime.abi_version == SC_EMBED_ABI_VERSION);
  EXPECT(runtime.protocol_min == 1 && runtime.protocol_max == 1);
  EXPECT(session.struct_size == sizeof session && session.role == SC_ROLE_HOST);
  EXPECT(view.struct_size == sizeof view && sink.kind == SC_SINK_LIBRARY && frame.surface == NULL);
  EXPECT(tab.struct_size == sizeof tab && pointer.click_count == 1 && pointer.button == SC_BUTTON_LEFT);
  error.code = SC_REFUSED;
  strcpy(error.message, "human_in_control: The person is using this tab.");
  EXPECT(sc_error_refusal(&error, code, sizeof code) == 16 && strcmp(code, "human_in_control") == 0);
  error.code = SC_ERROR;
  EXPECT(sc_error_refusal(&error, code, sizeof code) == 0 && code[0] == 0);
  /* The answers for a person's prompts link and check their arguments. */
  EXPECT(sc_prompt_answer_files(NULL, "file:2:1", NULL, 0, &error) == SC_INVALID_ARGUMENT);
  EXPECT(sc_prompt_answer_login(NULL, "auth:2:1", "a", NULL, &error) == SC_INVALID_ARGUMENT);
  EXPECT(sc_prompt_answer_download(NULL, "download:2:1", NULL, &error) == SC_INVALID_ARGUMENT);
  EXPECT(sc_prompt_cancel(NULL, NULL, &error) == SC_INVALID_ARGUMENT);
  EXPECT(sc_prompt_answer_permission(NULL, NULL, 1, &error) == SC_INVALID_ARGUMENT);
  EXPECT(sc_prompt_answer_certificate(NULL, "", 0, &error) == SC_INVALID_ARGUMENT);
  EXPECT(sc_session_open(&session, NULL, &error) == SC_INVALID_ARGUMENT);
  session.abi_version = 99;
  {
    sc_session *out = NULL;
    EXPECT(sc_session_open(&session, &out, &error) == SC_INVALID_ARGUMENT && out == NULL);
  }
  puts("embed_header_test: all checks passed");
  return 0;
}
