#include "sim_net.h"

#include <stdlib.h>
#include <string.h>

#define SIM_NET_DEFAULT_MAX_EP 64
#define SIM_NET_DEFAULT_BUF 4096
#define SIM_NET_DEFAULT_ACCEPTQ 16
#define SIM_NET_EVENT_Q 128

typedef enum {
    EP_FREE = 0,
    EP_STREAM = 1,
    EP_LISTENER = 2
} ep_kind_t;

typedef struct {
    uint8_t *data;
    size_t cap;
    size_t head;
    size_t len;
} ring_t;

typedef struct {
    int used;
    ep_kind_t kind;
    int local_closed;
    int peer_closed; /* peer called close */
    uint64_t id;
    uint64_t peer_id; /* for STREAM */
    ring_t rx;
    /* listener only */
    uint64_t *accept_q;
    size_t accept_cap;
    size_t accept_len;
} endpoint_t;

struct sim_net {
    endpoint_t *eps;
    size_t max_ep;
    size_t buf_cap;
    size_t accept_cap;
    size_t live;
    uint64_t next_id;

    sim_net_event_t *evq;
    size_t evq_cap;
    size_t evq_head;
    size_t evq_len;
};

/* ── ring ─────────────────────────────────────────────────────────── */

static int ring_init(ring_t *r, size_t cap)
{
    r->data = (uint8_t *)malloc(cap ? cap : 1);
    if (!r->data) {
        return -1;
    }
    r->cap = cap;
    r->head = 0;
    r->len = 0;
    return 0;
}

static void ring_free(ring_t *r)
{
    free(r->data);
    memset(r, 0, sizeof(*r));
}

static size_t ring_space(const ring_t *r)
{
    return r->cap - r->len;
}

static size_t ring_write(ring_t *r, const uint8_t *src, size_t n)
{
    size_t i;
    size_t w = n;
    if (w > ring_space(r)) {
        w = ring_space(r);
    }
    for (i = 0; i < w; i++) {
        size_t idx = (r->head + r->len) % r->cap;
        r->data[idx] = src[i];
        r->len++;
    }
    return w;
}

static size_t ring_read(ring_t *r, uint8_t *dst, size_t n)
{
    size_t i;
    size_t rd = n;
    if (rd > r->len) {
        rd = r->len;
    }
    for (i = 0; i < rd; i++) {
        dst[i] = r->data[r->head];
        r->head = (r->head + 1) % r->cap;
        r->len--;
    }
    return rd;
}

/* ── events ───────────────────────────────────────────────────────── */

static void push_event(sim_net_t *net, sim_net_event_type_t type, uint64_t fd,
                       uint64_t peer_fd, int err)
{
    sim_net_event_t *e;
    size_t idx;
    if (net->evq_len >= net->evq_cap) {
        return; /* drop if host is not draining (still level-queryable) */
    }
    idx = (net->evq_head + net->evq_len) % net->evq_cap;
    e = &net->evq[idx];
    e->type = type;
    e->fd = fd;
    e->peer_fd = peer_fd;
    e->err = err;
    net->evq_len++;
}

/* ── endpoints ────────────────────────────────────────────────────── */

static endpoint_t *find_ep(sim_net_t *net, uint64_t id)
{
    size_t i;
    if (!net || id == 0) {
        return NULL;
    }
    for (i = 0; i < net->max_ep; i++) {
        if (net->eps[i].used && net->eps[i].id == id) {
            return &net->eps[i];
        }
    }
    return NULL;
}

static const endpoint_t *find_ep_c(const sim_net_t *net, uint64_t id)
{
    return find_ep((sim_net_t *)net, id);
}

static endpoint_t *alloc_ep(sim_net_t *net)
{
    size_t i;
    for (i = 0; i < net->max_ep; i++) {
        if (!net->eps[i].used) {
            return &net->eps[i];
        }
    }
    return NULL;
}

static uint64_t next_id(sim_net_t *net)
{
    uint64_t id = net->next_id++;
    if (id == 0) {
        id = net->next_id++;
    }
    return id;
}

static int init_stream_ep(sim_net_t *net, endpoint_t *ep)
{
    memset(ep, 0, sizeof(*ep));
    ep->used = 1;
    ep->kind = EP_STREAM;
    ep->id = next_id(net);
    if (ring_init(&ep->rx, net->buf_cap) != 0) {
        memset(ep, 0, sizeof(*ep));
        return -1;
    }
    net->live++;
    return 0;
}

