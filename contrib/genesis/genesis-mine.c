// Find a nonce for the BitFinite testnet genesis header.
//
// Only nTime and nNonce change: the merkle root comes from the coinbase, which
// does not contain the timestamp, so it is a constant here and is passed in.
//
// The first 64 bytes of the 80-byte header (version, prevhash, and 28 bytes of
// the merkle root) never change, so their SHA256 state is computed once and
// copied per attempt. That is the whole optimisation; it roughly halves the work.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <openssl/sha.h>

static uint8_t hdr64[64];
static uint8_t tail16[16];
static SHA256_CTX mid;
static volatile int found = 0;
static volatile uint32_t answer = 0;
static int nthreads;

static void hex2bin_rev(const char *h, uint8_t *out, int n) {
    for (int i = 0; i < n; i++) {
        unsigned v; sscanf(h + 2*i, "%2x", &v);
        out[n - 1 - i] = (uint8_t)v;   // display hex is big-endian; header wants LE
    }
}

static void *worker(void *arg) {
    long id = (long)arg;
    uint8_t t[16]; memcpy(t, tail16, 16);
    SHA256_CTX c; uint8_t d1[32], d2[32];
    for (uint64_t n = (uint64_t)id; n <= 0xFFFFFFFFull && !found; n += nthreads) {
        uint32_t nonce = (uint32_t)n;
        memcpy(t + 12, &nonce, 4);
        c = mid;
        SHA256_Update(&c, t, 16);
        SHA256_Final(d1, &c);
        SHA256_Init(&c); SHA256_Update(&c, d1, 32); SHA256_Final(d2, &c);
        // nBits 0x1d00ffff => the four most significant bytes of the displayed
        // hash must be zero, i.e. the last four bytes of the raw digest.
        if (d2[31] == 0 && d2[30] == 0 && d2[29] == 0 && d2[28] == 0) {
            found = 1; answer = nonce;
            printf("nonce=%u hash=", nonce);
            for (int i = 31; i >= 0; i--) printf("%02x", d2[i]);
            printf("\n");
            return NULL;
        }
    }
    return NULL;
}

int main(int argc, char **argv) {
    // argv: <merkle-hex-display> <ntime> <nbits> <version> <threads> [fixed-nonce]
    if (argc < 6) { fprintf(stderr, "usage: %s merkle ntime nbits version threads [nonce]\n", argv[0]); return 2; }
    uint8_t merkle[32]; hex2bin_rev(argv[1], merkle, 32);
    uint32_t ntime = (uint32_t)strtoul(argv[2], NULL, 10);
    uint32_t nbits = (uint32_t)strtoul(argv[3], NULL, 16);
    uint32_t ver   = (uint32_t)strtoul(argv[4], NULL, 10);
    nthreads       = atoi(argv[5]);

    memset(hdr64, 0, 64);
    memcpy(hdr64, &ver, 4);                 // version
    // prevhash stays all zero (genesis)
    memcpy(hdr64 + 36, merkle, 28);         // first 28 bytes of merkle root
    memcpy(tail16, merkle + 28, 4);         // last 4 bytes of merkle root
    memcpy(tail16 + 4, &ntime, 4);
    memcpy(tail16 + 8, &nbits, 4);

    SHA256_Init(&mid); SHA256_Update(&mid, hdr64, 64);

    if (argc >= 7) {                        // verify a known nonce instead of searching
        uint32_t nonce = (uint32_t)strtoul(argv[6], NULL, 10);
        uint8_t t[16]; memcpy(t, tail16, 16); memcpy(t + 12, &nonce, 4);
        SHA256_CTX c = mid; uint8_t d1[32], d2[32];
        SHA256_Update(&c, t, 16); SHA256_Final(d1, &c);
        SHA256_Init(&c); SHA256_Update(&c, d1, 32); SHA256_Final(d2, &c);
        printf("verify nonce=%u hash=", nonce);
        for (int i = 31; i >= 0; i--) printf("%02x", d2[i]);
        printf("\n");
        return 0;
    }

    pthread_t th[64];
    for (long i = 0; i < nthreads; i++) pthread_create(&th[i], NULL, worker, (void*)i);
    for (long i = 0; i < nthreads; i++) pthread_join(th[i], NULL);
    return found ? 0 : 1;
}
