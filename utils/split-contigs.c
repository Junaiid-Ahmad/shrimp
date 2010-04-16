#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>
#include <omp.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "../common/fasta.h"
#include "../common/util.h"

#define MAX_CONTIGS 1000000

char * contig_name[MAX_CONTIGS];
int contig_size[MAX_CONTIGS];

int
main(int argc, char *argv[]) {
  fasta_t fasta_file;
  double target_size;
  int n_contigs = 0;
  char * seq;
  int i;

  if (argc < 3) {
    fprintf(stderr, "Usage: %s <genome_file> <target_RAM_size_in_GB>\n", argv[0]);
    exit(1);
  }

  fasta_file = fasta_open(argv[1], LETTER_SPACE);
  if (fasta_file == NULL) {
    fprintf(stderr, "error: could not open genome file [%s]\n", argv[1]);
    exit(1);
  }

  target_size = atof(argv[2]);
  if (target_size < 0.5 || target_size > 256.0) {
    fprintf(stderr, "error: the target memory size doesn't seem right in GB [%s]\n", argv[2]);
    exit(1);
  }

  while (n < MAX_CONTIGS && fasta_get_next(fasta_file, &contig_name[n], &seq, NULL)) {
    contig_size[n] = strlen(seq);
    free(seq);
    n++;
  }
  if (n >= MAX_CONTIGS) {
    fprintf(stderr, "error: too many contigs\n");
    exit(1);
  }

  fasta_close(fasta_file);

  for (i = 0; i < n; i++) {
    fprintf("%s\t%d\n", contig_name[i], contig_size[i]);
  }

  return 0;
}
