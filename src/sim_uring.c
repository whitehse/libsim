#include "sim_uring.h"

#include <stdlib.h>
#include <string.h>

#define SIM_URING_DEFAULT_SQ 64
#define SIM_URING_DEFAULT_CQ 128

/* Local negative results (not full errno table). */
#define SIM_URING_EAGAIN (-11)
#define SIM_URING_EINVAL (-22)
#define SIM_URING_ECANCELED (-125)

typedef struct {
    int used;
    sim_sqe_t sqe;
} pending_t;

struct sim_uring {
    sim_net_t *net;
    sim_clock_t *clk;

    sim_sqe_t *sq;
    size_t sq_cap;
    size_t sq_len; /* prepared not yet submitted */

    pending_t *pending;
    size_t pending_cap;
    size_t pending_count;

    sim_cqe_t *cq;
    size_t cq_cap;
    size_t cq_head;
    size_t cq_len;
};

sim_uring_config_t sim_uring_default_config(void)
{
    sim_uring_config_t c;
    memset(&c, 0, sizeof(c));
    c.sq_entries = SIM_URING_DEFAULT_SQ;
    c.cq_entries = SIM_URING_DEFAULT_CQ;
    c.max_pending = SIM_URING_DEFAULT_SQ;
    return c;
}

sim_uring_t *sim_uring_create(sim_net_t *net, sim_clock_t *clk)
{
    sim_uring_config_t c = sim_uring_default_config();
    return sim_uring_create_with_config(net, clk, &c);
}

sim_uring_t *sim_uring_create_with_config(sim_net_t *net, sim_clock_t *clk,
                                          const sim_uring_config_t *cfg)
{
    sim_uring_t *r;
    size_t sq, cq, pend;

    if (!net) {
        return NULL;
    }
    sq = (cfg && cfg->sq_entries) ? cfg->sq_entries : SIM_URING_DEFAULT_SQ;
    cq = (cfg && cfg->cq_entries) ? cfg->cq_entries : SIM_URING_DEFAULT_CQ;
    pend = (cfg && cfg->max_pending) ? cfg->max_pending : sq;
    if (sq == 0) {
        sq = SIM_URING_DEFAULT_SQ;
    }
    if (cq == 0) {
        cq = SIM_URING_DEFAULT_CQ;
    }
    if (pend == 0) {
        pend = sq;
    }

    r = (sim_uring_t *)calloc(1, sizeof(*r));
    if (!r) {
        return NULL;
    }
    r->sq = (sim_sqe_t *)calloc(sq, sizeof(sim_sqe_t));
    r->pending = (pending_t *)calloc(pend, sizeof(pending_t));
    r->cq = (sim_cqe_t *)calloc(cq, sizeof(sim_cqe_t));
    if (!r->sq || !r->pending || !r->cq) {
        free(r->sq);
        free(r->pending);
        free(r->cq);
        free(r);
        return NULL;
    }
    r->net = net;
    r->clk = clk;
    r->sq_cap = sq;
    r->pending_cap = pend;
    r->cq_cap = cq;
    return r;
}

void sim_uring_destroy(sim_uring_t *ring)
{
    if (!ring) {
        return;
    }
    free(ring->sq);
    free(ring->pending);
    free(ring->cq);
    free(ring);
}

sim_sqe_t *sim_uring_get_sqe(sim_uring_t *ring)
{
    sim_sqe_t *s;
    if (!ring || ring->sq_len >= ring->sq_cap) {
        return NULL;
    }
    s = &ring->sq[ring->sq_len];
    memset(s, 0, sizeof(*s));
    ring->sq_len++;
    return s;
}

void sim_sqe_prep_nop(sim_sqe_t *sqe)
{
    if (!sqe) {
        return;
    }
    memset(sqe, 0, sizeof(*sqe));
    sqe->op = SIM_URING_OP_NOP;
    sqe->prepared = 1;
}

void sim_sqe_prep_accept(sim_sqe_t *sqe, uint64_t listen_fd)
{
    if (!sqe) {
        return;
    }
    memset(sqe, 0, sizeof(*sqe));
    sqe->op = SIM_URING_OP_ACCEPT;
    sqe->fd = listen_fd;
    sqe->prepared = 1;
}