static void free_ep(sim_net_t *net, endpoint_t *ep)
{
    if (!ep->used) {
        return;
    }
    ring_free(&ep->rx);
    free(ep->accept_q);
    memset(ep, 0, sizeof(*ep));
    if (net->live > 0) {
        net->live--;
    }
}

/* ── public API ───────────────────────────────────────────────────── */

sim_net_config_t sim_net_default_config(void)
{
    sim_net_config_t c;
    memset(&c, 0, sizeof(c));
    c.max_endpoints = SIM_NET_DEFAULT_MAX_EP;
    c.buf_capacity = SIM_NET_DEFAULT_BUF;
    c.accept_queue = SIM_NET_DEFAULT_ACCEPTQ;
    return c;
}

sim_net_t *sim_net_create(void)
{
    sim_net_config_t c = sim_net_default_config();
    return sim_net_create_with_config(&c);
}

sim_net_t *sim_net_create_with_config(const sim_net_config_t *cfg)
{
    sim_net_t *net;
    size_t max_ep, buf_cap, accept_cap;

    max_ep = (cfg && cfg->max_endpoints) ? cfg->max_endpoints
                                         : SIM_NET_DEFAULT_MAX_EP;
    buf_cap =
        (cfg && cfg->buf_capacity) ? cfg->buf_capacity : SIM_NET_DEFAULT_BUF;
    accept_cap = (cfg && cfg->accept_queue) ? cfg->accept_queue
                                            : SIM_NET_DEFAULT_ACCEPTQ;
    if (max_ep == 0) {
        max_ep = SIM_NET_DEFAULT_MAX_EP;
    }
    if (buf_cap == 0) {
        buf_cap = SIM_NET_DEFAULT_BUF;
    }
    if (accept_cap == 0) {
        accept_cap = SIM_NET_DEFAULT_ACCEPTQ;
    }

    net = (sim_net_t *)calloc(1, sizeof(*net));
    if (!net) {
        return NULL;
    }
    net->eps = (endpoint_t *)calloc(max_ep, sizeof(endpoint_t));
    net->evq = (sim_net_event_t *)calloc(SIM_NET_EVENT_Q, sizeof(sim_net_event_t));
    if (!net->eps || !net->evq) {
        free(net->eps);
        free(net->evq);
        free(net);
        return NULL;
    }
    net->max_ep = max_ep;
    net->buf_cap = buf_cap;
    net->accept_cap = accept_cap;
    net->evq_cap = SIM_NET_EVENT_Q;
    net->next_id = 1;
    return net;
}

void sim_net_destroy(sim_net_t *net)
{
    size_t i;
    if (!net) {
        return;
    }
    for (i = 0; i < net->max_ep; i++) {
        if (net->eps[i].used) {
            free_ep(net, &net->eps[i]);
        }
    }
    free(net->eps);
    free(net->evq);
    free(net);
}

int sim_net_pipe(sim_net_t *net, uint64_t *out_a, uint64_t *out_b)
{
    endpoint_t *a, *b;
    if (!net || !out_a || !out_b) {
        return SIM_NET_EINVAL;
    }
    a = alloc_ep(net);
    if (!a) {
        return SIM_NET_ENOMEM;
    }
    if (init_stream_ep(net, a) != 0) {
        return SIM_NET_ENOMEM;
    }
    b = alloc_ep(net);
    if (!b) {
        free_ep(net, a);
        return SIM_NET_ENOMEM;
    }
    if (init_stream_ep(net, b) != 0) {
        free_ep(net, a);
        return SIM_NET_ENOMEM;
    }
    a->peer_id = b->id;
    b->peer_id = a->id;
    *out_a = a->id;
    *out_b = b->id;
    push_event(net, SIM_NET_EVENT_WRITABLE, a->id, 0, 0);
    push_event(net, SIM_NET_EVENT_WRITABLE, b->id, 0, 0);
    return 0;
}

uint64_t sim_net_socket(sim_net_t *net)
{
    endpoint_t *ep;
    if (!net) {
        return 0;
    }
    ep = alloc_ep(net);
    if (!ep) {
        return 0;
    }
    if (init_stream_ep(net, ep) != 0) {
        return 0;
    }
    ep->peer_id = 0;
    return ep->id;
}

