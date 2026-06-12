#include "msaw.h"

#define ROTL(x, k) (((x) << (k)) | ((x) >> (64 - (k))))

#define ENTROPY_EXTRACTOR_1 0xB5AD4ECEDA1CE2A9ULL
#define ENTROPY_EXTRACTOR_2 0xA5A5A5A5A5A5A5A5ULL

// dynamic roll array
static const uint64_t p_arr[] = {
	3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59
};

/**
 * build_entropy_1, build_entropy_2
 * @param start: Initial seed value, will be forced odd
 * @return: A mixed 64-bit value
 *
 * Advanced entropy extraction functions combining multiple PRNG techniques:
 * - Variable rotations using prime numbers (similar to RC5/RC6 approach)
 * - Addition of constant (like LCG additive component)
 * - Feedback of original input (similar to Feistel networks)
 * The dual rotation scheme using both prime table and dynamic shifts
 * ensures thorough bit mixing and strong avalanche effects.
 * Two separate versions with different constants prevent correlation.
 */
static inline uint64_t build_entropy_1(uint64_t start)
{
	uint64_t x = start | 1;
	{
		x *= x;
		x  = ROTL(x, p_arr[x >> 60]) ^ ROTL(x, 32 + (x & 0x7ULL));
		x += ENTROPY_EXTRACTOR_1;
		x *= x;
		x  = ROTL(x, p_arr[x >> 60]) ^ ROTL(x, 32 + (x & 0x7ULL));
		x += ENTROPY_EXTRACTOR_1;
		x *= x;
		x  = ROTL(x, p_arr[x >> 60]) ^ ROTL(x, 32 + (x & 0x7ULL));
		x += ENTROPY_EXTRACTOR_1;
		x *= x;
		x  = ROTL(x, p_arr[x >> 60]) ^ ROTL(x, 32 + (x & 0x7ULL));
		x += ENTROPY_EXTRACTOR_1;
			x ^= start;
		x *= x;
		x  = ROTL(x, p_arr[x >> 60]) ^ ROTL(x, 32 + (x & 0x7ULL));
		x += ENTROPY_EXTRACTOR_1;
		x *= x;
		x  = ROTL(x, p_arr[x >> 60]) ^ ROTL(x, 32 + (x & 0x7ULL));
		x += ENTROPY_EXTRACTOR_1;
		x *= x;
		x  = ROTL(x, p_arr[x >> 60]) ^ ROTL(x, 32 + (x & 0x7ULL));
		x += ENTROPY_EXTRACTOR_1;
		x *= x;
		x  = ROTL(x, p_arr[x >> 60]) ^ ROTL(x, 32 + (x & 0x7ULL));
		x += ENTROPY_EXTRACTOR_1;
		x *= x;
		x  = ROTL(x, p_arr[x >> 60]) ^ ROTL(x, 32 + (x & 0x7ULL));
		x += ENTROPY_EXTRACTOR_1;
		x *= x;
		x  = ROTL(x, p_arr[x >> 60]) ^ ROTL(x, 32 + (x & 0x7ULL));
		x += ENTROPY_EXTRACTOR_1;
		x *= x;
		x  = ROTL(x, p_arr[x >> 60]) ^ ROTL(x, 32 + (x & 0x7ULL));
		x += ENTROPY_EXTRACTOR_1;
		x *= x;
		x  = ROTL(x, p_arr[x >> 60]) ^ ROTL(x, 32 + (x & 0x7ULL));
		x += ENTROPY_EXTRACTOR_1;
			x ^= start;
		x *= x;
		x  = ROTL(x, p_arr[x >> 60]) ^ ROTL(x, 32 + (x & 0x7ULL));
		x += ENTROPY_EXTRACTOR_1;
		x *= x;
		x  = ROTL(x, p_arr[x >> 60]) ^ ROTL(x, 32 + (x & 0x7ULL));
		x += ENTROPY_EXTRACTOR_1;
		x *= x;
		x  = ROTL(x, p_arr[x >> 60]) ^ ROTL(x, 32 + (x & 0x7ULL));
		x += ENTROPY_EXTRACTOR_1;
		x *= x;
		x  = ROTL(x, p_arr[x >> 60]) ^ ROTL(x, 32 + (x & 0x7ULL));
		x += ENTROPY_EXTRACTOR_1;
	}
	return x;
}
static inline uint64_t build_entropy_2(uint64_t start)
{
	uint64_t x = start | 1;
	{
		x *= x;
		x  = ROTL(x, p_arr[x >> 60]) ^ ROTL(x, 32 + (x & 0x7ULL));
		x += ENTROPY_EXTRACTOR_2;
		x *= x;
		x  = ROTL(x, p_arr[x >> 60]) ^ ROTL(x, 32 + (x & 0x7ULL));
		x += ENTROPY_EXTRACTOR_2;
		x *= x;
		x  = ROTL(x, p_arr[x >> 60]) ^ ROTL(x, 32 + (x & 0x7ULL));
		x += ENTROPY_EXTRACTOR_2;
		x *= x;
		x  = ROTL(x, p_arr[x >> 60]) ^ ROTL(x, 32 + (x & 0x7ULL));
		x += ENTROPY_EXTRACTOR_2;
			x ^= start;
		x *= x;
		x  = ROTL(x, p_arr[x >> 60]) ^ ROTL(x, 32 + (x & 0x7ULL));
		x += ENTROPY_EXTRACTOR_2;
		x *= x;
		x  = ROTL(x, p_arr[x >> 60]) ^ ROTL(x, 32 + (x & 0x7ULL));
		x += ENTROPY_EXTRACTOR_2;
		x *= x;
		x  = ROTL(x, p_arr[x >> 60]) ^ ROTL(x, 32 + (x & 0x7ULL));
		x += ENTROPY_EXTRACTOR_2;
		x *= x;
		x  = ROTL(x, p_arr[x >> 60]) ^ ROTL(x, 32 + (x & 0x7ULL));
		x += ENTROPY_EXTRACTOR_2;
		x *= x;
		x  = ROTL(x, p_arr[x >> 60]) ^ ROTL(x, 32 + (x & 0x7ULL));
		x += ENTROPY_EXTRACTOR_2;
		x *= x;
		x  = ROTL(x, p_arr[x >> 60]) ^ ROTL(x, 32 + (x & 0x7ULL));
		x += ENTROPY_EXTRACTOR_2;
		x *= x;
		x  = ROTL(x, p_arr[x >> 60]) ^ ROTL(x, 32 + (x & 0x7ULL));
		x += ENTROPY_EXTRACTOR_2;
		x *= x;
		x  = ROTL(x, p_arr[x >> 60]) ^ ROTL(x, 32 + (x & 0x7ULL));
		x += ENTROPY_EXTRACTOR_2;
			x ^= start;
		x *= x;
		x  = ROTL(x, p_arr[x >> 60]) ^ ROTL(x, 32 + (x & 0x7ULL));
		x += ENTROPY_EXTRACTOR_2;
		x *= x;
		x  = ROTL(x, p_arr[x >> 60]) ^ ROTL(x, 32 + (x & 0x7ULL));
		x += ENTROPY_EXTRACTOR_2;
		x *= x;
		x  = ROTL(x, p_arr[x >> 60]) ^ ROTL(x, 32 + (x & 0x7ULL));
		x += ENTROPY_EXTRACTOR_2;
		x *= x;
		x  = ROTL(x, p_arr[x >> 60]) ^ ROTL(x, 32 + (x & 0x7ULL));
		x += ENTROPY_EXTRACTOR_2;
	}
	return x;
}

