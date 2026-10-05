/* честный читатель: без сборщика мусора и питона */
#define _GNU_SOURCE
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>

static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

static int cmp(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return x < y ? -1 : x > y;
}

int main(int argc, char **argv)
{
	int n = argc > 1 ? atoi(argv[1]) : 200;
	size_t fs = 1024 * 768 * 2;
	unsigned char *b = malloc(fs);
	double *d = malloc(sizeof(double) * n), prev = 0;
	int fd = open("/dev/video0", O_RDONLY), k = 0;
	double ym[4096];

	if (fd < 0) { perror("/dev/video0"); return 1; }
	for (int i = 0; i < n; i++) {
		size_t got = 0;

		while (got < fs) {
			ssize_t r = read(fd, b + got, fs - got);

			if (r <= 0) break;
			got += (size_t)r;
		}
		double t = now();
		if (prev && i > 40) d[k++] = (t - prev) * 1000;   /* первые кадры — разгон */
		prev = t;
		unsigned long s = 0;
		for (size_t j = 0; j < fs; j += 64) s += b[j];
		if (i < 4096) ym[i] = (double)s / (fs / 64);
	}
	close(fd);
	qsort(d, k, sizeof(double), cmp);
	double sum = 0;
	for (int i = 0; i < k; i++) sum += d[i];
	printf("кадров %d: медиана %.1f мс (%.1f к/с), 95%% %.1f, макс %.1f, доля дольше 45 мс %.1f%%\n",
	       k, d[k / 2], 1000.0 * k / sum, d[(int)(k * 0.95)], d[k - 1],
	       100.0 * ({ int c = 0; for (int i = 0; i < k; i++) if (d[i] > 45) c++; c; }) / k);
	double av = 0, mx = 0;
	int nn = n < 4096 ? n : 4096;
	for (int i = 0; i < nn; i++) av += ym[i];
	av /= nn;
	for (int i = 1; i < nn; i++) if (fabs(ym[i] - ym[i-1]) > mx) mx = fabs(ym[i] - ym[i-1]);
	printf("яркость средняя %.1f, наибольший скачок между соседними кадрами %.2f\n", av, mx);
	return 0;
}
