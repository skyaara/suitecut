/* SPDX-License-Identifier: MIT */
/* Embedding SuiteCut: an app shows and drives tabs of a shared SuiteCut
 * session in its own window. The library speaks protocol 1 of
 * suitecut-session over its private Unix socket; it never runs an engine in
 * the app's process.
 *
 * Conventions are client.h's: every options struct starts with struct_size
 * and abi_version (SC_EMBED_ABI_VERSION here), handles are opaque, every
 * call returns sc_status and fills sc_error, close is idempotent, destroy
 * needs no call in flight on that handle, and the library never calls the
 * app back from its own threads. Every function may be called from any
 * thread and blocks for at most its timeout. client.h and SC_ABI_VERSION are
 * unchanged. */
#ifndef SUITECUT_EMBED_H
#define SUITECUT_EMBED_H
#include "suitecut/client.h"
#ifdef __cplusplus
extern "C" {
#endif

#define SC_EMBED_ABI_VERSION 1u
/* Returned only by embed.h functions. A refusal's message starts with the
 * protocol's error code and a colon: "human_in_control: The person ...". */
#define SC_REFUSED ((sc_status)5)
#define SC_UNSUPPORTED ((sc_status)6)
#define SC_CLOSED ((sc_status)7)

typedef struct sc_session sc_session;
typedef struct sc_view sc_view;
typedef struct sc_event sc_event;
typedef struct sc_json sc_json; /* an owned UTF-8 JSON document */

SC_API uint32_t sc_embed_abi_version(void);
/* Copies the protocol code of an SC_REFUSED error, NUL-terminated, into
 * |code| and returns its length; 0 when the error is not a refusal. */
SC_API size_t sc_error_refusal(const sc_error *error, char *code, size_t code_size);
SC_API const char *sc_json_data(const sc_json *json, size_t *size);
SC_API void sc_json_destroy(sc_json *json);

/* ---- Runtime: find and check a runtime before using it. ----
 * |path| is a SuiteCut.app bundle (Contents/MacOS holds the programs) or a
 * directory holding suitecut-browser and suitecut-session. On macOS a
 * signing requirement is checked against the whole bundle, nested code
 * included, before anything in it runs. The service is then asked for its
 * version and protocols (suitecut-session --version --json); no engine
 * starts. A service too old to answer speaks protocol 0 only. */
typedef struct sc_runtime_options {
  size_t struct_size;
  uint32_t abi_version;
  const char *path;                /* required */
  const char *signing_requirement; /* macOS code requirement; NULL skips the check (development only) */
  uint32_t protocol_min, protocol_max; /* what the app speaks; the defaults are 1 and 1 */
} sc_runtime_options;
typedef struct sc_runtime_info {
  size_t struct_size;
  uint32_t abi_version;
  char service_version[64];
  uint32_t protocol_min, protocol_max; /* what the service speaks */
  uint32_t protocol;                   /* the highest protocol both speak */
  uint32_t signature_verified;         /* 1 only when signing_requirement was checked and met */
  char engine_path[1024], service_path[1024];
} sc_runtime_info;
SC_API void sc_runtime_options_init(sc_runtime_options *options);
SC_API void sc_runtime_info_init(sc_runtime_info *info);
/* SC_UNSUPPORTED when the protocol ranges do not overlap; SC_ERROR when the
 * signature or the layout is wrong. */
SC_API sc_status sc_runtime_inspect(const sc_runtime_options *options, sc_runtime_info *out, sc_error *error);

/* ---- Session: start or join one as an owner. ---- */
typedef enum sc_session_role { SC_ROLE_HOST = 1, SC_ROLE_AGENT = 2 } sc_session_role;
#define SC_SESSION_START 1u /* start the service when it is not running */
typedef struct sc_session_options {
  size_t struct_size;
  uint32_t abi_version;
  const sc_runtime_info *runtime; /* required with SC_SESSION_START */
  const char *name;               /* session name: letters, digits, '.', '_' and '-' */
  const char *directory;          /* absolute private directory for the session's socket. The socket
                                   * path, directory + "/" + name + ".sock", must fit AF_UNIX: at most
                                   * 103 bytes on macOS and 107 on Linux, or SC_INVALID_ARGUMENT */
  const char *profile;            /* absolute; used only when starting */
  const char *owner;              /* letters, digits, '.', '_', ':' and '-' */
  sc_session_role role;           /* default SC_ROLE_HOST */
  const char *grant;              /* an agent's grant from its host; NULL when none */
  const char *client_name, *client_version;
  uint32_t scale;                 /* frame pixels per CSS pixel when starting; 0 keeps the service default */
  uint32_t idle_ms;               /* idle time before the service stops, when starting; 0 keeps its default */
  uint32_t timeout_ms;            /* per request; 0 means 30000 */
  uint32_t flags;
} sc_session_options;
SC_API void sc_session_options_init(sc_session_options *options);
SC_API sc_status sc_session_open(const sc_session_options *options, sc_session **out, sc_error *error);
/* The service's hello reply: protocol, service, runtime, engine pid, role,
 * tool groups and capabilities. */
SC_API sc_status sc_session_describe(sc_session *session, sc_json **out, sc_error *error);
/* Host role. A grant lets an agent join as this owner with the tool groups
 * in |tools_csv| ("core,debug"; NULL grants "core"); ttl_ms 0 never expires.
 * The token is NUL-terminated; 65 bytes hold it. */
SC_API sc_status sc_session_grant(sc_session *session, const char *tools_csv, uint32_t ttl_ms, char *token,
                                  size_t token_size, sc_error *error);
SC_API sc_status sc_session_revoke(sc_session *session, const char *token, sc_error *error);
/* Closes this handle's connections. shutdown != 0 first asks the service to
 * close every owner's tabs and stop (host role only). Idempotent; calls on a
 * closed session return SC_CLOSED. */
SC_API sc_status sc_session_close(sc_session *session, int shutdown, sc_error *error);
SC_API void sc_session_destroy(sc_session *session);

/* ---- Tabs: only this owner's are visible. ---- */
typedef enum sc_control { SC_CONTROL_UNCLAIMED = 0, SC_CONTROL_AGENT = 1, SC_CONTROL_HUMAN = 2 } sc_control;
typedef enum sc_history { SC_HISTORY_BACK = 1, SC_HISTORY_FORWARD = 2, SC_HISTORY_RELOAD = 3 } sc_history;
typedef struct sc_tab {
  size_t struct_size;
  uint32_t abi_version;
  int32_t page;
  sc_control control;
  uint64_t generation; /* grows each time control changes hands */
} sc_tab;
SC_API void sc_tab_init(sc_tab *tab);
/* {"tabs":[{page, control, generation, url, title}], "cursor"} */
SC_API sc_status sc_tab_list(sc_session *session, sc_json **out, sc_error *error);
/* |url| NULL opens a blank tab. A host's tab opens unclaimed unless
 * for_agent != 0; an agent's opens held by the agent. */
SC_API sc_status sc_tab_open(sc_session *session, const char *url, int for_agent, sc_tab *out, sc_error *error);
SC_API sc_status sc_tab_close(sc_session *session, int32_t page, sc_error *error);
/* Host role. Navigating takes the tab for the person. */
SC_API sc_status sc_tab_navigate(sc_session *session, int32_t page, const char *url, sc_tab *out, sc_error *error);
SC_API sc_status sc_tab_history(sc_session *session, int32_t page, sc_history step, sc_tab *out, sc_error *error);
SC_API sc_status sc_tab_take(sc_session *session, int32_t page, sc_tab *out, sc_error *error);
SC_API sc_status sc_tab_release(sc_session *session, int32_t page, sc_tab *out, sc_error *error);
/* Writes a PNG of the tab to the absolute |path|. Does not take the tab. */
SC_API sc_status sc_tab_screenshot(sc_session *session, int32_t page, const char *path, sc_error *error);

/* ---- Views: a tab's frames, delivered to the app. ---- */
typedef enum sc_sink_kind {
  /* The library receives and checks the frames; read them with sc_view_next_frame. macOS. */
  SC_SINK_LIBRARY = 1,
  /* macOS: a bootstrap name the app checked a receive right in under itself,
   * receiving the frame messages directly. */
  SC_SINK_MACH_BOOTSTRAP = 2
} sc_sink_kind;
typedef struct sc_surface_sink {
  size_t struct_size;
  uint32_t abi_version;
  sc_sink_kind kind;
  const char *name; /* SC_SINK_MACH_BOOTSTRAP */
} sc_surface_sink;
typedef struct sc_view_options {
  size_t struct_size;
  uint32_t abi_version;
  int32_t page;
  const sc_surface_sink *sink; /* NULL: a view for input and resizing only, with no frames */
  int css_width, css_height;   /* both 0 keep the tab's size */
  int replace;               /* take the tab's view over from another viewer */
} sc_view_options;
#define SC_PIXEL_NV12_FULL_601 0x34323066u /* '420f': bi-planar Y'CbCr 4:2:0, full range, BT.601 */
typedef struct sc_frame {
  size_t struct_size;
  uint32_t abi_version;
  uint32_t width, height; /* device pixels: CSS size times the session's scale, rounded down to even */
  uint32_t pixel_format;  /* SC_PIXEL_NV12_FULL_601 */
  uint64_t timestamp_us;  /* the engine's mach_absolute_time, in microseconds */
  uint64_t received;      /* frames this view has received, the ones skipped for newer included */
  /* macOS: an IOSurfaceRef this frame owns, with its use count raised so the
   * engine does not draw into it, until sc_frame_release. To keep it longer,
   * CFRetain it and raise its use count yourself. */
  void *surface;
} sc_frame;
SC_API void sc_surface_sink_init(sc_surface_sink *sink);
SC_API void sc_view_options_init(sc_view_options *options);
SC_API void sc_frame_init(sc_frame *frame);
/* Host role. SC_UNSUPPORTED where the sink kind does not exist. */
SC_API sc_status sc_view_attach(sc_session *session, const sc_view_options *options, sc_view **out, sc_error *error);
SC_API sc_status sc_view_resize(sc_view *view, int css_width, int css_height, sc_error *error);
/* SC_SINK_LIBRARY only. Returns the newest frame; older waiting ones were
 * already released. SC_TIMEOUT when none arrived. Frames from any process
 * other than the engine are discarded. */
SC_API sc_status sc_view_next_frame(sc_view *view, uint32_t wait_timeout_ms, sc_frame *out, sc_error *error);
SC_API void sc_frame_release(sc_frame *frame);
SC_API sc_status sc_view_detach(sc_view *view, sc_error *error);
SC_API void sc_view_destroy(sc_view *view);

/* ---- Input to a view's tab, in CSS pixels. ----
 * A press, a key or text takes the tab for the person; moving and
 * scrolling do not. Modifiers come from the keys the app holds down with
 * sc_view_key ("Shift", "Control", "Alt", "Meta"). */
typedef enum sc_pointer_action { SC_POINTER_MOVE = 1, SC_POINTER_DOWN = 2, SC_POINTER_UP = 3 } sc_pointer_action;
typedef enum sc_button { SC_BUTTON_LEFT = 1, SC_BUTTON_RIGHT = 2, SC_BUTTON_MIDDLE = 3 } sc_button;
typedef struct sc_pointer_event {
  size_t struct_size;
  uint32_t abi_version;
  sc_pointer_action action;
  double x, y;
  sc_button button;     /* down and up; default left */
  uint32_t click_count; /* down and up; default 1 */
} sc_pointer_event;
typedef struct sc_wheel_event {
  size_t struct_size;
  uint32_t abi_version;
  double x, y, delta_x, delta_y;
} sc_wheel_event;
typedef struct sc_key_event {
  size_t struct_size;
  uint32_t abi_version;
  int down;        /* 1 pressed, 0 released */
  const char *key; /* a DOM key value: "Enter", "ArrowLeft", "Shift", "a" */
} sc_key_event;
SC_API void sc_pointer_event_init(sc_pointer_event *event);
SC_API void sc_wheel_event_init(sc_wheel_event *event);
SC_API void sc_key_event_init(sc_key_event *event);
/* Moves sent while another input of this view is on its way are coalesced:
 * only the newest waiting move is sent. */
SC_API sc_status sc_view_pointer(sc_view *view, const sc_pointer_event *event, sc_error *error);
SC_API sc_status sc_view_wheel(sc_view *view, const sc_wheel_event *event, sc_error *error);
SC_API sc_status sc_view_key(sc_view *view, const sc_key_event *event, sc_error *error);
/* Inserts typed text at the focus. */
SC_API sc_status sc_view_text(sc_view *view, const char *utf8, size_t size, sc_error *error);

/* ---- Agent calls ---- */
/* {"tools":[{name, description, inputSchema}]} for this connection's grant. */
SC_API sc_status sc_agent_catalog(sc_session *session, sc_json **out, sc_error *error);
/* Claims an unclaimed tab for the agent before the call. expect_generation
 * 0 skips the generation check; timeout_ms 0 uses the session's.
 * {"result", "generation", "controlLost"?} */
SC_API sc_status sc_agent_call(sc_session *session, int32_t page, const char *tool, const char *arguments_json,
                               size_t arguments_size, uint64_t expect_generation, uint32_t timeout_ms,
                               sc_json **out, sc_error *error);

/* ---- Events for this owner, from the session's cursor ---- */
typedef enum sc_event_kind {
  SC_EVENT_OTHER = 0,         /* a kind this library does not name; read its JSON */
  SC_EVENT_TAB_CHANGED = 1,   /* {"tab":{...}} */
  SC_EVENT_TAB_CLOSED = 2,    /* {"page"} */
  SC_EVENT_RESYNC = 3,        /* events were missed: list the tabs again */
  SC_EVENT_ENGINE_EXITED = 4  /* the session's engine is gone; open the session again */
} sc_event_kind;
/* Long-polls; SC_TIMEOUT when nothing happened. The first event a session
 * returns is SC_EVENT_RESYNC. */
SC_API sc_status sc_session_next_event(sc_session *session, uint32_t wait_timeout_ms, sc_event **out,
                                       sc_error *error);
SC_API sc_event_kind sc_event_kind_of(const sc_event *event);
/* The event's page, or 0. */
SC_API int32_t sc_event_page(const sc_event *event);
SC_API const char *sc_event_json(const sc_event *event, size_t *size);
SC_API void sc_event_destroy(sc_event *event);

#ifdef __cplusplus
}
#endif
#endif
