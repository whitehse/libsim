#include "sim_fuzz.h"

#include <string.h>

void sim_fuzz_cursor_init(sim_fuzz_cursor_t *c, const uint8_t *data, size_t size)
{
    if (!c) {
        return;
    }
    c->data = data;
    c->size = size;
    c->pos = 0;
}

size_t sim_fuzz_remaining(const sim_fuzz_cursor_t *c)
{
    if (!c || c->pos >= c->size) {
        return 0;
    }
    return c->size - c->pos;
}

uint8_t sim_fuzz_u8(sim_fuzz_cursor_t *c, int *ok)
{
    if (!c || c->pos >= c->size) {
        if (ok) {
            *ok = 0;
        }
        return 0;
    }
    if (ok) {
        *ok = 1;
    }
    return c->data[c->pos++];
}

uint16_t sim_fuzz_u16(sim_fuzz_cursor_t *c, int *ok)
{
    uint8_t a, b;
    int oka = 1, okb = 1;
    a = sim_fuzz_u8(c, &oka);
    b = sim_fuzz_u8(c, &okb);
    if (ok) {
        *ok = oka && okb;
    }
    return (uint16_t)a | ((uint16_t)b << 8);
}

uint32_t sim_fuzz_u32(sim_fuzz_cursor_t *c, int *ok)
{
    uint16_t lo, hi;
    int ok1 = 1, ok2 = 1;
    lo = sim_fuzz_u16(c, &ok1);
    hi = sim_fuzz_u16(c, &ok2);
    if (ok) {
        *ok = ok1 && ok2;
    }
    return (uint32_t)lo | ((uint32_t)hi << 16);
}

int sim_fuzz_bytes(sim_fuzz_cursor_t *c, uint8_t *dst, size_t max, size_t *out_len)
{
    int ok = 1;
    uint8_t n;
    size_t take, i;
    if (out_len) {
        *out_len = 0;
    }
    if (!c) {
        return -1;
    }
    n = sim_fuzz_u8(c, &ok);
    if (!ok) {
        return -1;
    }
    take = n;
    if (take > max) {
        take = max;
    }
    if (take > sim_fuzz_remaining(c)) {
        take = sim_fuzz_remaining(c);
    }
    for (i = 0; i < take; i++) {
        if (dst) {
            dst[i] = c->data[c->pos];
        }
        c->pos++;
    }
    /* skip excess length if n > take and still data */
    {
        size_t skip = (size_t)n > take ? (size_t)n - take : 0;
        if (skip > sim_fuzz_remaining(c)) {
            skip = sim_fuzz_remaining(c);
        }
        c->pos += skip;
    }
    if (out_len) {
        *out_len = take;
    }
    return 0;
}

int sim_fuzz_drive_bc(const uint8_t *data, size_t size)
{
    sim_fuzz_cursor_t cur;
    sim_clock_t *clk;
    sim_timer_mgr_t *tm;
    sim_net_t *net;
    uint64_t pipe_a = 0, pipe_b = 0;
    uint64_t last_timer = 0;
    uint64_t every_id = 0;
    int steps = 0;
    const int max_steps = 256;

    sim_fuzz_cursor_init(&cur, data, size);
    clk = sim_clock_create();
    tm = sim_timer_create(clk);
    net = sim_net_create();
    if (!clk || !tm || !net) {
        sim_timer_destroy(tm);
        sim_clock_destroy(clk);
        sim_net_destroy(net);
        return 0;
    }

    while (sim_fuzz_remaining(&cur) > 0 && steps < max_steps) {
        int ok = 1;
        uint8_t op = sim_fuzz_u8(&cur, &ok);
        uint8_t code;
        if (!ok) {
            break;
        }
        code = op & 0x0f;
        steps++;

        switch (code) {
        case 0: {
            uint16_t d = sim_fuzz_u16(&cur, &ok);
            if (ok) {
                sim_clock_advance_ns(clk, d);
            }
            break;
        }
        case 1: {
            uint16_t d = sim_fuzz_u16(&cur, &ok);
            if (ok) {
                last_timer = sim_timer_schedule_after(tm, d, NULL);
            }
            break;
        }
        case 2: {
            uint16_t d = sim_fuzz_u16(&cur, &ok);
            if (ok && d > 0) {
                if (every_id) {
                    sim_timer_cancel(tm, every_id);
                }
                every_id = sim_timer_schedule_every(tm, d, NULL);
                last_timer = every_id;
            }
            break;
        }
        case 3:
            if (last_timer) {
                sim_timer_cancel(tm, last_timer);
                if (last_timer == every_id) {
                    every_id = 0;
                }
                last_timer = 0;
            }
            break;
        case 4: {
            sim_timer_event_t ev;
            int n = 0;
            while (sim_timer_next_event(tm, &ev) && n < 32) {
                n++;
            }
            break;
        }
        case 5:
            if (pipe_a == 0) {
                (void)sim_net_pipe(net, &pipe_a, &pipe_b);
            }
            break;
        case 6: {
            uint8_t buf[64];
            size_t len = 0;
            if (pipe_a == 0) {
                (void)sim_net_pipe(net, &pipe_a, &pipe_b);
            }
            if (sim_fuzz_bytes(&cur, buf, sizeof(buf), &len) == 0 && pipe_a) {
                (void)sim_net_send(net, pipe_a, buf, len);
            }
            break;
        }
        case 7: {
            uint8_t buf[64];
            if (pipe_b) {
                (void)sim_net_recv(net, pipe_b, buf, sizeof(buf));
            }
            break;
        }
        case 8:
            if (pipe_a) {
                sim_net_close(net, pipe_a);
                pipe_a = 0;
            }
            break;
        case 9:
            if (pipe_b) {
                sim_net_close(net, pipe_b);
                pipe_b = 0;
            }
            break;
        case 10: {
            uint64_t lst = sim_net_socket(net);
            uint64_t cli = sim_net_socket(net);
            uint64_t acc = 0;
            if (lst && cli && sim_net_listen(net, lst) == 0) {
                if (sim_net_connect(net, cli, lst) == 0) {
                    (void)sim_net_accept(net, lst, &acc);
                }
            }
            if (acc) {
                sim_net_close(net, acc);
            }
            if (cli) {
                sim_net_close(net, cli);
            }
            if (lst) {
                sim_net_close(net, lst);
            }
            break;
        }
        case 11: {
            sim_net_event_t ev;
            int n = 0;
            while (sim_net_next_event(net, &ev) && n < 64) {
                n++;
            }
            break;
        }
        default:
            break;
        }
    }

    sim_timer_destroy(tm);
    sim_clock_destroy(clk);
    sim_net_destroy(net);
    return 0;
}

