/* SPDX-License-Identifier: MIT */
#ifndef SUITECUT_CLIENT_H
#define SUITECUT_CLIENT_H
#include <stddef.h>
#include <stdint.h>
#if defined(_WIN32) && defined(SUITECUT_CLIENT_SHARED)
# ifdef SUITECUT_CLIENT_BUILD
#  define SC_API __declspec(dllexport)
# else
#  define SC_API __declspec(dllimport)
# endif
#else
# define SC_API
#endif
#ifdef __cplusplus
extern "C" {
#endif

#define SC_ABI_VERSION 1u
typedef struct sc_client sc_client;
typedef struct sc_request sc_request;
typedef struct sc_packet sc_packet;
typedef enum sc_status {
  SC_OK = 0, SC_INVALID_ARGUMENT = 1, SC_TIMEOUT = 2,
  SC_ERROR = 3, SC_ALREADY_CONSUMED = 4
} sc_status;
typedef struct sc_error { sc_status code; char message[512]; } sc_error;
typedef struct sc_launch_options {
  size_t struct_size;
  uint32_t abi_version;
  const char *executable;
  const char *profile; /* NULL creates a temporary profile owned by the client. */
  int width, height;
  double frames_per_second;
  int headless;
  uint32_t startup_timeout_ms, shutdown_timeout_ms;
  size_t max_pending, max_event_bytes, max_payload_bytes;
  const char *const *arguments;
  size_t argument_count;
} sc_launch_options;
typedef struct sc_stats {
  uint64_t commands, responses, dropped_events;
  size_t pending, queued_event_bytes;
} sc_stats;

/* Supplemental connection options leave the ABI-v1 launch layout unchanged.
 * TLS always verifies trust and hostname/IP; headers_json is an object of strings. */
typedef enum sc_remote_backend
#ifdef __cplusplus
  : int
#endif
{ SC_REMOTE_CHROMIUM = 1, SC_REMOTE_FIREFOX = 2 } sc_remote_backend;
typedef struct sc_connection_options {
  size_t struct_size;
  uint32_t abi_version;
  const char *headers_json;
  const char *tls_ca_file;
  const char *tls_client_certificate;
  const char *tls_client_key;
} sc_connection_options;
SC_API void sc_connection_options_init(sc_connection_options *options);
SC_API uint32_t sc_abi_version(void);
SC_API void sc_launch_options_init(sc_launch_options *options);
SC_API sc_status sc_client_launch(const sc_launch_options *options, sc_client **out, sc_error *error);
/* Same ABI-v1 options, launching an installed Chrome/Chromium via native CDP. */
SC_API sc_status sc_client_launch_chromium(const sc_launch_options *options, sc_client **out, sc_error *error);
SC_API sc_status sc_client_launch_firefox(const sc_launch_options *options, sc_client **out, sc_error *error);
SC_API sc_status sc_client_connect_firefox(const char *endpoint, const sc_launch_options *options,
                                          sc_client **out, sc_error *error);
SC_API sc_status sc_client_connect_remote(sc_remote_backend backend, const char *endpoint,
                                         const sc_launch_options *options, const sc_connection_options *connection,
                                         sc_client **out, sc_error *error);
/* Attach to caller-owned Chrome using a CDP port or ws(s):// URL.
 * options may be NULL for defaults. Closing detaches without killing Chrome. */
SC_API sc_status sc_client_connect_chromium(const char *endpoint, const sc_launch_options *options,
                                          sc_client **out, sc_error *error);
/* close is idempotent; destroy requires no other calls on this handle in flight.
 * Requests and packets may outlive their client handle. No callback is invoked.
 * Other calls may run concurrently; each request result is consumed once. */
SC_API sc_status sc_client_close(sc_client *client, sc_error *error);
SC_API void sc_client_destroy(sc_client *client);
/* JSON object containing command/pageId/fields. The client supplies version/id.
 * json_size excludes the trailing NUL; no NUL termination is required.
 * timeout_ms is the command deadline; wait timeout does not cancel a command.
 * Responses preserve engine ok/error in the raw header; SC_OK means delivery. */
SC_API sc_status sc_client_submit(sc_client *client, const char *json, size_t json_size,
                                 uint32_t timeout_ms, sc_request **out, sc_error *error);
SC_API sc_status sc_client_call(sc_client *client, const char *json, size_t json_size,
                               uint32_t timeout_ms, sc_packet **out, sc_error *error);
SC_API int sc_request_id(const sc_request *request);
SC_API sc_status sc_request_wait(sc_request *request, uint32_t wait_timeout_ms,
                                sc_packet **out, sc_error *error);
SC_API sc_status sc_request_cancel(sc_request *request, sc_error *error);
/* Destroy releases local state only. Cancel explicitly to stop remote work. */
SC_API void sc_request_destroy(sc_request *request);
SC_API sc_status sc_client_poll(sc_client *client, uint32_t wait_timeout_ms,
                               sc_packet **out, sc_error *error);
SC_API sc_status sc_client_ready(sc_client *client, sc_packet **out, sc_error *error);
SC_API sc_status sc_client_stats(sc_client *client, sc_stats *out, sc_error *error);
SC_API const char *sc_packet_header(const sc_packet *packet, size_t *size);
SC_API const uint8_t *sc_packet_payload(const sc_packet *packet, size_t *size);
SC_API void sc_packet_destroy(sc_packet *packet);

#ifdef __cplusplus
}
#endif
#endif
