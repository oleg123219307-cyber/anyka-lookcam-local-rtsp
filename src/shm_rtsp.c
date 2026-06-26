#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SHMID 0
#define SHM_BYTES 1048628
#define RING_BEGIN 52
#define RING_END 1048576
#define MAX_NALS 512
#define RTP_PAYLOAD 1200
#define HOLD_BACK_NALS 8
#define FRAME_SLEEP_US 80000
#define RTP_TS_INC 9000

struct nal {
    size_t start;
    size_t payload;
    size_t end;
    int type;
};

static unsigned char *g_shm;
static uint16_t g_seq = 1;
static uint32_t g_ts = 1;
static const uint32_t g_ssrc = 0x39180001;

static int find_start(const unsigned char *p, size_t n, size_t i, int *sc) {
    if (i + 4 <= n && p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 0 && p[i + 3] == 1) {
        *sc = 4;
        return 1;
    }
    if (i + 3 <= n && p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1) {
        *sc = 3;
        return 1;
    }
    return 0;
}

static uint32_t rd32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static size_t hdr_ptr(int off) {
    size_t p = (size_t)(rd32(g_shm + off) & 0x000fffff);
    if (p < RING_BEGIN || p >= RING_END) return RING_BEGIN;
    return p;
}

static size_t before_pos(size_t p) {
    return p > RING_BEGIN ? p - 1 : RING_END - 1;
}

static size_t ring_next(size_t p) {
    p++;
    return p < RING_END ? p : RING_BEGIN;
}

static size_t ring_advance(size_t p, size_t n) {
    size_t ring_size = RING_END - RING_BEGIN;
    size_t off = (p - RING_BEGIN + n) % ring_size;
    return RING_BEGIN + off;
}

static size_t ring_distance(size_t from, size_t to) {
    if (to >= from) return to - from;
    return (RING_END - from) + (to - RING_BEGIN);
}

static unsigned char ring_byte(size_t p) {
    return g_shm[p];
}

static void ring_copy(size_t pos, unsigned char *dst, size_t len) {
    while (len) {
        size_t chunk = RING_END - pos;
        if (chunk > len) chunk = len;
        memcpy(dst, g_shm + pos, chunk);
        dst += chunk;
        len -= chunk;
        pos = RING_BEGIN;
    }
}

static size_t nal_len(const struct nal *n) {
    return ring_distance(n->payload, n->end);
}

static int collect_nals(struct nal *out, int max) {
    int count = 0;
    size_t starts[MAX_NALS];
    int scs[MAX_NALS];
    for (size_t i = RING_BEGIN; i + 5 < RING_END && count < max; i++) {
        int sc = 0;
        if (!find_start(g_shm, SHM_BYTES, i, &sc)) continue;
        starts[count] = i;
        scs[count] = sc;
        count++;
        i += (size_t)sc;
    }
    for (int i = 0; i < count; i++) {
        size_t payload = starts[i] + (size_t)scs[i];
        size_t end = (i + 1 < count) ? starts[i + 1] : starts[0];
        while (ring_distance(payload, end) > 0 && ring_byte(before_pos(end)) == 0) {
            end = before_pos(end);
        }
        out[i].start = starts[i];
        out[i].payload = payload;
        out[i].end = end;
        out[i].type = g_shm[payload] & 0x1f;
    }
    return count;
}

static int ring_after(size_t p, size_t from, size_t to) {
    if (from < to) return p > from && p < to;
    if (from > to) return p > from || p < to;
    return 0;
}

static int build_ring_order(struct nal *nals, int count, size_t from, size_t to, int *order, int max) {
    int n = 0;
    if (from < to) {
        for (int i = 0; i < count && n < max; i++) {
            if (nals[i].start > from && nals[i].start < to) order[n++] = i;
        }
    } else if (from > to) {
        for (int i = 0; i < count && n < max; i++) {
            if (nals[i].start > from) order[n++] = i;
        }
        for (int i = 0; i < count && n < max; i++) {
            if (nals[i].start < to) order[n++] = i;
        }
    }
    return n;
}

static int find_start_from_gop_hint(struct nal *nals, int count, size_t write_pos) {
    size_t hint = hdr_ptr(12);
    int order[MAX_NALS];
    int n = build_ring_order(nals, count, before_pos(hint), write_pos, order, MAX_NALS);
    for (int i = 0; i < n; i++) {
        int idx = order[i];
        if (nals[idx].type == 7) return idx;
    }
    for (int i = 0; i < n; i++) {
        int idx = order[i];
        if (nals[idx].type == 5) return idx;
    }

    int best_sps = -1, best_idr = -1;
    size_t cursor = before_pos(write_pos);
    int back_order[MAX_NALS];
    int bn = build_ring_order(nals, count, write_pos, cursor, back_order, MAX_NALS);
    for (int i = 0; i < bn; i++) {
        int idx = back_order[i];
        if (nals[idx].type == 7) best_sps = idx;
        if (nals[idx].type == 5) best_idr = idx;
    }
    return best_sps >= 0 ? best_sps : best_idr;
}

