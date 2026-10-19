// SPDX-License-Identifier: MIT
// Existing native input work shared by Source and included-source controls.
  struct InputWork {
    const suitecut::NativeWorkOrigin native_origin = suitecut::CurrentNativeWorkOrigin();
    int owner = 0; uint64_t generation = 0;
    Dict message, keyboard_target, pointer_target; std::string kind, action, key, text;
    bool pointer_verified = false;
    bool keyboard_target_prepared = false;
    bool widget_completing=false,widget_complete_done=false,widget_completion_ok=false;
    std::string widget_completion_error,widget_completion_json;
    int phase = 0, step = 1, count = 1;
    size_t cursor = 0, character_bytes = 0;
    double start_x = 0, start_y = 0, due = 0;
    bool busy = false, held_mouse = false, final_release_submitted = false;
    std::vector<std::string> chord, held_keys;
    bool manual_prepared = false;
    bool standards_send_keys = false, send_keys_auto_shift = false, send_keys_cleanup_failed = false;
    SuiteCutSendKeysToken send_keys_token;
    std::vector<SuiteCutKey> send_keys_held;
  };
