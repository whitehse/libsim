/**
 * @file sim_net.h
 * @brief In-memory stream pipes and listen/accept for class B/C hosts (P0.2).
 *
 * No syscalls. Byte streams between endpoints with fixed ring buffers.
 * Enough to drive agent/mobile socket-shaped tests without a kernel.
 */
#ifndef SIM_NET_H
#define SIM_NET_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sim_net sim_net_t;

typedef struct {
    /** Max concurrent endpoints (0 → 64). */
    size_t max_endpoints;
    /** Per-endpoint RX ring capacity in bytes (0 → 4096). */
    size_t buf_capacity;
    /** Max pending accepts per listener (0 → 16). */
    size_t accept_queue;
} sim_net_config_t;

typedef enum {
    SIM_NET_EVENT_NONE = 0,
    /** Endpoint has data to recv (or EOF pending with empty buffer). */
    SIM_NET_EVENT_READABLE = 1,
    /** Endpoint has TX space (peer RX not full). */
    SIM_NET_EVENT_WRITABLE = 2,
    /** Listener has a connection waiting for accept. */
    SIM_NET_EVENT_ACCEPT = 3,
    /** Peer closed; further recv returns 0 after buffer drain. */
    SIM_NET_EVENT_HUP = 4,
    /** Local error state (invalid use). */
    SIM_NET_EVENT_ERROR = 5
} sim_net_event_type_t;

typedef struct {
    sim_net_event_type_t type;
    uint64_t             fd;
    /** For ACCEPT: newly accepted endpoint id (also returned by sim_net_accept). */
    uint64_t             peer_fd;
    int                  err;
} sim_net_event_t;

/** Return codes for send/recv (negative). */
enum {
    SIM_NET_EAGAIN = -2,  /* would block */
    SIM_NET_EPIPE = -3,   /* peer closed / not connected */
    SIM_NET_EINVAL = -4,
    SIM_NET_ENOMEM = -5
};

sim_net_config_t sim_net_default_config(void);

sim_net_t *sim_net_create(void);
sim_net_t *sim_net_create_with_config(const sim_net_config_t *cfg);
void sim_net_destroy(sim_net_t *net);

/**
 * Create a connected bidirectional pipe.
 * Writes to *a appear on *b's recv and vice versa.
 * Returns 0 on success, SIM_NET_E* on failure.
 */
int sim_net_pipe(sim_net_t *net, uint64_t *out_a, uint64_t *out_b);

/** Create an unbound stream endpoint. Returns fd (>0) or 0 on failure. */
uint64_t sim_net_socket(sim_net_t *net);

/**
 * Mark endpoint as passive listener. Returns 0 on success.
 */
int sim_net_listen(sim_net_t *net, uint64_t fd);

/**
 * Connect client_fd to a listening fd. Completes immediately into the
 * listener accept queue (no handshake delay). Returns 0 on success.
 */
int sim_net_connect(sim_net_t *net, uint64_t client_fd, uint64_t listener_fd);

/**
 * Accept one pending connection on a listener.
 * Writes new connected endpoint id to *out_fd.
 * Returns 0 on success, SIM_NET_EAGAIN if none pending.
 */
int sim_net_accept(sim_net_t *net, uint64_t listener_fd, uint64_t *out_fd);

/**
 * Send up to len bytes. Returns bytes sent (>0), 0 if len==0,
 * or SIM_NET_EAGAIN / SIM_NET_EPIPE / SIM_NET_EINVAL.
 */
int sim_net_send(sim_net_t *net, uint64_t fd, const void *buf, size_t len);

/**
 * Recv up to len bytes. Returns bytes read (>0), 0 on EOF,
 * or SIM_NET_EAGAIN / SIM_NET_EINVAL.
 */
int sim_net_recv(sim_net_t *net, uint64_t fd, void *buf, size_t len);

/** Close endpoint; peer sees HUP/EOF after draining. Returns 0 or error. */
int sim_net_close(sim_net_t *net, uint64_t fd);

/** Level-triggered readiness helpers (1 = yes, 0 = no, -1 = bad fd). */
int sim_net_readable(const sim_net_t *net, uint64_t fd);
int sim_net_writable(const sim_net_t *net, uint64_t fd);

/**
 * Pull next queued edge event. Returns 1 if an event was written, 0 if empty.
 */
int sim_net_next_event(sim_net_t *net, sim_net_event_t *out);

/** Number of live endpoints. */
size_t sim_net_endpoint_count(const sim_net_t *net);

#ifdef __cplusplus
}
#endif

#endif /* SIM_NET_H */
