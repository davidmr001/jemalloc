#include "test/jemalloc_test.h"

static nstime_monotonic_t *nstime_monotonic_orig;
static nstime_update_t *nstime_update_orig;

static unsigned nupdates_mock;
static nstime_t time_mock;
static bool monotonic_mock;

static bool
nstime_monotonic_mock(void) {
	return monotonic_mock;
}

static bool
nstime_update_mock(nstime_t *time) {
	nupdates_mock++;
	if (monotonic_mock) {
		nstime_copy(time, &time_mock);
	}
	return !monotonic_mock;
}

TEST_BEGIN(test_decay_ticks) {
	ticker_t *decay_ticker;
	unsigned tick0, tick1;
	size_t sz, large0;
	void *p;

	decay_ticker = decay_ticker_get(tsd_fetch(), 0);
	assert_ptr_not_null(decay_ticker,
	    "Unexpected failure getting decay ticker");

	sz = sizeof(size_t);
	assert_d_eq(mallctl("arenas.lextent.0.size", (void *)&large0, &sz, NULL,
	    0), 0, "Unexpected mallctl failure");

	/*
	 * Test the standard APIs using a large size class, since we can't
	 * control tcache interactions for small size classes (except by
	 * completely disabling tcache for the entire test program).
	 */

	/* malloc(). */
	tick0 = ticker_read(decay_ticker);
	p = malloc(large0);
	assert_ptr_not_null(p, "Unexpected malloc() failure");
	tick1 = ticker_read(decay_ticker);
	assert_u32_ne(tick1, tick0, "Expected ticker to tick during malloc()");
	/* free(). */
	tick0 = ticker_read(decay_ticker);
	free(p);
	tick1 = ticker_read(decay_ticker);
	assert_u32_ne(tick1, tick0, "Expected ticker to tick during free()");

	/* calloc(). */
	tick0 = ticker_read(decay_ticker);
	p = calloc(1, large0);
	assert_ptr_not_null(p, "Unexpected calloc() failure");
	tick1 = ticker_read(decay_ticker);
	assert_u32_ne(tick1, tick0, "Expected ticker to tick during calloc()");
	free(p);

	/* posix_memalign(). */
	tick0 = ticker_read(decay_ticker);
	assert_d_eq(posix_memalign(&p, sizeof(size_t), large0), 0,
	    "Unexpected posix_memalign() failure");
	tick1 = ticker_read(decay_ticker);
	assert_u32_ne(tick1, tick0,
	    "Expected ticker to tick during posix_memalign()");
	free(p);

	/* aligned_alloc(). */
	tick0 = ticker_read(decay_ticker);
	p = aligned_alloc(sizeof(size_t), large0);
	assert_ptr_not_null(p, "Unexpected aligned_alloc() failure");
	tick1 = ticker_read(decay_ticker);
	assert_u32_ne(tick1, tick0,
	    "Expected ticker to tick during aligned_alloc()");
	free(p);

	/* realloc(). */
	/* Allocate. */
	tick0 = ticker_read(decay_ticker);
	p = realloc(NULL, large0);
	assert_ptr_not_null(p, "Unexpected realloc() failure");
	tick1 = ticker_read(decay_ticker);
	assert_u32_ne(tick1, tick0, "Expected ticker to tick during realloc()");
	/* Reallocate. */
	tick0 = ticker_read(decay_ticker);
	p = realloc(p, large0);
	assert_ptr_not_null(p, "Unexpected realloc() failure");
	tick1 = ticker_read(decay_ticker);
	assert_u32_ne(tick1, tick0, "Expected ticker to tick during realloc()");
	/* Deallocate. */
	tick0 = ticker_read(decay_ticker);
	realloc(p, 0);
	tick1 = ticker_read(decay_ticker);
	assert_u32_ne(tick1, tick0, "Expected ticker to tick during realloc()");

	/*
	 * Test the *allocx() APIs using large and small size classes, with
	 * tcache explicitly disabled.
	 */
	{
		unsigned i;
		size_t allocx_sizes[2];
		allocx_sizes[0] = large0;
		allocx_sizes[1] = 1;

		for (i = 0; i < sizeof(allocx_sizes) / sizeof(size_t); i++) {
			sz = allocx_sizes[i];

			/* mallocx(). */
			tick0 = ticker_read(decay_ticker);
			p = mallocx(sz, MALLOCX_TCACHE_NONE);
			assert_ptr_not_null(p, "Unexpected mallocx() failure");
			tick1 = ticker_read(decay_ticker);
			assert_u32_ne(tick1, tick0,
			    "Expected ticker to tick during mallocx() (sz=%zu)",
			    sz);
			/* rallocx(). */
			tick0 = ticker_read(decay_ticker);
			p = rallocx(p, sz, MALLOCX_TCACHE_NONE);
			assert_ptr_not_null(p, "Unexpected rallocx() failure");
			tick1 = ticker_read(decay_ticker);
			assert_u32_ne(tick1, tick0,
			    "Expected ticker to tick during rallocx() (sz=%zu)",
			    sz);
			/* xallocx(). */
			tick0 = ticker_read(decay_ticker);
			xallocx(p, sz, 0, MALLOCX_TCACHE_NONE);
			tick1 = ticker_read(decay_ticker);
			assert_u32_ne(tick1, tick0,
			    "Expected ticker to tick during xallocx() (sz=%zu)",
			    sz);
			/* dallocx(). */
			tick0 = ticker_read(decay_ticker);
			dallocx(p, MALLOCX_TCACHE_NONE);
			tick1 = ticker_read(decay_ticker);
			assert_u32_ne(tick1, tick0,
			    "Expected ticker to tick during dallocx() (sz=%zu)",
			    sz);
			/* sdallocx(). */
			p = mallocx(sz, MALLOCX_TCACHE_NONE);
			assert_ptr_not_null(p, "Unexpected mallocx() failure");
			tick0 = ticker_read(decay_ticker);
			sdallocx(p, sz, MALLOCX_TCACHE_NONE);
			tick1 = ticker_read(decay_ticker);
			assert_u32_ne(tick1, tick0,
			    "Expected ticker to tick during sdallocx() "
			    "(sz=%zu)", sz);
		}
	}

	/*
	 * Test tcache fill/flush interactions for large and small size classes,
	 * using an explicit tcache.
	 */
	if (config_tcache) {
		unsigned tcache_ind, i;
		size_t tcache_sizes[2];
		tcache_sizes[0] = large0;
		tcache_sizes[1] = 1;

		sz = sizeof(unsigned);
		assert_d_eq(mallctl("tcache.create", (void *)&tcache_ind, &sz,
		    NULL, 0), 0, "Unexpected mallctl failure");

		for (i = 0; i < sizeof(tcache_sizes) / sizeof(size_t); i++) {
			sz = tcache_sizes[i];

			/* tcache fill. */
			tick0 = ticker_read(decay_ticker);
			p = mallocx(sz, MALLOCX_TCACHE(tcache_ind));
			assert_ptr_not_null(p, "Unexpected mallocx() failure");
			tick1 = ticker_read(decay_ticker);
			assert_u32_ne(tick1, tick0,
			    "Expected ticker to tick during tcache fill "
			    "(sz=%zu)", sz);
			/* tcache flush. */
			dallocx(p, MALLOCX_TCACHE(tcache_ind));
			tick0 = ticker_read(decay_ticker);
			assert_d_eq(mallctl("tcache.flush", NULL, NULL,
			    (void *)&tcache_ind, sizeof(unsigned)), 0,
			    "Unexpected mallctl failure");
			tick1 = ticker_read(decay_ticker);
			assert_u32_ne(tick1, tick0,
			    "Expected ticker to tick during tcache flush "
			    "(sz=%zu)", sz);
		}
	}
}
TEST_END

