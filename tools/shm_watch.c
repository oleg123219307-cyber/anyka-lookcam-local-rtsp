#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/shm.h>
#include <unistd.h>

#define SHMID 0
#define SHM_BYTES 1048628
#define PAYLOAD_OFF 52
#define MAX_NALS 512

struct nal {
    size_t start;
    size_t payload;
    size_t end;
    int type;
};

static uint32_t rd32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

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

static int collect_nals(const unsigned char *shm, struct nal *out, int max) {
    int count = 0;
    size_t starts[MAX_NALS];
    int scs[MAX_NALS];
    for (size_t i = PAYLOAD_OFF; i + 5 < SHM_BYTES && count < max; i++) {
        int sc = 0;
        if (!find_start(shm, SHM_BYTES, i, &sc)) continue;
        starts[count] = i;
        scs[count] = sc;
        count++;
        i += (size_t)sc;
    }
    for (int i = 0; i < count; i++) {
        size_t payload = starts[i] + (size_t)scs[i];
        size_t end = (i + 1 < count) ? starts[i + 1] : SHM_BYTES;
        while (end > payload && shm[end - 1] == 0) end--;
        out[i].start = starts[i];
        out[i].payload = payload;
        out[i].end = end;
        out[i].type = shm[payload] & 0x1f;
    }
    return count;
}

static void print_near(struct nal *nals, int count, size_t pos) {
    int best = -1;
    size_t bestd = (size_t)-1;
    for (int i = 0; i < count; i++) {
        size_t p = nals[i].start;
        size_t d = p > pos ? p - pos : pos - p;
        if (d < bestd) {
            bestd = d;
            best = i;
        }
    }
    printf(" near%06lu:", (unsigned long)pos);
    if (best < 0) {
        printf(" none");
        return;
    }
    int a = best - 3;
    int b = best + 3;
    if (a < 0) a = 0;
    if (b >= count) b = count - 1;
    for (int i = a; i <= b; i++) {
        printf(" %d@%lu", nals[i].type, (unsigned long)nals[i].start);
    }
}

int main(int argc, char **argv) {
    int loops = argc > 1 ? atoi(argv[1]) : 20;
    int delay_ms = argc > 2 ? atoi(argv[2]) : 500;
    unsigned char *shm = (unsigned char *)shmat(SHMID, NULL, SHM_RDONLY);
    if (shm == (void *)-1) {
        perror("shmat");
        return 1;
    }
    for (int k = 0; k < loops; k++) {
        struct nal nals[MAX_NALS];
        int count = collect_nals(shm, nals, MAX_NALS);
        printf("%02d hdr", k);
        for (int off = 0; off < 52; off += 4) {
            uint32_t v = rd32(shm + off);
            printf(" %02d=%08lx/%06lu", off, (unsigned long)v, (unsigned long)(v & 0x000fffff));
        }
        printf(" nals=%d", count);
        if (count > 0) {
            printf(" first=%d@%lu last=%d@%lu", nals[0].type, (unsigned long)nals[0].start, nals[count - 1].type, (unsigned long)nals[count - 1].start);
        }
        print_near(nals, count, rd32(shm + 4) & 0x000fffff);
        print_near(nals, count, rd32(shm + 8) & 0x000fffff);
        print_near(nals, count, rd32(shm + 12) & 0x000fffff);
        printf("\n");
        fflush(stdout);
        usleep((useconds_t)delay_ms * 1000);
    }
    return 0;
}
