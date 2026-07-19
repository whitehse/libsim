/**
 * P0.2 smoke: in-memory pipes + listen/accept.
 */
#include "sim_net.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void drain_events(sim_net_t *net)
{
    sim_net_event_t ev;
    while (sim_net_next_event(net, &ev)) {
        /* discard */
    }
}

static void test_pipe_echo(void)
{
    sim_net_t *net = sim_net_create();
    uint64_t a = 0, b = 0;
    char buf[64];
    int n;
    sim_net_event_t ev;
    int saw_readable = 0;

    assert(net);
    assert(sim_net_pipe(net, &a, &b) == 0);
    assert(a != 0 && b != 0 && a != b);
    assert(sim_net_endpoint_count(net) == 2);
    assert(sim_net_writable(net, a) == 1);
    assert(sim_net_readable(net, b) == 0);

    n = sim_net_send(net, a, "hello", 5);
    assert(n == 5);
    assert(sim_net_readable(net, b) == 1);

    while (sim_net_next_event(net, &ev)) {
        if (ev.type == SIM_NET_EVENT_READABLE && ev.fd == b) {
            saw_readable = 1;
        }
    }
    assert(saw_readable);

    n = sim_net_recv(net, b, buf, sizeof(buf));
    assert(n == 5);
    assert(memcmp(buf, "hello", 5) == 0);

    assert(sim_net_close(net, a) == 0);
    n = sim_net_recv(net, b, buf, sizeof(buf));
    assert(n == 0); /* EOF */

    sim_net_close(net, b);
    sim_net_destroy(net);
    printf("  PASS: pipe echo + EOF\n");
}

static void test_pipe_backpressure(void)
{
    sim_net_config_t cfg = sim_net_default_config();
    sim_net_t *net;
    uint64_t a = 0, b = 0;
    char chunk[16];
    int total = 0;
    int n;
    char rbuf[256];

    cfg.buf_capacity = 32;
    cfg.max_endpoints = 8;
    net = sim_net_create_with_config(&cfg);
    assert(sim_net_pipe(net, &a, &b) == 0);
    memset(chunk, 'x', sizeof(chunk));

    while ((n = sim_net_send(net, a, chunk, sizeof(chunk))) > 0) {
        total += n;
    }
    assert(n == SIM_NET_EAGAIN);
    assert(total == 32);
    assert(sim_net_writable(net, a) == 0);

    n = sim_net_recv(net, b, rbuf, 16);
    assert(n == 16);
    assert(sim_net_writable(net, a) == 1);

    n = sim_net_send(net, a, chunk, 16);
    assert(n == 16);

    sim_net_destroy(net);
    printf("  PASS: backpressure EAGAIN\n");
}

static void test_listen_accept(void)
{
    sim_net_t *net = sim_net_create();
    uint64_t lst, cli, accepted = 0;
    char buf[8];
    int n;
    sim_net_event_t ev;
    int saw_accept = 0;

    lst = sim_net_socket(net);
    cli = sim_net_socket(net);
    assert(lst && cli);
    assert(sim_net_listen(net, lst) == 0);
    assert(sim_net_connect(net, cli, lst) == 0);

    while (sim_net_next_event(net, &ev)) {
        if (ev.type == SIM_NET_EVENT_ACCEPT && ev.fd == lst) {
            saw_accept = 1;
            assert(ev.peer_fd != 0);
        }
    }
    assert(saw_accept);

    assert(sim_net_accept(net, lst, &accepted) == 0);
    assert(accepted != 0);
    assert(sim_net_accept(net, lst, &accepted) == SIM_NET_EAGAIN);

    n = sim_net_send(net, cli, "ping", 4);
    assert(n == 4);
    n = sim_net_recv(net, accepted, buf, sizeof(buf));
    assert(n == 4 && memcmp(buf, "ping", 4) == 0);

    n = sim_net_send(net, accepted, "pong", 4);
    assert(n == 4);
    n = sim_net_recv(net, cli, buf, sizeof(buf));
    assert(n == 4 && memcmp(buf, "pong", 4) == 0);

    sim_net_destroy(net);
    printf("  PASS: listen/connect/accept\n");
}

static void test_invalid(void)
{
    sim_net_t *net = sim_net_create();
    uint64_t a = 0, b = 0;
    char c;
    assert(sim_net_pipe(net, &a, &b) == 0);
    assert(sim_net_send(net, 999, "x", 1) == SIM_NET_EINVAL);
    assert(sim_net_recv(net, 999, &c, 1) == SIM_NET_EINVAL);
    assert(sim_net_close(net, 999) == SIM_NET_EINVAL);
    drain_events(net);
    sim_net_destroy(net);
    printf("  PASS: invalid fd\n");
}

int main(void)
{
    printf("libsim_net:\n");
    test_pipe_echo();
    test_pipe_backpressure();
    test_listen_accept();
    test_invalid();
    printf("ALL PASS\n");
    return 0;
}
