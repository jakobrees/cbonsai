#ifndef MSAW_H
#define MSAW_H

#include <stdint.h>

/*
 * msaw — Middle Square Weyl sequence PRNG (reentrant struct API).
 *
 * Each struct msaw is an independent, deterministic stream. Streams are
 * cheap to advance (msaw_next), expensive to seed from scratch (msaw_seed,
 * which runs the full entropy/step construction), and cheap to fork
 * (msaw_split, which derives a child stream from parent draws).
 */
struct msaw {
	uint64_t x;	/* primary state */
	uint64_t w;	/* Weyl sequence counter */
	uint64_t s;	/* Weyl step (odd, unique nibbles) */
};

/* Initialize a stream from a seed. Includes warm-up; relatively expensive. */
void msaw_seed(struct msaw *st, uint64_t seed);

/* Next 32-bit pseudo-random value. */
uint32_t msaw_next(struct msaw *st);

/* Uniform-ish draw in [0, n). Returns 0 when n is 0 (no draw consumed). */
uint32_t msaw_below(struct msaw *st, uint32_t n);

/*
 * Derive a child stream from a parent without a full reseed: child x and w
 * come from parent draws, the Weyl step s is shared (same step, different
 * phase). Cheap enough to call per leaf-walker spawn.
 */
void msaw_split(struct msaw *parent, struct msaw *child);

#endif /* MSAW_H */