static const char b64tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int b64(const unsigned char *in, size_t len, char *out, size_t outsz) {
    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = ((uint32_t)in[i]) << 16;
        int rem = (int)(len - i);
        if (rem > 1) v |= ((uint32_t)in[i + 1]) << 8;
        if (rem > 2) v |= in[i + 2];
        if (o + 4 >= outsz) return -1;
        out[o++] = b64tab[(v >> 18) & 63];
        out[o++] = b64tab[(v >> 12) & 63];
        out[o++] = rem > 1 ? b64tab[(v >> 6) & 63] : '=';
        out[o++] = rem > 2 ? b64tab[v & 63] : '=';
    }
    if (o >= outsz) return -1;
    out[o] = 0;
    return 0;
}

static int send_all(int fd, const void *buf, size_t len) {
    const unsigned char *p = (const unsigned char *)buf;
    while (len) {
        ssize_t n = send(fd, p, len, 0);
        if (n <= 0) return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static int send_rtp_packet(int fd, const unsigned char *payload, size_t len, int marker) {
    unsigned char frame[4 + 12 + RTP_PAYLOAD + 8];
    size_t rtp_len = 12 + len;
    frame[0] = '$';
    frame[1] = 0;
    frame[2] = (unsigned char)((rtp_len >> 8) & 255);
    frame[3] = (unsigned char)(rtp_len & 255);
    frame[4] = 0x80;
    frame[5] = (unsigned char)(96 | (marker ? 0x80 : 0));
    frame[6] = (unsigned char)(g_seq >> 8);
    frame[7] = (unsigned char)(g_seq & 255);
    frame[8] = (unsigned char)(g_ts >> 24);
    frame[9] = (unsigned char)(g_ts >> 16);
    frame[10] = (unsigned char)(g_ts >> 8);
    frame[11] = (unsigned char)(g_ts);
    frame[12] = (unsigned char)(g_ssrc >> 24);
    frame[13] = (unsigned char)(g_ssrc >> 16);
    frame[14] = (unsigned char)(g_ssrc >> 8);
    frame[15] = (unsigned char)(g_ssrc);
    memcpy(frame + 16, payload, len);
    g_seq++;
    return send_all(fd, frame, 4 + rtp_len);
}

static int send_nal(int fd, const unsigned char *nal, size_t len) {
    if (len <= RTP_PAYLOAD) return send_rtp_packet(fd, nal, len, 1);
    unsigned char fu[RTP_PAYLOAD];
    unsigned char hdr = nal[0];
    unsigned char nri = hdr & 0xe0;
    unsigned char typ = hdr & 0x1f;
    size_t off = 1;
    int first = 1;
    while (off < len) {
        size_t chunk = len - off;
        if (chunk > RTP_PAYLOAD - 2) chunk = RTP_PAYLOAD - 2;
        int last = (off + chunk >= len);
        fu[0] = nri | 28;
        fu[1] = typ | (first ? 0x80 : 0) | (last ? 0x40 : 0);
        memcpy(fu + 2, nal + off, chunk);
        if (send_rtp_packet(fd, fu, chunk + 2, last) < 0) return -1;
        off += chunk;
        first = 0;
    }
    return 0;
}

static int send_nal_ring(int fd, size_t payload, size_t len, int marker) {
    if (len <= RTP_PAYLOAD) {
        unsigned char pkt[RTP_PAYLOAD];
        ring_copy(payload, pkt, len);
        return send_rtp_packet(fd, pkt, len, marker);
    }

    unsigned char fu[RTP_PAYLOAD];
    unsigned char hdr = ring_byte(payload);
    unsigned char nri = hdr & 0xe0;
    unsigned char typ = hdr & 0x1f;
    size_t off = 1;
    int first = 1;
    while (off < len) {
        size_t chunk = len - off;
        if (chunk > RTP_PAYLOAD - 2) chunk = RTP_PAYLOAD - 2;
        int last = (off + chunk >= len);
        fu[0] = nri | 28;
        fu[1] = typ | (first ? 0x80 : 0) | (last ? 0x40 : 0);
        ring_copy(ring_advance(payload, off), fu + 2, chunk);
        if (send_rtp_packet(fd, fu, chunk + 2, last && marker) < 0) return -1;
        off += chunk;
        first = 0;
    }
    return 0;
}

static int should_send_nal(int type, size_t len) {
    if (type == 1 || type == 5) return len >= 2 && len <= 200000;
    if (type == 7) return len >= 4 && len <= 256;
    if (type == 8) return len >= 2 && len <= 128;
    if (type == 6) return len >= 2 && len <= 4096;
    if (type == 9) return len >= 2 && len <= 32;
    return 0;
}

struct bitr {
    unsigned char data[128];
    size_t bytes;
    size_t bit;
};

static void rbsp_prefix_from_ring(size_t payload, size_t len, struct bitr *br) {
    br->bytes = 0;
    br->bit = 0;
    int z = 0;
    for (size_t i = 1; i < len && br->bytes < sizeof(br->data); i++) {
        unsigned char b = ring_byte(ring_advance(payload, i));
        if (z >= 2 && b == 3) {
            z = 0;
            continue;
        }
        br->data[br->bytes++] = b;
        if (b == 0) z++;
        else z = 0;
    }
}

static int br_bit(struct bitr *br) {
    if (br->bit >= br->bytes * 8) return -1;
    unsigned char b = br->data[br->bit / 8];
    int v = (b >> (7 - (br->bit & 7))) & 1;
    br->bit++;
    return v;
}

static int br_ue(struct bitr *br) {
    int zeros = 0;
    for (;;) {
        int b = br_bit(br);
        if (b < 0 || zeros > 30) return -1;
        if (b) break;
        zeros++;
    }
    int v = (1 << zeros) - 1;
    for (int i = 0; i < zeros; i++) {
        int b = br_bit(br);
        if (b < 0) return -1;
        v = (v << 1) | b;
    }
    return v;
}

static int first_mb_in_slice(const struct nal *n) {
    if (!(n->type >= 1 && n->type <= 5)) return -1;
    size_t len = nal_len(n);
    if (len < 2) return -1;
    struct bitr br;
    rbsp_prefix_from_ring(n->payload, len, &br);
    return br_ue(&br);
}

static int ends_access_unit(struct nal *nals, int *order, int oi, int send_count) {
    int idx = order[oi];
    if (!(nals[idx].type >= 1 && nals[idx].type <= 5)) return 0;
    for (int j = oi + 1; j < send_count; j++) {
        int ni = order[j];
        int t = nals[ni].type;
        size_t len = nal_len(&nals[ni]);
        if (!should_send_nal(t, len)) continue;
        if (!(t >= 1 && t <= 5)) return 1;
        int mb = first_mb_in_slice(&nals[ni]);
        return mb == 0 || mb < 0;
    }
    return 1;
}

static int latest_gop(struct nal *nals, int count, int *sps_i, int *pps_i, int *idr_i) {
    *sps_i = *pps_i = *idr_i = -1;
    for (int i = 0; i < count; i++) {
        if (nals[i].type == 7) *sps_i = i;
        if (nals[i].type == 8) *pps_i = i;
        if (nals[i].type == 5 && *sps_i >= 0 && *pps_i >= 0) *idr_i = i;
    }
    return *sps_i >= 0 && *pps_i >= 0 && *idr_i >= 0;
}

static void make_sdp(char *sdp, size_t sdp_sz) {
    struct nal nals[MAX_NALS];
    int count = collect_nals(nals, MAX_NALS);
    int si, pi, ii;
    char sps[256] = "", pps[128] = "";
    if (latest_gop(nals, count, &si, &pi, &ii)) {
        unsigned char tmp[256];
        size_t sps_len = nal_len(&nals[si]);
        size_t pps_len = nal_len(&nals[pi]);
        if (sps_len < sizeof(tmp)) {
            ring_copy(nals[si].payload, tmp, sps_len);
            b64(tmp, sps_len, sps, sizeof(sps));
        }
        if (pps_len < sizeof(tmp)) {
            ring_copy(nals[pi].payload, tmp, pps_len);
            b64(tmp, pps_len, pps, sizeof(pps));
        }
    }
    snprintf(sdp, sdp_sz,
        "v=0\r\n"
        "o=- 0 0 IN IP4 0.0.0.0\r\n"
        "s=anyka-shm\r\n"
        "t=0 0\r\n"
        "a=control:*\r\n"
        "m=video 0 RTP/AVP 96\r\n"
        "a=rtpmap:96 H264/90000\r\n"
        "a=fmtp:96 packetization-mode=1;sprop-parameter-sets=%s,%s\r\n"
        "a=control:track1\r\n", sps, pps);
}

static void cseq_of(const char *req, char *out, size_t outsz) {
    const char *p = strstr(req, "CSeq:");
    if (!p) { snprintf(out, outsz, "1"); return; }
    p += 5;
    while (*p == ' ' || *p == '\t') p++;
    size_t i = 0;
    while (*p >= '0' && *p <= '9' && i + 1 < outsz) out[i++] = *p++;
    out[i] = 0;
}

static int reply(int fd, const char *cseq, const char *extra, const char *body) {
    char hdr[2048];
    int blen = body ? (int)strlen(body) : 0;
    int n = snprintf(hdr, sizeof(hdr),
        "RTSP/1.0 200 OK\r\nCSeq: %s\r\n%s%sContent-Length: %d\r\n\r\n",
        cseq, extra ? extra : "", extra ? "" : "", blen);
    if (send_all(fd, hdr, (size_t)n) < 0) return -1;
    if (blen && send_all(fd, body, (size_t)blen) < 0) return -1;
    return 0;
}

static void stream_loop(int fd) {
    size_t last_sent = RING_BEGIN;
    int started = 0;
    int idle = 0;

    for (;;) {
        fd_set rfds;
        struct timeval tv;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        if (select(fd + 1, &rfds, NULL, NULL, &tv) > 0) {
            char tmp;
            ssize_t r = recv(fd, &tmp, 1, MSG_PEEK | MSG_DONTWAIT);
            if (r == 0) return;
        }

        struct nal nals[MAX_NALS];
        int count = collect_nals(nals, MAX_NALS);
        int si, pi, ii;
        if (count <= 0 || !latest_gop(nals, count, &si, &pi, &ii)) {
            usleep(200000);
            continue;
        }

        int order[MAX_NALS];
        int order_count = 0;

        if (!started) {
            size_t write_pos = hdr_ptr(8);
            int start = find_start_from_gop_hint(nals, count, write_pos);
            if (start < 0) start = si < pi ? si : pi;
            if (start > ii) start = ii;
            last_sent = before_pos(nals[start].start);
            order_count = build_ring_order(nals, count, last_sent, write_pos, order, MAX_NALS);
            started = 1;
        } else {
            size_t write_pos = hdr_ptr(8);
            order_count = build_ring_order(nals, count, last_sent, write_pos, order, MAX_NALS);
            if (order_count <= 1) {
                idle++;
                if (idle > 250) {
                    started = 0;
                    idle = 0;
                }
                usleep(40000);
                continue;
            }
        }

        idle = 0;
        int send_count = order_count > HOLD_BACK_NALS ? order_count - HOLD_BACK_NALS : 0;
        if (send_count <= 0) {
            usleep(40000);
            continue;
        }
        for (int oi = 0; oi < send_count; oi++) {
            int i = order[oi];
            int t = nals[i].type;
            int is_vcl = (t == 1 || t == 5);
            size_t len = nal_len(&nals[i]);
            last_sent = nals[i].payload;
            if (!should_send_nal(t, len)) continue;
            int marker = ends_access_unit(nals, order, oi, send_count);
            if (send_nal_ring(fd, nals[i].payload, len, marker) < 0) return;
            if (marker) {
                g_ts += RTP_TS_INC;
                usleep(FRAME_SLEEP_US);
            }
        }
    }
}

static void handle_client(int fd) {
    char req[4096];
    for (;;) {
        ssize_t n = recv(fd, req, sizeof(req) - 1, 0);
        if (n <= 0) return;
        req[n] = 0;
        char cseq[32];
        cseq_of(req, cseq, sizeof(cseq));
        if (!strncmp(req, "OPTIONS", 7)) {
            reply(fd, cseq, "Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN\r\n", NULL);
        } else if (!strncmp(req, "DESCRIBE", 8)) {
            char sdp[1024];
            make_sdp(sdp, sizeof(sdp));
            char extra[256];
            snprintf(extra, sizeof(extra), "Content-Type: application/sdp\r\n");
            reply(fd, cseq, extra, sdp);
        } else if (!strncmp(req, "SETUP", 5)) {
            reply(fd, cseq, "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\nSession: 3918\r\n", NULL);
        } else if (!strncmp(req, "PLAY", 4)) {
            if (reply(fd, cseq, "Session: 3918\r\nRTP-Info: url=rtsp://0.0.0.0:554/track1\r\n", NULL) < 0) return;
            stream_loop(fd);
            return;
        } else {
            reply(fd, cseq, NULL, NULL);
        }
    }
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);
    g_shm = (unsigned char *)shmat(SHMID, NULL, SHM_RDONLY);
    if (g_shm == (void *)-1) {
        perror("shmat");
        return 1;
    }
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = INADDR_ANY;
    a.sin_port = htons(554);
    if (bind(srv, (struct sockaddr *)&a, sizeof(a)) < 0) {
        perror("bind");
        return 1;
    }
    if (listen(srv, 2) < 0) {
        perror("listen");
        return 1;
    }
    fprintf(stderr, "shm_rtsp listening on 554\n");
    for (;;) {
        int c = accept(srv, NULL, NULL);
        if (c < 0) continue;
        pid_t pid = fork();
        if (pid == 0) {
            close(srv);
            handle_client(c);
            close(c);
            _exit(0);
        }
        close(c);
    }
}
