/* Зерно на снимке: разница с собственным размытием, по тёмным местам и по всему кадру. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static unsigned char *load(const char *f, int *w, int *h)
{
	FILE *p = fopen(f, "rb");
	char m[3];
	int mx;
	unsigned char *d;

	if (!p) return NULL;
	if (fscanf(p, "%2s %d %d %d", m, w, h, &mx) != 4) { fclose(p); return NULL; }
	fgetc(p);
	d = malloc((size_t)*w * *h * 3);
	fread(d, 1, (size_t)*w * *h * 3, p);
	fclose(p);
	return d;
}

int main(int argc, char **argv)
{
	for (int a = 1; a < argc; a++) {
		int w, h;
		unsigned char *d = load(argv[a], &w, &h);
		double sd = 0, sa = 0;
		long nd = 0, na = 0;

		if (!d) { printf("%s: не прочитался\n", argv[a]); continue; }
		/* яркость, разница точки со средним по кресту 5x5 */
		for (int y = 2; y < h - 2; y += 2)
			for (int x = 2; x < w - 2; x += 2) {
				double c = 0, s = 0;

				for (int j = -2; j <= 2; j++)
					for (int i = -2; i <= 2; i++) {
						const unsigned char *q = d + (((size_t)(y + j) * w) + x + i) * 3;
						double v = 0.30 * q[0] + 0.59 * q[1] + 0.11 * q[2];

						s += v;
						if (!i && !j) c = v;
					}
				double mean = s / 25.0, e = c - mean;

				sa += e * e; na++;
				if (mean < 70) { sd += e * e; nd++; }
			}
		printf("%-12s зерно: по кадру %.3f, в тенях %.3f (%ld точек)\n",
		       argv[a], sqrt(sa / (na ? na : 1)), sqrt(sd / (nd ? nd : 1)), nd);
		free(d);
	}
	return 0;
}
