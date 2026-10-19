// SPDX-License-Identifier: MIT
#include "suitecut/client.h"
#include "suitecut/json.hpp"
#include "suitecut/transport.hpp"
#include <algorithm>
#include <cstring>
#include <exception>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>

struct sc_client { std::shared_ptr<suitecut::Transport> transport; };
struct sc_request {
  std::shared_ptr<suitecut::Transport> transport;
  suitecut::Request request;
  std::timed_mutex mutex;
  bool consumed = false;
  sc_request(std::shared_ptr<suitecut::Transport> owner, suitecut::Request value)
      : transport(std::move(owner)), request(std::move(value)) {}
};
struct sc_packet { suitecut::Packet packet; };
namespace {
sc_status result(sc_error *error, sc_status code, const char *message = "") noexcept {
  if (error) {
    error->code = code;
    const auto length = std::min(std::strlen(message), sizeof(error->message) - 1);
    std::memcpy(error->message, message, length);
    error->message[length] = 0;
  }
  return code;
}
template <class Fn> sc_status boundary(sc_error *error, Fn fn) noexcept {
  try { fn(); return result(error, SC_OK); }
  catch (const std::invalid_argument& e) { return result(error, SC_INVALID_ARGUMENT, e.what()); }
  catch (const std::exception& e) { return result(error, SC_ERROR, e.what()); }
  catch (...) { return result(error, SC_ERROR, "Unknown native client failure"); }
}
void check_client(sc_client *client) {
  if (!client) throw std::invalid_argument("Client is null");
}
std::string command(const char *json, size_t size) {
  // Engine command framer also has a finite limit. Reject before allocating.
  if (!json || !size || size > 65536)
    throw std::invalid_argument("Command is null, empty or exceeds the protocol line limit");
  return std::string(json, size);
}
}
extern "C" {
uint32_t sc_abi_version(void) { return SC_ABI_VERSION; }
void sc_launch_options_init(sc_launch_options *out) {
  if (!out) return;
  *out = {};
  out->struct_size = sizeof(*out); out->abi_version = SC_ABI_VERSION;
  out->width = 1280; out->height = 720; out->frames_per_second = 60; out->headless = 1;
  out->startup_timeout_ms = 30000; out->shutdown_timeout_ms = 5000;
  out->max_pending = 256; out->max_event_bytes = 64 * 1024 * 1024;
  out->max_payload_bytes = 192 * 1024 * 1024;
}
static sc_status launch_client(const sc_launch_options *options, sc_client **out, sc_error *error,
                               suitecut::BrowserBackend backend, const char *endpoint = nullptr,
                               const sc_connection_options *connection = nullptr) {
  if (out) *out = nullptr;
  return boundary(error, [&] {
    if (!out || !options || options->struct_size != sizeof(*options) ||
        options->abi_version != SC_ABI_VERSION || (!endpoint && !options->executable))
      throw std::invalid_argument("Invalid launch options or ABI version");
    if (options->headless != 0 && options->headless != 1)
      throw std::invalid_argument("headless must be zero or one");
    if (options->argument_count > 1024 || (options->argument_count && !options->arguments))
      throw std::invalid_argument("Invalid launch arguments");
    suitecut::LaunchOptions config;
    if (options->executable) config.executable = options->executable;
    if (endpoint) config.cdp_endpoint = endpoint;
    config.backend = backend;
    if(connection) {
      if(connection->struct_size!=sizeof(*connection) || connection->abi_version!=SC_ABI_VERSION)throw std::invalid_argument("Invalid connection options or ABI version");
      if(connection->tls_ca_file)config.tls_ca_file=connection->tls_ca_file;
      if(connection->tls_client_certificate)config.tls_client_certificate=connection->tls_client_certificate;
      if(connection->tls_client_key)config.tls_client_key=connection->tls_client_key;
      if(connection->headers_json) {
        const auto headers=suitecut::Json::parse(connection->headers_json,{65536,4,256,16384});
        for(const auto& [key,value]:headers.as_object())config.connect_headers.emplace(key,value.as_string());
      }
    }
    if (options->profile) config.profile = options->profile;
    config.width = options->width; config.height = options->height;
    config.frames_per_second = options->frames_per_second;
    config.headless = options->headless != 0;
    config.startup_timeout = std::chrono::milliseconds(options->startup_timeout_ms);
    config.shutdown_timeout = std::chrono::milliseconds(options->shutdown_timeout_ms);
    config.max_pending = options->max_pending;
    config.max_event_bytes = options->max_event_bytes;
    config.max_payload_bytes = options->max_payload_bytes;
    for (size_t i = 0; i < options->argument_count; ++i) {
      if (!options->arguments[i]) throw std::invalid_argument("Launch argument is null");
      config.arguments.emplace_back(options->arguments[i]);
    }
    auto client = std::make_unique<sc_client>();
    client->transport = suitecut::Transport::launch(config);
    *out = client.release();
  });
}
sc_status sc_client_launch(const sc_launch_options *options, sc_client **out, sc_error *error) {
  return launch_client(options, out, error, suitecut::BrowserBackend::cef);
}
sc_status sc_client_launch_chromium(const sc_launch_options *options, sc_client **out, sc_error *error) {
  return launch_client(options, out, error, suitecut::BrowserBackend::chromium);
}
sc_status sc_client_connect_chromium(const char *endpoint, const sc_launch_options *options,
                                     sc_client **out, sc_error *error) {
  if (!endpoint || !*endpoint) {
    if (out) *out = nullptr;
    return boundary(error, [] { throw std::invalid_argument("A Chromium CDP endpoint is required"); });
  }
  sc_launch_options defaults;
  if (!options) { sc_launch_options_init(&defaults); options = &defaults; }
  return launch_client(options, out, error, suitecut::BrowserBackend::chromium, endpoint);
}
void sc_connection_options_init(sc_connection_options *options) {
  if(!options)return;
  *options={}; options->struct_size=sizeof(*options); options->abi_version=SC_ABI_VERSION;
}
sc_status sc_client_launch_firefox(const sc_launch_options *options, sc_client **out, sc_error *error) {
  return launch_client(options,out,error,suitecut::BrowserBackend::firefox);
}
sc_status sc_client_connect_firefox(const char *endpoint, const sc_launch_options *options, sc_client **out, sc_error *error) {
  return sc_client_connect_remote(SC_REMOTE_FIREFOX,endpoint,options,nullptr,out,error);
}
sc_status sc_client_connect_remote(sc_remote_backend backend, const char *endpoint,
                                 const sc_launch_options *options, const sc_connection_options *connection,
                                 sc_client **out, sc_error *error) {
  if(!endpoint || !*endpoint || (backend!=SC_REMOTE_CHROMIUM && backend!=SC_REMOTE_FIREFOX)) {
    if(out)*out=nullptr;
    return boundary(error,[]{throw std::invalid_argument("Remote connection requires a supported backend and endpoint");});
  }
  sc_launch_options defaults;
  if(!options){sc_launch_options_init(&defaults);options=&defaults;}
  return launch_client(options,out,error,backend==SC_REMOTE_FIREFOX?suitecut::BrowserBackend::firefox:suitecut::BrowserBackend::chromium,endpoint,connection);
}
sc_status sc_client_close(sc_client *client, sc_error *error) {
  return boundary(error, [&] { check_client(client); client->transport->close(); });
}
void sc_client_destroy(sc_client *client) {
  if (!client) return;
  try { client->transport->close(); } catch (...) { /* C destruction never throws. */ }
  delete client;
}
sc_status sc_client_submit(sc_client *client, const char *json, size_t size,
                          uint32_t timeout, sc_request **out, sc_error *error) {
  if (out) *out = nullptr;
  return boundary(error, [&] {
    check_client(client);
    if (!out) throw std::invalid_argument("Request output is null");
    // Allocate the public handle before accepting any remote work.
    auto handle = std::make_unique<sc_request>(client->transport, suitecut::Request{});
    handle->request = client->transport->submit(command(json, size), std::chrono::milliseconds(timeout));
    *out = handle.release();
  });
}
sc_status sc_client_call(sc_client *client, const char *json, size_t size,
                        uint32_t timeout, sc_packet **out, sc_error *error) {
  if (out) *out = nullptr;
  return boundary(error, [&] {
    check_client(client);
    if (!out) throw std::invalid_argument("Packet output is null");
    auto packet = std::make_unique<sc_packet>();
    packet->packet = client->transport->call(command(json, size), std::chrono::milliseconds(timeout));
    *out = packet.release();
  });
}
int sc_request_id(const sc_request *request) { return request ? request->request.id : 0; }
sc_status sc_request_wait(sc_request *request, uint32_t timeout, sc_packet **out, sc_error *error) {
  if (out) *out = nullptr;
  if (!request || !out) return result(error, SC_INVALID_ARGUMENT, "Request or packet output is null");
  try {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
    std::unique_lock lock(request->mutex, std::defer_lock);
    if (!lock.try_lock_until(deadline))
      return result(error, SC_TIMEOUT, "Request has an active waiter");
    if (request->consumed) return result(error, SC_ALREADY_CONSUMED, "Request result was consumed");
    if (request->request.result.wait_until(deadline) != std::future_status::ready)
      return result(error, SC_TIMEOUT, "Request is pending");
    auto packet = std::make_unique<sc_packet>();
    request->consumed = true;
    packet->packet = request->request.result.get();
    *out = packet.release();
    return result(error, SC_OK);
  } catch (const std::exception& e) { return result(error, SC_ERROR, e.what()); }
  catch (...) { return result(error, SC_ERROR, "Unknown request failure"); }
}
sc_status sc_request_cancel(sc_request *request, sc_error *error) {
  return boundary(error, [&] {
    if (!request) throw std::invalid_argument("Request is null");
    request->transport->cancel(request->request.id);
  });
}
void sc_request_destroy(sc_request *request) { delete request; }
sc_status sc_client_poll(sc_client *client, uint32_t timeout, sc_packet **out, sc_error *error) {
  if (out) *out = nullptr;
  sc_status status = SC_OK;
  const auto checked = boundary(error, [&] {
    check_client(client);
    if (!out) throw std::invalid_argument("Packet output is null");
    auto packet = std::make_unique<sc_packet>();
    auto event = client->transport->poll(std::chrono::milliseconds(timeout));
    if (!event) { status = SC_TIMEOUT; return; }
    packet->packet = std::move(*event);
    *out = packet.release();
  });
  return checked == SC_OK ? result(error, status, status == SC_TIMEOUT ? "No event available" : "") : checked;
}
sc_status sc_client_ready(sc_client *client, sc_packet **out, sc_error *error) {
  if (out) *out = nullptr;
  return boundary(error, [&] {
    check_client(client);
    if (!out) throw std::invalid_argument("Packet output is null");
    auto packet = std::make_unique<sc_packet>();
    packet->packet = client->transport->ready();
    *out = packet.release();
  });
}
sc_status sc_client_stats(sc_client *client, sc_stats *out, sc_error *error) {
  return boundary(error, [&] {
    check_client(client);
    if (!out) throw std::invalid_argument("Stats output is null");
    const auto stats = client->transport->stats();
    *out = {stats.commands, stats.responses, stats.dropped_events, stats.pending, stats.queued_event_bytes};
  });
}
const char *sc_packet_header(const sc_packet *packet, size_t *size) {
  if (size) *size = packet ? packet->packet.header.size() : 0;
  return packet ? packet->packet.header.c_str() : nullptr;
}
const uint8_t *sc_packet_payload(const sc_packet *packet, size_t *size) {
  if (size) *size = packet ? packet->packet.payload.size() : 0;
  return packet && !packet->packet.payload.empty() ? packet->packet.payload.data() : nullptr;
}
void sc_packet_destroy(sc_packet *packet) { delete packet; }
}
