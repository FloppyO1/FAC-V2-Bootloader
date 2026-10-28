/* Tiny assertion helper for the host tests */
#ifndef TEST_H
#define TEST_H

#include <stdio.h>

extern int t_fail;			// failed checks
extern int t_run;			// tests run
extern int t_run_failed;	// tests with at least one failed check
void sim_reset(void);

#define CHECK(cond) do { \
		if (!(cond)) { printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); t_fail++; } \
	} while (0)

#define CHECK_EQ(a, b) do { \
		unsigned long long a_ = (unsigned long long) (a), b_ = (unsigned long long) (b); \
		if (a_ != b_) { \
			printf("%s:%d: CHECK_EQ failed: %s (0x%llX) != %s (0x%llX)\n", __FILE__, __LINE__, #a, a_, #b, b_); \
			t_fail++; \
		} \
	} while (0)

/* Runs one test on a freshly reset simulation */
#define RUN(fn) do { \
		int before_ = t_fail; \
		sim_reset(); \
		fn(); \
		t_run++; \
		if (t_fail != before_) { printf("FAILED: %s\n", #fn); t_run_failed++; } \
	} while (0)

#define TEST_FINISH() ( \
		printf("%d tests, %d failed (%d checks failed)\n", t_run, t_run_failed, t_fail), \
		t_fail ? 1 : 0)

#endif /* TEST_H */