int sim_net_listen(sim_net_t *net, uint64_t fd)
{
    endpoint_t *ep = find_ep(net, fd);
    if (!ep || ep->kind != EP_STREAM || ep->peer_id != 0 || ep->local_closed) {
        return SIM_NET_EINVAL;
    }
    ep->kind = EP_LISTENER;
    ep->accept_q = (uint64_t *)calloc(net->accept_cap, sizeof(uint64_t));
    if (!ep->accept_q) {
        ep->kind = EP_STREAM;
        return SIM_NET_ENOMEM;
    }
    ep->accept_cap = net->accept_cap;
    ep->accept_len = 0;
    ring_free(&ep->rx); /* listener has no stream buffer */
    return 0;
}

int sim_net_connect(sim_net_t *net, uint64_t client_fd, uint64_t listener_fd)
{
    endpoint_t *cli = find_ep(net, client_fd);
    endpoint_t *lst = find_ep(net, listener_fd);
    endpoint_t *srv;
    if (!cli || !lst) {
        return SIM_NET_EINVAL;
    }
    if (cli->kind != EP_STREAM || lst->kind != EP_LISTENER) {
        return SIM_NET_EINVAL;
    }
    if (cli->peer_id != 0 || cli->local_closed) {
        return SIM_NET_EINVAL;
    }
    if (lst->accept_len >= lst->accept_cap) {
        return SIM_NET_EAGAIN;
    }
    /* Server-side endpoint for this connection */
    srv = alloc_ep(net);
    if (!srv) {
        return SIM_NET_ENOMEM;
    }
    if (init_stream_ep(net, srv) != 0) {
        return SIM_NET_ENOMEM;
    }
    cli->peer_id = srv->id;
    srv->peer_id = cli->id;
    lst->accept_q[lst->accept_len++] = srv->id;
    push_event(net, SIM_NET_EVENT_ACCEPT, listener_fd, srv->id, 0);
    push_event(net, SIM_NET_EVENT_WRITABLE, client_fd, 0, 0);
    push_event(net, SIM_NET_EVENT_WRITABLE, srv->id, 0, 0);
    return 0;
}

int sim_net_accept(sim_net_t *net, uint64_t listener_fd, uint64_t *out_fd)
{
    endpoint_t *lst = find_ep(net, listener_fd);
    uint64_t id;
    size_t i;
    if (!lst || lst->kind != EP_LISTENER || !out_fd) {
        return SIM_NET_EINVAL;
    }
    if (lst->accept_len == 0) {
        return SIM_NET_EAGAIN;
    }
    id = lst->accept_q[0];
    for (i = 1; i < lst->accept_len; i++) {
        lst->accept_q[i - 1] = lst->accept_q[i];
    }
    lst->accept_len--;
    *out_fd = id;
    return 0;
}

int sim_net_send(sim_net_t *net, uint64_t fd, const void *buf, size_t len)
{
    endpoint_t *ep = find_ep(net, fd);
    endpoint_t *peer;
    size_t n;
    int was_empty;
    int was_full;

    if (!ep || ep->kind != EP_STREAM) {
        return SIM_NET_EINVAL;
    }
    if (ep->local_closed) {
        return SIM_NET_EPIPE;
    }
    if (ep->peer_id == 0) {
        return SIM_NET_EPIPE;
    }
    peer = find_ep(net, ep->peer_id);
    if (!peer || peer->kind != EP_STREAM) {
        return SIM_NET_EPIPE;
    }
    if (peer->local_closed) {
        return SIM_NET_EPIPE;
    }
    if (len == 0) {
        return 0;
    }
    if (!buf) {
        return SIM_NET_EINVAL;
    }
    if (ring_space(&peer->rx) == 0) {
        return SIM_NET_EAGAIN;
    }
    was_empty = (peer->rx.len == 0);
    was_full = 0;
    n = ring_write(&peer->rx, (const uint8_t *)buf, len);
    if (n == 0) {
        return SIM_NET_EAGAIN;
    }
    if (was_empty && n > 0) {
        push_event(net, SIM_NET_EVENT_READABLE, peer->id, 0, 0);
    }
    (void)was_full;
    if (ring_space(&peer->rx) > 0) {
        /* still writable */
    } else {
        /* became full — no WRITABLE for us until peer recvs */
    }
    return (int)n;
}