TEST_BEGIN(test_decay_ticker) {
#define NPS 1024
	int flags = (MALLOCX_ARENA(0) | MALLOCX_TCACHE_NONE);
	void *ps[NPS];
	uint64_t epoch;
	uint64_t npurge0 = 0;
	uint64_t npurge1 = 0;
	size_t sz, large;
	unsigned i, nupdates0;
	nstime_t time, decay_time, deadline;

	/*
	 * Allocate a bunch of large objects, pause the clock, deallocate the
	 * objects, restore the clock, then [md]allocx() in a tight loop to
	 * verify the ticker triggers purging.
	 */

	if (config_tcache) {
		size_t tcache_max;

		sz = sizeof(size_t);
		assert_d_eq(mallctl("arenas.tcache_max", (void *)&tcache_max,
		    &sz, NULL, 0), 0, "Unexpected mallctl failure");
		large = nallocx(tcache_max + 1, flags);
	}  else {
		sz = sizeof(size_t);
		assert_d_eq(mallctl("arenas.lextent.0.size", &large, &sz, NULL,
		    0), 0, "Unexpected mallctl failure");
	}

	assert_d_eq(mallctl("arena.0.purge", NULL, NULL, NULL, 0), 0,
	    "Unexpected mallctl failure");
	assert_d_eq(mallctl("epoch", NULL, NULL, (void *)&epoch,
	    sizeof(uint64_t)), 0, "Unexpected mallctl failure");
	sz = sizeof(uint64_t);
	assert_d_eq(mallctl("stats.arenas.0.npurge", (void *)&npurge0, &sz,
	    NULL, 0), config_stats ? 0 : ENOENT, "Unexpected mallctl result");

	for (i = 0; i < NPS; i++) {
		ps[i] = mallocx(large, flags);
		assert_ptr_not_null(ps[i], "Unexpected mallocx() failure");
	}

	nupdates_mock = 0;
	nstime_init(&time_mock, 0);
	nstime_update(&time_mock);
	monotonic_mock = true;

	nstime_monotonic_orig = nstime_monotonic;
	nstime_update_orig = nstime_update;
	nstime_monotonic = nstime_monotonic_mock;
	nstime_update = nstime_update_mock;

	for (i = 0; i < NPS; i++) {
		dallocx(ps[i], flags);
		nupdates0 = nupdates_mock;
		assert_d_eq(mallctl("arena.0.decay", NULL, NULL, NULL, 0), 0,
		    "Unexpected arena.0.decay failure");
		assert_u_gt(nupdates_mock, nupdates0,
		    "Expected nstime_update() to be called");
	}

	nstime_monotonic = nstime_monotonic_orig;
	nstime_update = nstime_update_orig;

	nstime_init(&time, 0);
	nstime_update(&time);
	nstime_init2(&decay_time, opt_decay_time, 0);
	nstime_copy(&deadline, &time);
	nstime_add(&deadline, &decay_time);
	do {
		for (i = 0; i < DECAY_NTICKS_PER_UPDATE / 2; i++) {
			void *p = mallocx(1, flags);
			assert_ptr_not_null(p, "Unexpected mallocx() failure");
			dallocx(p, flags);
		}
		assert_d_eq(mallctl("epoch", NULL, NULL, (void *)&epoch,
		    sizeof(uint64_t)), 0, "Unexpected mallctl failure");
		sz = sizeof(uint64_t);
		assert_d_eq(mallctl("stats.arenas.0.npurge", (void *)&npurge1,
		    &sz, NULL, 0), config_stats ? 0 : ENOENT,
		    "Unexpected mallctl result");

		nstime_update(&time);
	} while (nstime_compare(&time, &deadline) <= 0 && npurge1 == npurge0);

	if (config_stats) {
		assert_u64_gt(npurge1, npurge0, "Expected purging to occur");
	}
#undef NPS
}
TEST_END

