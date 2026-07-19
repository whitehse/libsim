/**
 * P0.3 smoke: sim_uring over sim_net + sim_clock.
 */
#include "sim.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void drain_cq(sim_uring_t *r)
{
    sim_cqe_t *cqe;
    while (sim_uring_peek_cqe(r, &cqe)) {
        sim_uring_cqe_seen(r, cqe);
    }
}

static void test_nop(void)
{
    sim_net_t *net = sim_net_create();
    sim_uring_t *r = sim_uring_create(net, NULL);
    sim_sqe_t *sqe;
    sim_cqe_t *cqe;

    sqe = sim_uring_get_sqe(r);
    assert(sqe);
    sim_sqe_prep_nop(sqe);
    sim_sqe_set_user_data(sqe, 0xABC);
    assert(sim_uring_submit(r) == 1);
    assert(sim_uring_peek_cqe(r, &cqe) == 1);
    assert(cqe->user_data == 0xABC);
    assert(cqe->res == 0);
    sim_uring_cqe_seen(r, cqe);
    assert(sim_uring_cq_ready(r) == 0);

    sim_uring_destroy(r);
    sim_net_destroy(net);
    printf("  PASS: nop\n");
}

static void test_send_recv(void)
{
    sim_net_t *net = sim_net_create();
    sim_uring_t *r = sim_uring_create(net, NULL);
    uint64_t a = 0, b = 0;
    char rbuf[16];
    sim_sqe_t *sqe;
    sim_cqe_t *cqe;

    assert(sim_net_pipe(net, &a, &b) == 0);

    sqe = sim_uring_get_sqe(r);
    sim_sqe_prep_send(sqe, a, "hi", 2);
    sim_sqe_set_user_data(sqe, 1);
    assert(sim_uring_submit(r) == 1);
    assert(sim_uring_peek_cqe(r, &cqe) == 1);
    assert(cqe->user_data == 1 && cqe->res == 2);
    sim_uring_cqe_seen(r, cqe);

    memset(rbuf, 0, sizeof(rbuf));
    sqe = sim_uring_get_sqe(r);
    sim_sqe_prep_recv(sqe, b, rbuf, sizeof(rbuf));
    sim_sqe_set_user_data(sqe, 2);
    assert(sim_uring_submit(r) == 1);
    assert(sim_uring_peek_cqe(r, &cqe) == 1);
    assert(cqe->user_data == 2 && cqe->res == 2);
    assert(memcmp(rbuf, "hi", 2) == 0);
    sim_uring_cqe_seen(r, cqe);

    sim_uring_destroy(r);
    sim_net_destroy(net);
    printf("  PASS: send/recv immediate\n");
}

static void test_recv_deferred(void)
{
    sim_net_t *net = sim_net_create();
    sim_uring_t *r = sim_uring_create(net, NULL);
    uint64_t a = 0, b = 0;
    char rbuf[8];
    sim_sqe_t *sqe;
    sim_cqe_t *cqe;

    assert(sim_net_pipe(net, &a, &b) == 0);

    /* RECV before data → pending */
    sqe = sim_uring_get_sqe(r);
    sim_sqe_prep_recv(sqe, b, rbuf, sizeof(rbuf));
    sim_sqe_set_user_data(sqe, 9);
    assert(sim_uring_submit(r) == 1);
    assert(sim_uring_cq_ready(r) == 0);
    assert(sim_uring_pending(r) == 1);

    assert(sim_net_send(net, a, "z", 1) == 1);
    assert(sim_uring_progress(r) == 1);
    assert(sim_uring_peek_cqe(r, &cqe) == 1);
    assert(cqe->user_data == 9 && cqe->res == 1 && rbuf[0] == 'z');
    sim_uring_cqe_seen(r, cqe);
    assert(sim_uring_pending(r) == 0);

    sim_uring_destroy(r);
    sim_net_destroy(net);
    printf("  PASS: deferred recv + progress\n");
}

