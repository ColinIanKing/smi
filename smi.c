#include <sys/io.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <fcntl.h>
#include <limits.h>

#define MSR_SMI_COUNT   (0x00000034)

static inline int cpu_has_msr(void)
{
	uint32_t edx;

	asm("cpuid" : "=d" (edx));

	return edx & (1 << 5);
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

int main(void)
{
	if ((getuid() != 0) || (geteuid() != 0)) {
		fprintf(stderr, "Need to run as root.\n");
		exit(EXIT_FAILURE);
	}

	if (!cpu_has_msr()) {
		fprintf(stderr, "CPU does not have MSRs\n");
		exit(EXIT_FAILURE);
	}

	if (ioperm(0xb2, 2, 1) < 0) {
		fprintf(stderr, "Cannot access port 0xb2\n");
		exit(EXIT_FAILURE);
	}
	for (;;) {
		int i;
		uint64_t smicount;
		for (i = 0; i < 1024; i++) {
			outb(1, 0xb2);
		}
		readmsr(0, MSR_SMI_COUNT, &smicount);
		printf("SMI count: %" PRIu64 "\r", smicount);
		fflush(stdout);
	}
	return EXIT_SUCCESS;
}
