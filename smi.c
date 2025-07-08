/*
 * Copyright (C) 2021 Canonical
 * Copyright (C) 2021-2025 Colin Ian King
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */
#include <sys/io.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <fcntl.h>
#include <limits.h>
#include <cpuid.h>

#define MSR_SMI_COUNT   (0x00000034)
#define ITERATIONS	(1000)

static inline int cpu_has_msr(void)
{
	uint32_t a, b, c, d;

	__get_cpuid(1, &a, &b, &c, &d);
	return d & (1 << 5);

}

static inline int cpu_has_tsc(void)
{
	uint32_t a, b, c, d;

	__get_cpuid(1, &a, &b, &c, &d);
	return d & (1 << 4);
}

static inline void cpu_brand(void)
{
	uint32_t brand[12];

	if (!__get_cpuid_max(0x80000004, NULL))
		return;

	__get_cpuid(0x80000002, brand+0x0, brand+0x1, brand+0x2, brand+0x3);
	__get_cpuid(0x80000003, brand+0x4, brand+0x5, brand+0x6, brand+0x7);
	__get_cpuid(0x80000004, brand+0x8, brand+0x9, brand+0xa, brand+0xb);

	printf("CPU: %s\n", (char *)brand);
}

static int readmsr(const int cpu, const uint32_t reg, uint64_t *val)
{
	char buffer[PATH_MAX];
	uint64_t value = 0;
	int fd;
	int ret;

	*val = ~0;
	snprintf(buffer, sizeof(buffer), "/dev/cpu/%d/msr", cpu);
	if ((fd = open(buffer, O_RDONLY)) < 0) {
		if (system("modprobe msr") < 0)
			return -1;
		if ((fd = open(buffer, O_RDONLY)) < 0)
			return -1;
	}
	ret = pread(fd, &value, 8, reg);
	(void)close(fd);
	if (ret < 0)
		return -1;

	*val = value;
	return 0;
}

static inline uint64_t rdtsc(void)
{
	uint64_t tsc;

	if (sizeof(long) == sizeof(uint64_t)) {
		uint32_t lo, hi;

		asm volatile("rdtsc" : "=a" (lo), "=d" (hi));

		return ((uint64_t)(hi) << 32) | lo;
	} else {
		asm volatile("rdtsc" : "=A" (tsc));
	}
	return tsc;
}

static inline double timeval_to_double(const struct timeval *tv)
{
        return (double)tv->tv_sec + ((double)tv->tv_usec / 1000000.0);
}

static double time_now(void)
{
        struct timeval now;

        if (gettimeofday(&now, NULL) < 0)
                return -1.0;

        return timeval_to_double(&now);
}

int main(void)
{
	uint64_t t1, t2, dt = 0, ticks, iterations = 0;
	double d1, d2, secs, ticks_per_sec;

	if ((getuid() != 0) || (geteuid() != 0)) {
		fprintf(stderr, "Need to run as root.\n");
		exit(EXIT_FAILURE);
	}
	cpu_brand();

	if (!cpu_has_tsc()) {
		fprintf(stderr, "CPU does not have rdtsc\n");
		exit(EXIT_FAILURE);
	}

	if (!cpu_has_msr()) {
		fprintf(stderr, "CPU does not have MSRs\n");
		exit(EXIT_FAILURE);
	}
	printf("Estimating TSC ticks per second..\n");
	t1 = rdtsc();
	d1 = time_now();
	sleep(5);
	t2 = rdtsc();
	d2 = time_now();

	secs = d2 - d1;
	ticks = t2 - t1;

	ticks_per_sec = ticks / secs;
	printf("TSC %f ticks per second\n", ticks_per_sec);

	if (ioperm(0xb2, 2, 1) < 0) {
		fprintf(stderr, "Cannot access port 0xb2\n");
		exit(EXIT_FAILURE);
	}

	printf("hit control-c to stop..\n");

	for (;;) {
		int i;
		uint64_t smicount;
		double smi_ticks, smi_usecs, smi_rate;

		for (i = 0; i < ITERATIONS; i++) {
			t1 = rdtsc();
			outb(1, 0xb2);
			t2 = rdtsc();
			dt += (t2 - t1);
		}
		iterations += ITERATIONS;
		smi_ticks = (double)(dt) / iterations;
		smi_usecs = 1000000 * smi_ticks / ticks_per_sec;
		smi_rate = ticks_per_sec / smi_ticks;
		readmsr(0, MSR_SMI_COUNT, &smicount);

		printf("SMI count: %" PRIu64 ": %.2f TSC ticks per smi (%.2f us) (%.2f SMIs/sec)\n", smicount, smi_ticks, smi_usecs, smi_rate);
		fflush(stdout);
	}
	return EXIT_SUCCESS;
}