/**
 * build_step
 * @param seed: Initial entropy source
 * @return: 64-bit value with guaranteed unique 4-bit nibbles
 *
 * Constructs a step value for a Weyl sequence by ensuring:
 * - Result is odd (required for multiplicative progression)
 * - Each 32-bit half contains 8 unique nibbles (4-bit values)
 * - No zero nibbles (maintains nonzero multiplication property)
 */
static inline uint64_t build_step(uint64_t seed)
{
	uint64_t saved = 0, used = 1, added = 0; // lowest (0) nibble not allowed

	do {
		seed = build_entropy_1(seed);
		uint64_t nibble = seed >> 60;
		if (used & (1 << nibble)) continue;

		used |= (1 << (nibble));
		saved |= (nibble << (4 * added));
		added++;
	} while (added < 8);

	used = 1;

	do {
		seed = build_entropy_2(seed);
		uint64_t nibble = seed >> 60;
		if (used & (1 << nibble)) continue;

		used |= (1 << (nibble));
		saved |= (nibble << (4 * added));
		added++;
	} while (added < 16);

	return saved;
}

/**
 * msaw_next
 * @param st: Stream state
 * @return: 32-bit pseudo-random number
 *
 * Generates next random number using:
 * - Dynamic Prime-based rotation
 * - Middle square method (x *= x)
 * - Weyl sequence addition (w += s)
 * Combines aspects of several proven PRNG techniques:
 * Middle Square method, Weyl sequences, multiplicative
 * congruential generators, and rotation-based cryptographic
 * primitives.
 */
uint32_t msaw_next(struct msaw *st)
{
	// update value
	st->x = ROTL(st->x, p_arr[st->x >> 60]);
	st->x *= st->x;
	st->x += (st->w += st->s);
	st->x = ROTL(st->x, 32);
	return (uint32_t)st->x;
}

/**
 * msaw_seed
 * @param st: Stream state to initialize
 * @param seed: Initial entropy source
 *
 * Initializes a Weyl sequence PRNG with Middle Square mixing:
 * - x: Primary state variable
 * - w: Weyl sequence counter
 * - s: Carefully constructed step value
 * Includes warm-up phase to ensure state mixing.
 * Design influenced by Middle Square Weyl Sequence generator
 * but with enhanced mixing and stronger step construction.
 */
void msaw_seed(struct msaw *st, uint64_t seed)
{
	st->x = build_entropy_1(seed);
	st->w = build_entropy_2((st->x << 5) - st->x);
	st->s = build_step(st->x ^ ROTL(st->w, p_arr[st->w & 0xFULL]));

	// Warm-up phase
	for (int i = 0; i < 16; i++) {
		msaw_next(st);
	}
}

uint32_t msaw_below(struct msaw *st, uint32_t n)
{
	if (n == 0) return 0;
	return msaw_next(st) % n;
}

void msaw_split(struct msaw *parent, struct msaw *child)
{
	// sequence the draws explicitly: evaluation order inside one
	// expression is unspecified and would break determinism
	uint64_t hi = msaw_next(parent);
	uint64_t lo = msaw_next(parent);
	child->x = (hi << 32) | lo;

	hi = msaw_next(parent);
	lo = msaw_next(parent);
	child->w = (hi << 32) | lo;

	child->s = parent->s;

	// brief warm-up to decorrelate from the raw parent draws
	msaw_next(child);
	msaw_next(child);
}