int sim_net_recv(sim_net_t *net, uint64_t fd, void *buf, size_t len)
{
    endpoint_t *ep = find_ep(net, fd);
    endpoint_t *peer;
    size_t n;
    int was_full;
    size_t space_before;

    if (!ep || ep->kind != EP_STREAM) {
        return SIM_NET_EINVAL;
    }
    if (len == 0) {
        return 0;
    }
    if (!buf) {
        return SIM_NET_EINVAL;
    }
    if (ep->rx.len == 0) {
        if (ep->peer_closed || (ep->peer_id == 0 && ep->local_closed)) {
            return 0; /* EOF */
        }
        if (ep->peer_id == 0) {
            return SIM_NET_EAGAIN; /* not connected yet */
        }
        peer = find_ep(net, ep->peer_id);
        if (!peer || peer->local_closed) {
            return 0; /* EOF */
        }
        return SIM_NET_EAGAIN;
    }
    space_before = ring_space(&ep->rx);
    was_full = (space_before == 0);
    n = ring_read(&ep->rx, (uint8_t *)buf, len);
    if (was_full && n > 0 && ep->peer_id != 0) {
        peer = find_ep(net, ep->peer_id);
        if (peer && !peer->local_closed) {
            push_event(net, SIM_NET_EVENT_WRITABLE, peer->id, 0, 0);
        }
    }
    if (ep->rx.len == 0 && ep->peer_closed) {
        push_event(net, SIM_NET_EVENT_HUP, ep->id, 0, 0);
    }
    return (int)n;
}

int sim_net_close(sim_net_t *net, uint64_t fd)
{
    endpoint_t *ep = find_ep(net, fd);
    endpoint_t *peer;
    size_t i;

    if (!ep) {
        return SIM_NET_EINVAL;
    }
    if (ep->kind == EP_LISTENER) {
        /* Drop pending accepts' server ends if still unaccepted? keep simple:
         * leave them; host should accept or abandon. Free listener only. */
        free_ep(net, ep);
        return 0;
    }
    if (ep->local_closed) {
        free_ep(net, ep);
        return 0;
    }
    ep->local_closed = 1;
    if (ep->peer_id != 0) {
        peer = find_ep(net, ep->peer_id);
        if (peer) {
            peer->peer_closed = 1;
            push_event(net, SIM_NET_EVENT_HUP, peer->id, 0, 0);
            if (peer->rx.len > 0) {
                push_event(net, SIM_NET_EVENT_READABLE, peer->id, 0, 0);
            }
            /* clear reverse peer link so peer send fails */
            if (peer->peer_id == ep->id) {
                /* keep peer_id for identity but mark closed via peer_closed */
            }
        }
    }
    /* If peer already closed and our rx empty, free fully */
    if (ep->peer_closed && ep->rx.len == 0) {
        free_ep(net, ep);
        return 0;
    }
    /* Detach: free local endpoint; peer keeps peer_closed */
    if (ep->peer_id != 0) {
        peer = find_ep(net, ep->peer_id);
        if (peer && peer->peer_id == ep->id) {
            /* peer still points at us — clear so send uses peer_closed path */
            peer->peer_id = 0;
        }
    }
    free_ep(net, ep);
    (void)i;
    return 0;
}

int sim_net_readable(const sim_net_t *net, uint64_t fd)
{
    const endpoint_t *ep = find_ep_c(net, fd);
    if (!ep || ep->kind != EP_STREAM) {
        return -1;
    }
    if (ep->rx.len > 0) {
        return 1;
    }
    if (ep->peer_closed || ep->peer_id == 0) {
        /* EOF readable as 0-byte */
        return ep->peer_closed ? 1 : 0;
    }
    return 0;
}

int sim_net_writable(const sim_net_t *net, uint64_t fd)
{
    const endpoint_t *ep = find_ep_c(net, fd);
    const endpoint_t *peer;
    if (!ep || ep->kind != EP_STREAM) {
        return -1;
    }
    if (ep->local_closed || ep->peer_id == 0) {
        return 0;
    }
    peer = find_ep_c(net, ep->peer_id);
    if (!peer || peer->local_closed) {
        return 0;
    }
    return ring_space(&peer->rx) > 0 ? 1 : 0;
}

int sim_net_next_event(sim_net_t *net, sim_net_event_t *out)
{
    if (!net || !out) {
        return 0;
    }
    if (net->evq_len == 0) {
        memset(out, 0, sizeof(*out));
        return 0;
    }
    *out = net->evq[net->evq_head];
    net->evq_head = (net->evq_head + 1) % net->evq_cap;
    net->evq_len--;
    return 1;
}

size_t sim_net_endpoint_count(const sim_net_t *net)
{
    return net ? net->live : 0;
}