void sim_sqe_prep_recv(sim_sqe_t *sqe, uint64_t fd, void *buf, size_t len)
{
    if (!sqe) {
        return;
    }
    memset(sqe, 0, sizeof(*sqe));
    sqe->op = SIM_URING_OP_RECV;
    sqe->fd = fd;
    sqe->buf = buf;
    sqe->len = len;
    sqe->prepared = 1;
}

void sim_sqe_prep_send(sim_sqe_t *sqe, uint64_t fd, const void *buf, size_t len)
{
    if (!sqe) {
        return;
    }
    memset(sqe, 0, sizeof(*sqe));
    sqe->op = SIM_URING_OP_SEND;
    sqe->fd = fd;
    sqe->buf = (void *)buf;
    sqe->len = len;
    sqe->prepared = 1;
}

void sim_sqe_prep_poll_add(sim_sqe_t *sqe, uint64_t fd, unsigned events)
{
    if (!sqe) {
        return;
    }
    memset(sqe, 0, sizeof(*sqe));
    sqe->op = SIM_URING_OP_POLL_ADD;
    sqe->fd = fd;
    sqe->poll_events = events ? events : (SIM_POLL_IN | SIM_POLL_OUT);
    sqe->prepared = 1;
}

void sim_sqe_prep_timeout(sim_sqe_t *sqe, uint64_t delay_ns)
{
    if (!sqe) {
        return;
    }
    memset(sqe, 0, sizeof(*sqe));
    sqe->op = SIM_URING_OP_TIMEOUT;
    sqe->timeout_ns = delay_ns;
    sqe->timeout_is_abs = 0;
    sqe->prepared = 1;
}

void sim_sqe_set_user_data(sim_sqe_t *sqe, uint64_t user_data)
{
    if (sqe) {
        sqe->user_data = user_data;
    }
}

void sim_sqe_set_user_data64(sim_sqe_t *sqe, uint64_t user_data)
{
    sim_sqe_set_user_data(sqe, user_data);
}

static int cq_push(sim_uring_t *r, uint64_t user_data, int32_t res)
{
    size_t idx;
    if (r->cq_len >= r->cq_cap) {
        return -1;
    }
    idx = (r->cq_head + r->cq_len) % r->cq_cap;
    r->cq[idx].user_data = user_data;
    r->cq[idx].res = res;
    r->cq[idx].flags = 0;
    r->cq_len++;
    return 0;
}

/**
 * Try to complete one SQE. Returns:
 *  1  completed (CQE pushed)
 *  0  still pending (must park)
 * -1  hard error completed with negative res
 */
static int try_complete(sim_uring_t *r, sim_sqe_t *s)
{
    int n;
    uint64_t accepted;
    int rd, wr;

    switch (s->op) {
    case SIM_URING_OP_NOP:
        cq_push(r, s->user_data, 0);
        return 1;

    case SIM_URING_OP_ACCEPT:
        n = sim_net_accept(r->net, s->fd, &accepted);
        if (n == SIM_NET_EAGAIN) {
            return 0;
        }
        if (n != 0) {
            cq_push(r, s->user_data, SIM_URING_EINVAL);
            return 1;
        }
        /* res = accepted fd if it fits in int32; else user peeks peer via net */
        if (accepted > 0x7fffffffu) {
            cq_push(r, s->user_data, (int32_t)(accepted & 0x7fffffff));
        } else {
            cq_push(r, s->user_data, (int32_t)accepted);
        }
        return 1;

    case SIM_URING_OP_RECV:
        if (!s->buf && s->len > 0) {
            cq_push(r, s->user_data, SIM_URING_EINVAL);
            return 1;
        }
        n = sim_net_recv(r->net, s->fd, s->buf, s->len);
        if (n == SIM_NET_EAGAIN) {
            return 0;
        }
        if (n == SIM_NET_EINVAL) {
            cq_push(r, s->user_data, SIM_URING_EINVAL);
            return 1;
        }
        /* n == 0 EOF or >0 bytes */
        cq_push(r, s->user_data, n);
        return 1;

    case SIM_URING_OP_SEND:
        if (!s->buf && s->len > 0) {
            cq_push(r, s->user_data, SIM_URING_EINVAL);
            return 1;
        }
        n = sim_net_send(r->net, s->fd, s->buf, s->len);
        if (n == SIM_NET_EAGAIN) {
            return 0;
        }
        if (n == SIM_NET_EPIPE) {
            cq_push(r, s->user_data, -32); /* EPIPE-ish */
            return 1;
        }
        if (n == SIM_NET_EINVAL) {
            cq_push(r, s->user_data, SIM_URING_EINVAL);
            return 1;
        }
        cq_push(r, s->user_data, n);
        return 1;

    case SIM_URING_OP_POLL_ADD:
        rd = sim_net_readable(r->net, s->fd);
        wr = sim_net_writable(r->net, s->fd);
        if (rd < 0 && wr < 0) {
            cq_push(r, s->user_data, SIM_URING_EINVAL);
            return 1;
        }
        {
            unsigned ready = 0;
            if ((s->poll_events & SIM_POLL_IN) && rd == 1) {
                ready |= SIM_POLL_IN;
            }
            if ((s->poll_events & SIM_POLL_OUT) && wr == 1) {
                ready |= SIM_POLL_OUT;
            }
            if (ready) {
                cq_push(r, s->user_data, (int32_t)ready);
                return 1;
            }
        }
        return 0;

    case SIM_URING_OP_TIMEOUT:
        if (!r->clk) {
            cq_push(r, s->user_data, SIM_URING_EINVAL);
            return 1;
        }
        {
            uint64_t now = sim_clock_now_ns(r->clk);
            uint64_t deadline = s->timeout_ns;
            if (!s->timeout_is_abs) {
                /* convert relative → absolute once */
                if (s->timeout_ns > UINT64_MAX - now) {
                    deadline = UINT64_MAX;
                } else {
                    deadline = now + s->timeout_ns;
                }
                s->timeout_ns = deadline;
                s->timeout_is_abs = 1;
            }
            if (now >= deadline) {
                cq_push(r, s->user_data, -62); /* ETIME-ish */
                return 1;
            }
        }
        return 0;

    default:
        cq_push(r, s->user_data, SIM_URING_EINVAL);
        return 1;
    }
}