static void test_accept(void)
{
    sim_net_t *net = sim_net_create();
    sim_uring_t *r = sim_uring_create(net, NULL);
    uint64_t lst, cli;
    sim_sqe_t *sqe;
    sim_cqe_t *cqe;
    int32_t accepted;

    lst = sim_net_socket(net);
    cli = sim_net_socket(net);
    assert(sim_net_listen(net, lst) == 0);

    sqe = sim_uring_get_sqe(r);
    sim_sqe_prep_accept(sqe, lst);
    sim_sqe_set_user_data(sqe, 3);
    assert(sim_uring_submit(r) == 1);
    assert(sim_uring_pending(r) == 1);

    assert(sim_net_connect(net, cli, lst) == 0);
    assert(sim_uring_progress(r) == 1);
    assert(sim_uring_peek_cqe(r, &cqe) == 1);
    assert(cqe->user_data == 3);
    accepted = cqe->res;
    assert(accepted > 0);
    sim_uring_cqe_seen(r, cqe);

    /* exchange */
    sqe = sim_uring_get_sqe(r);
    sim_sqe_prep_send(sqe, cli, "ok", 2);
    sim_sqe_set_user_data(sqe, 4);
    sim_uring_submit(r);
    drain_cq(r);

    {
        char buf[4];
        sqe = sim_uring_get_sqe(r);
        sim_sqe_prep_recv(sqe, (uint64_t)accepted, buf, sizeof(buf));
        sim_sqe_set_user_data(sqe, 5);
        sim_uring_submit(r);
        assert(sim_uring_peek_cqe(r, &cqe) == 1);
        assert(cqe->res == 2);
        sim_uring_cqe_seen(r, cqe);
    }

    sim_uring_destroy(r);
    sim_net_destroy(net);
    printf("  PASS: accept deferred\n");
}

static void test_timeout(void)
{
    sim_net_t *net = sim_net_create();
    sim_clock_t *clk = sim_clock_create();
    sim_uring_t *r = sim_uring_create(net, clk);
    sim_sqe_t *sqe;
    sim_cqe_t *cqe;

    sqe = sim_uring_get_sqe(r);
    sim_sqe_prep_timeout(sqe, 1000);
    sim_sqe_set_user_data(sqe, 7);
    assert(sim_uring_submit(r) == 1);
    assert(sim_uring_pending(r) == 1);

    sim_clock_advance_ns(clk, 999);
    assert(sim_uring_progress(r) == 0);

    sim_clock_advance_ns(clk, 1);
    assert(sim_uring_progress(r) == 1);
    assert(sim_uring_peek_cqe(r, &cqe) == 1);
    assert(cqe->user_data == 7);
    assert(cqe->res < 0); /* timed out */
    sim_uring_cqe_seen(r, cqe);

    sim_uring_destroy(r);
    sim_clock_destroy(clk);
    sim_net_destroy(net);
    printf("  PASS: timeout\n");
}

static void test_poll(void)
{
    sim_net_t *net = sim_net_create();
    sim_uring_t *r = sim_uring_create(net, NULL);
    uint64_t a = 0, b = 0;
    sim_sqe_t *sqe;
    sim_cqe_t *cqe;

    assert(sim_net_pipe(net, &a, &b) == 0);

    sqe = sim_uring_get_sqe(r);
    sim_sqe_prep_poll_add(sqe, b, SIM_POLL_IN);
    sim_sqe_set_user_data(sqe, 11);
    assert(sim_uring_submit(r) == 1);
    assert(sim_uring_pending(r) == 1);

    assert(sim_net_send(net, a, "p", 1) == 1);
    assert(sim_uring_progress(r) == 1);
    assert(sim_uring_peek_cqe(r, &cqe) == 1);
    assert(cqe->user_data == 11);
    assert((cqe->res & SIM_POLL_IN) != 0);
    sim_uring_cqe_seen(r, cqe);

    sim_uring_destroy(r);
    sim_net_destroy(net);
    printf("  PASS: poll IN\n");
}

int main(void)
{
    printf("libsim_uring:\n");
    test_nop();
    test_send_recv();
    test_recv_deferred();
    test_accept();
    test_timeout();
    test_poll();
    printf("ALL PASS\n");
    return 0;
}