TEST_BEGIN(test_decay_nonmonotonic) {
#define NPS (SMOOTHSTEP_NSTEPS + 1)
	int flags = (MALLOCX_ARENA(0) | MALLOCX_TCACHE_NONE);
	void *ps[NPS];
	uint64_t epoch;
	uint64_t npurge0 = 0;
	uint64_t npurge1 = 0;
	size_t sz, large0;
	unsigned i, nupdates0;

	sz = sizeof(size_t);
	assert_d_eq(mallctl("arenas.lextent.0.size", (void *)&large0, &sz, NULL,
	    0), 0, "Unexpected mallctl failure");

	assert_d_eq(mallctl("arena.0.purge", NULL, NULL, NULL, 0), 0,
	    "Unexpected mallctl failure");
	assert_d_eq(mallctl("epoch", NULL, NULL, (void *)&epoch,
	    sizeof(uint64_t)), 0, "Unexpected mallctl failure");
	sz = sizeof(uint64_t);
	assert_d_eq(mallctl("stats.arenas.0.npurge", (void *)&npurge0, &sz,
	    NULL, 0), config_stats ? 0 : ENOENT, "Unexpected mallctl result");

	nupdates_mock = 0;
	nstime_init(&time_mock, 0);
	nstime_update(&time_mock);
	monotonic_mock = false;

	nstime_monotonic_orig = nstime_monotonic;
	nstime_update_orig = nstime_update;
	nstime_monotonic = nstime_monotonic_mock;
	nstime_update = nstime_update_mock;

	for (i = 0; i < NPS; i++) {
		ps[i] = mallocx(large0, flags);
		assert_ptr_not_null(ps[i], "Unexpected mallocx() failure");
	}

	for (i = 0; i < NPS; i++) {
		dallocx(ps[i], flags);
		nupdates0 = nupdates_mock;
		assert_d_eq(mallctl("arena.0.decay", NULL, NULL, NULL, 0), 0,
		    "Unexpected arena.0.decay failure");
		assert_u_gt(nupdates_mock, nupdates0,
		    "Expected nstime_update() to be called");
	}

	assert_d_eq(mallctl("epoch", NULL, NULL, (void *)&epoch,
	    sizeof(uint64_t)), 0, "Unexpected mallctl failure");
	sz = sizeof(uint64_t);
	assert_d_eq(mallctl("stats.arenas.0.npurge", (void *)&npurge1, &sz,
	    NULL, 0), config_stats ? 0 : ENOENT, "Unexpected mallctl result");

	if (config_stats) {
		assert_u64_eq(npurge0, npurge1, "Unexpected purging occurred");
	}

	nstime_monotonic = nstime_monotonic_orig;
	nstime_update = nstime_update_orig;
#undef NPS
}
TEST_END