#if !defined(LIBSIM_NO_URING)

int sim_fuzz_drive_a(const uint8_t *data, size_t size)
{
    sim_fuzz_cursor_t cur;
    sim_clock_t *clk;
    sim_net_t *net;
    sim_uring_t *ring;
    uint64_t a = 0, b = 0;
    int steps = 0;
    const int max_steps = 256;
    uint8_t rbuf[64];

    /* Always exercise BC path on a copy of the stream first (cheap). */
    (void)sim_fuzz_drive_bc(data, size > 128 ? 128 : size);

    sim_fuzz_cursor_init(&cur, data, size);
    clk = sim_clock_create();
    net = sim_net_create();
    ring = sim_uring_create(net, clk);
    if (!clk || !net || !ring) {
        sim_uring_destroy(ring);
        sim_net_destroy(net);
        sim_clock_destroy(clk);
        return 0;
    }
    (void)sim_net_pipe(net, &a, &b);

    while (sim_fuzz_remaining(&cur) > 0 && steps < max_steps) {
        int ok = 1;
        uint8_t op = sim_fuzz_u8(&cur, &ok);
        sim_sqe_t *sqe;
        if (!ok) {
            break;
        }
        steps++;

        if ((op & 0x80) == 0) {
            /* light BC-style advance/send mixed in */
            if ((op & 0x0f) == 0) {
                uint16_t d = sim_fuzz_u16(&cur, &ok);
                if (ok) {
                    sim_clock_advance_ns(clk, d);
                    (void)sim_uring_progress(ring);
                }
            } else if ((op & 0x0f) == 6 && a) {
                uint8_t buf[32];
                size_t len = 0;
                if (sim_fuzz_bytes(&cur, buf, sizeof(buf), &len) == 0) {
                    (void)sim_net_send(net, a, buf, len);
                    (void)sim_uring_progress(ring);
                }
            }
            continue;
        }

        switch (op & 0x0f) {
        case 0:
            sqe = sim_uring_get_sqe(ring);
            if (sqe) {
                sim_sqe_prep_nop(sqe);
                sim_sqe_set_user_data(sqe, op);
                (void)sim_uring_submit(ring);
            }
            break;
        case 1:
            sqe = sim_uring_get_sqe(ring);
            if (sqe && b) {
                sim_sqe_prep_recv(sqe, b, rbuf, sizeof(rbuf));
                sim_sqe_set_user_data(sqe, op);
                (void)sim_uring_submit(ring);
                (void)sim_uring_progress(ring);
            }
            break;
        case 2: {
            uint8_t buf[32];
            size_t len = 0;
            sqe = sim_uring_get_sqe(ring);
            if (sqe && a &&
                sim_fuzz_bytes(&cur, buf, sizeof(buf), &len) == 0) {
                sim_sqe_prep_send(sqe, a, buf, len);
                sim_sqe_set_user_data(sqe, op);
                (void)sim_uring_submit(ring);
                (void)sim_uring_progress(ring);
            }
            break;
        }
        case 3:
            sqe = sim_uring_get_sqe(ring);
            if (sqe && b) {
                sim_sqe_prep_poll_add(sqe, b, SIM_POLL_IN);
                sim_sqe_set_user_data(sqe, op);
                (void)sim_uring_submit(ring);
                (void)sim_uring_progress(ring);
            }
            break;
        case 4: {
            uint16_t d = sim_fuzz_u16(&cur, &ok);
            sqe = sim_uring_get_sqe(ring);
            if (sqe && ok) {
                sim_sqe_prep_timeout(sqe, d);
                sim_sqe_set_user_data(sqe, op);
                (void)sim_uring_submit(ring);
                sim_clock_advance_ns(clk, (uint64_t)d + 1u);
                (void)sim_uring_progress(ring);
            }
            break;
        }
        default:
            break;
        }

        /* drain some CQEs */
        {
            sim_cqe_t *cqe;
            int n = 0;
            while (sim_uring_peek_cqe(ring, &cqe) && n < 16) {
                sim_uring_cqe_seen(ring, cqe);
                n++;
            }
        }
    }

    sim_uring_destroy(ring);
    sim_net_destroy(net);
    sim_clock_destroy(clk);
    return 0;
}

#endif /* !LIBSIM_NO_URING */