static int park_pending(sim_uring_t *r, const sim_sqe_t *s)
{
    size_t i;
    for (i = 0; i < r->pending_cap; i++) {
        if (!r->pending[i].used) {
            r->pending[i].used = 1;
            r->pending[i].sqe = *s;
            r->pending_count++;
            return 0;
        }
    }
    return -1;
}

int sim_uring_submit(sim_uring_t *ring)
{
    size_t i;
    int submitted;

    if (!ring) {
        return -1;
    }
    submitted = 0;
    for (i = 0; i < ring->sq_len; i++) {
        sim_sqe_t *s = &ring->sq[i];
        int rc;
        if (!s->prepared) {
            continue;
        }
        rc = try_complete(ring, s);
        if (rc == 0) {
            if (park_pending(ring, s) != 0) {
                /* pending full: complete as canceled */
                cq_push(ring, s->user_data, SIM_URING_ECANCELED);
            }
        }
        submitted++;
    }
    ring->sq_len = 0;
    memset(ring->sq, 0, ring->sq_cap * sizeof(sim_sqe_t));
    return submitted;
}

int sim_uring_progress(sim_uring_t *ring)
{
    size_t i;
    int done = 0;
    if (!ring) {
        return 0;
    }
    for (i = 0; i < ring->pending_cap; i++) {
        int rc;
        if (!ring->pending[i].used) {
            continue;
        }
        rc = try_complete(ring, &ring->pending[i].sqe);
        if (rc != 0) {
            ring->pending[i].used = 0;
            if (ring->pending_count > 0) {
                ring->pending_count--;
            }
            done++;
        }
    }
    return done;
}

int sim_uring_peek_cqe(sim_uring_t *ring, sim_cqe_t **cqe_out)
{
    if (!ring || !cqe_out) {
        return 0;
    }
    if (ring->cq_len == 0) {
        *cqe_out = NULL;
        return 0;
    }
    *cqe_out = &ring->cq[ring->cq_head];
    return 1;
}

void sim_uring_cqe_seen(sim_uring_t *ring, sim_cqe_t *cqe)
{
    if (!ring || !cqe || ring->cq_len == 0) {
        return;
    }
    if (cqe != &ring->cq[ring->cq_head]) {
        return; /* only head supported (like sequential seen) */
    }
    ring->cq_head = (ring->cq_head + 1) % ring->cq_cap;
    ring->cq_len--;
}

size_t sim_uring_cq_ready(const sim_uring_t *ring)
{
    return ring ? ring->cq_len : 0;
}

size_t sim_uring_pending(const sim_uring_t *ring)
{
    return ring ? ring->pending_count : 0;
}