static unsigned
do_arena_create(ssize_t decay_time) {
	unsigned arena_ind;
	size_t sz = sizeof(unsigned);
	assert_d_eq(mallctl("arenas.create", (void *)&arena_ind, &sz, NULL, 0),
	    0, "Unexpected mallctl() failure");
	size_t mib[3];
	size_t miblen = sizeof(mib)/sizeof(size_t);
	assert_d_eq(mallctlnametomib("arena.0.decay_time", mib, &miblen), 0,
	    "Unexpected mallctlnametomib() failure");
	mib[1] = (size_t)arena_ind;
	assert_d_eq(mallctlbymib(mib, miblen, NULL, NULL, (void *)&decay_time,
	    sizeof(decay_time)), 0, "Unexpected mallctlbymib() failure");
	return arena_ind;
}

static void
do_arena_destroy(unsigned arena_ind) {
	size_t mib[3];
	size_t miblen = sizeof(mib)/sizeof(size_t);
	assert_d_eq(mallctlnametomib("arena.0.destroy", mib, &miblen), 0,
	    "Unexpected mallctlnametomib() failure");
	mib[1] = (size_t)arena_ind;
	assert_d_eq(mallctlbymib(mib, miblen, NULL, NULL, NULL, 0), 0,
	    "Unexpected mallctlbymib() failure");
}

void
do_epoch(void) {
	uint64_t epoch = 1;
	assert_d_eq(mallctl("epoch", NULL, NULL, (void *)&epoch, sizeof(epoch)),
	    0, "Unexpected mallctl() failure");
}

static size_t
get_arena_pdirty(unsigned arena_ind) {
	do_epoch();
	size_t mib[4];
	size_t miblen = sizeof(mib)/sizeof(size_t);
	assert_d_eq(mallctlnametomib("stats.arenas.0.pdirty", mib, &miblen), 0,
	    "Unexpected mallctlnametomib() failure");
	mib[2] = (size_t)arena_ind;
	size_t pdirty;
	size_t sz = sizeof(pdirty);
	assert_d_eq(mallctlbymib(mib, miblen, (void *)&pdirty, &sz, NULL, 0), 0,
	    "Unexpected mallctlbymib() failure");
	return pdirty;
}

static void *
do_mallocx(size_t size, int flags) {
	void *p = mallocx(size, flags);
	assert_ptr_not_null(p, "Unexpected mallocx() failure");
	return p;
}

static void
generate_dirty(unsigned arena_ind, size_t size) {
	int flags = MALLOCX_ARENA(arena_ind) | MALLOCX_TCACHE_NONE;
	void *p = do_mallocx(size, flags);
	dallocx(p, flags);
}

TEST_BEGIN(test_decay_now) {
	unsigned arena_ind = do_arena_create(0);
	assert_zu_eq(get_arena_pdirty(arena_ind), 0, "Unexpected dirty pages");
	size_t sizes[] = {16, PAGE<<2, HUGEPAGE<<2};
	/* Verify that dirty pages never linger after deallocation. */
	for (unsigned i = 0; i < sizeof(sizes)/sizeof(size_t); i++) {
		size_t size = sizes[i];
		generate_dirty(arena_ind, size);
		assert_zu_eq(get_arena_pdirty(arena_ind), 0,
		    "Unexpected dirty pages");
	}
	do_arena_destroy(arena_ind);
}
TEST_END

TEST_BEGIN(test_decay_never) {
	unsigned arena_ind = do_arena_create(-1);
	int flags = MALLOCX_ARENA(arena_ind) | MALLOCX_TCACHE_NONE;
	assert_zu_eq(get_arena_pdirty(arena_ind), 0, "Unexpected dirty pages");
	size_t sizes[] = {16, PAGE<<2, HUGEPAGE<<2};
	void *ptrs[sizeof(sizes)/sizeof(size_t)];
	for (unsigned i = 0; i < sizeof(sizes)/sizeof(size_t); i++) {
		ptrs[i] = do_mallocx(sizes[i], flags);
	}
	/* Verify that each deallocation generates additional dirty pages. */
	size_t pdirty_prev = get_arena_pdirty(arena_ind);
	assert_zu_eq(pdirty_prev, 0, "Unexpected dirty pages");
	for (unsigned i = 0; i < sizeof(sizes)/sizeof(size_t); i++) {
		dallocx(ptrs[i], flags);
		size_t pdirty = get_arena_pdirty(arena_ind);
		assert_zu_gt(pdirty, pdirty_prev,
		    "Expected dirty pages to increase.");
		pdirty_prev = pdirty;
	}
	do_arena_destroy(arena_ind);
}
TEST_END

int
main(void) {
	return test(
	    test_decay_ticks,
	    test_decay_ticker,
	    test_decay_nonmonotonic,
	    test_decay_now,
	    test_decay_never);
}
