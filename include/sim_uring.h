/**
 * @file sim_uring.h
 * @brief Class-A io_uring-shaped submission/completion sim (P0.3).
 *
 * No syscalls and no liburing dependency. Operates on sim_net endpoints and
 * optional sim_clock timeouts so edgehost fuzz hosts can share one code path
 * shape with production io_uring (get_sqe → prep → submit → cqe).
 *
 * Class B/C hosts should not require this module (compile with LIBSIM_NO_URING
 * if desired).
 */
#ifndef SIM_URING_H
#define SIM_URING_H

#include "sim_clock.h"
#include "sim_net.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sim_uring sim_uring_t;
typedef struct sim_sqe sim_sqe_t;
typedef struct sim_cqe sim_cqe_t;

/** Poll event mask (subset of POLLIN/POLLOUT). */
enum {
    SIM_POLL_IN = 1,
    SIM_POLL_OUT = 2
};

typedef enum {
    SIM_URING_OP_NOP = 0,
    SIM_URING_OP_ACCEPT = 1,
    SIM_URING_OP_RECV = 2,
    SIM_URING_OP_SEND = 3,
    SIM_URING_OP_POLL_ADD = 4,
    SIM_URING_OP_TIMEOUT = 5
} sim_uring_op_t;

struct sim_sqe {
    sim_uring_op_t op;
    uint64_t       fd;
    uint64_t       user_data;
    void          *buf;
    size_t         len;
    unsigned       poll_events;
    uint64_t       timeout_ns; /* absolute deadline when using clock; or delay */
    int            timeout_is_abs;
    int            prepared;
};

struct sim_cqe {
    uint64_t user_data;
    int32_t  res; /* bytes, accepted fd as int if small, or -errno-style */
    uint32_t flags;
};

typedef struct {
    /** SQ depth (0 → 64). */
    size_t sq_entries;
    /** CQ depth (0 → 128). */
    size_t cq_entries;
    /** Max in-flight deferred ops (0 → same as sq_entries). */
    size_t max_pending;
} sim_uring_config_t;

sim_uring_config_t sim_uring_default_config(void);

/**
 * Create a ring bound to a sim_net (required) and optional clock (for TIMEOUT).
 * Neither net nor clock is owned.
 */
sim_uring_t *sim_uring_create(sim_net_t *net, sim_clock_t *clk);
sim_uring_t *sim_uring_create_with_config(sim_net_t *net, sim_clock_t *clk,
                                          const sim_uring_config_t *cfg);
void sim_uring_destroy(sim_uring_t *ring);

/** Get next free SQE, or NULL if SQ full. */
sim_sqe_t *sim_uring_get_sqe(sim_uring_t *ring);

void sim_sqe_prep_nop(sim_sqe_t *sqe);
void sim_sqe_prep_accept(sim_sqe_t *sqe, uint64_t listen_fd);
void sim_sqe_prep_recv(sim_sqe_t *sqe, uint64_t fd, void *buf, size_t len);
void sim_sqe_prep_send(sim_sqe_t *sqe, uint64_t fd, const void *buf, size_t len);
void sim_sqe_prep_poll_add(sim_sqe_t *sqe, uint64_t fd, unsigned events);
/** Relative timeout from current clock (requires clock). */
void sim_sqe_prep_timeout(sim_sqe_t *sqe, uint64_t delay_ns);
void sim_sqe_set_user_data(sim_sqe_t *sqe, uint64_t user_data);
void sim_sqe_set_user_data64(sim_sqe_t *sqe, uint64_t user_data); /* alias */

/**
 * Submit prepared SQEs. Completes immediately when possible; otherwise
 * parks as pending until sim_uring_progress().
 * Returns number of SQEs submitted (>=0) or -1 on error.
 */
int sim_uring_submit(sim_uring_t *ring);

/**
 * Retry deferred ops after sim_net I/O or sim_clock_advance.
 * Returns number of newly completed CQEs this call.
 */
int sim_uring_progress(sim_uring_t *ring);

/** Peek next CQE without consuming. Returns 1 if present. */
int sim_uring_peek_cqe(sim_uring_t *ring, sim_cqe_t **cqe_out);

/** Consume one CQE previously returned by peek. */
void sim_uring_cqe_seen(sim_uring_t *ring, sim_cqe_t *cqe);

/** Number of CQEs waiting. */
size_t sim_uring_cq_ready(const sim_uring_t *ring);

/** Number of deferred (not yet completed) ops. */
size_t sim_uring_pending(const sim_uring_t *ring);

#ifdef __cplusplus
}
#endif

#endif /* SIM_URING_H */
