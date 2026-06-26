#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/shm.h>

#define SHMID 0
#define SHM_BYTES 1048628
#define RING_BEGIN 52
#define RING_END 1048576
#define MAX_NALS 1024

struct item {
    size_t start;
    size_t payload;
    size_t end;
    int type;
};

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

static size_t before_pos(size_t p) {
    return p > RING_BEGIN ? p - 1 : RING_END - 1;
}

static unsigned char *g_shm;

static size_t ring_distance(size_t from, size_t to) {
    if (to >= from) return to - from;
    return (RING_END - from) + (to - RING_BEGIN);
}

static int collect(struct item *out, int max) {
    size_t starts[MAX_NALS];
    int scs[MAX_NALS];
    int count = 0;
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
        while (ring_distance(payload, end) > 0 && g_shm[before_pos(end)] == 0) {
            end = before_pos(end);
        }
        out[i].start = starts[i];
        out[i].payload = payload;
        out[i].end = end;
        out[i].type = g_shm[payload] & 0x1f;
    }
    return count;
}

int main(void) {
    g_shm = (unsigned char *)shmat(SHMID, NULL, SHM_RDONLY);
    if (g_shm == (void *)-1) {
        perror("shmat");
        return 1;
    }
    struct item it[MAX_NALS];
    int n = collect(it, MAX_NALS);
    unsigned counts[32] = {0};
    size_t min_len[32], max_len[32], sum_len[32];
    memset(min_len, 0xff, sizeof(min_len));
    memset(max_len, 0, sizeof(max_len));
    memset(sum_len, 0, sizeof(sum_len));
    for (int i = 0; i < n; i++) {
        int t = it[i].type;
        size_t len = ring_distance(it[i].payload, it[i].end);
        counts[t]++;
        if (len < min_len[t]) min_len[t] = len;
        if (len > max_len[t]) max_len[t] = len;
        sum_len[t] += len;
    }
    printf("nals=%d\n", n);
    for (int t = 0; t < 32; t++) {
        if (!counts[t]) continue;
        printf("type=%d count=%u min=%lu avg=%lu max=%lu\n",
               t, counts[t], (unsigned long)min_len[t],
               (unsigned long)(sum_len[t] / counts[t]), (unsigned long)max_len[t]);
    }
    printf("sequence:");
    for (int i = 0; i < n && i < 160; i++) {
        printf(" %d:%lu@%lu", it[i].type,
               (unsigned long)ring_distance(it[i].payload, it[i].end),
               (unsigned long)it[i].start);
    }
    printf("\n");
    return 0;
}
