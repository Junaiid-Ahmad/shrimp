/*	$Id$	*/

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "../common/sw-full-common.h"
#include "../common/input.h"
#include "../common/dynhash.h"
#include "../common/util.h"
#include "../common/version.h"

#include "probcalc.h"

/* STATS DEFINITIONS */
double * factTable;
double ** chooseTable;
double *** maxIndelsTable;
double *** minIndelsTable;
double ** subsTable;
/* END OF STATS DEF */

static dynhash_t read_list;		/* list of reads and top matches */
static dynhash_t contig_cache;		/* contig name cache */
static dynhash_t read_seq_cache;	/* read sequence cache */
static dynhash_t read_edit_cache;	/* read edit string cache */

static bool Bflag = false;		/* print a progress bar */
static bool Gflag = false;		/* calculate rates and output them */
static bool Rflag = false;		/* include read sequence in output */
static bool Sflag = false;		/* do it all in one pass (save in ram)*/

static char *rates_file;		/* -g flag specifies a rates file */

struct readinfo {
	char	    *name;
	struct input top_matches[0];
};

struct rates {
	uint64_t samples;
	uint64_t total_len;
	uint64_t insertions;
	uint64_t deletions;
	uint64_t matches;
	uint64_t mismatches;
	uint64_t crossovers;

	double   erate;			/* error rate (crossovers) */
	double   srate;			/* substitution rate (mismatches) */
	double   irate;			/* indel rate (insertions, deletions) */
	double   mrate;			/* match rate (matches) */
};

struct readstatspval {
	struct input     *rs;
	double		  pchance;
	double		  pgenome;
	double            normodds;
};

struct pass_cb {
	struct rates	rates;
	uint64_t	nbytes;
	uint64_t	nfiles;
	uint64_t	total_files;
	int		pass;
};

enum {
	SORT_PCHANCE = 1,
	SORT_PGENOME,
	SORT_NORMODDS
};

static uint64_t total_alignments;	/* total outputs in all files */
static uint64_t total_unique_reads;	/* total unique reads in all files */

/* arguments */
static double   normodds_cutoff		= DEF_NORMODDS_CUTOFF;
static double   pchance_cutoff		= DEF_PCHANCE_CUTOFF;
static double	pgenome_cutoff	        = DEF_PGENOME_CUTOFF;
static int	top_matches		= DEF_TOP_MATCHES;
static uint64_t genome_len;
static int	sort_field		= SORT_PCHANCE;

#define PROGRESS_BAR(_a, _b, _c, _d)	\
    if (Bflag) progress_bar((_a), (_b), (_c), (_d))

#define ALMOST_ZERO	0.000000001
#define ALMOST_ONE	0.999999999

static uint32_t
keyhasher(void *k)
{
	
	return (hash_string((char *)k));
} 

static int
keycomparer(void *a, void *b)
{

	return (strcmp((char *)a, (char *)b));
}

/* percolate down in our min-heap */
static void
reheap(struct input *stats, int node)
{
	struct input tmp;
	int left, right, max;

	left  = node * 2;
	right = left + 1;
	max   = node;
	
	if (left <= stats[0].score &&
	    stats[left].score < stats[node].score)
		max = left;

	if (right <= stats[0].score &&
	    stats[right].score < stats[max].score)
		max = right;

	if (max != node) {
		tmp = stats[node];
		stats[node] = stats[max];
		stats[max] = tmp;
		reheap(stats, max);
	}
}

/* update our match min-heap */
static void
save_match(struct readinfo *ri, struct input *input)
{
	struct input *stats = ri->top_matches;

	if (input->score < stats[1].score)
		return;

	memcpy(&stats[1], input, sizeof(stats[1]));
	reheap(stats, 1);
}

/* Returns the number of bytes read */
static void
read_file(const char *fpath)
{
	struct input input;
	struct readinfo *ri;
	char *key;
	gzFile fp;
	int i;

	fp = gzopen(fpath, "r");
	if (fp == NULL) {
		fprintf(stderr, "error: could not open file [%s]: %s\n",
		    fpath, strerror(errno));
		exit(1);
	}

	while (input_parseline(fp, &input)) {
		/*
		 * If we don't want the read sequence, free it if it exists.
		 * Otherwise, cache it so we don't have duplicates hogging
		 * memory.
		 */
		if (!Rflag) {
			if (input.read_seq != NULL)
				free(input.read_seq);
			input.read_seq = NULL;
		} else if (input.read_seq != NULL) {
			if (dynhash_find(read_seq_cache, input.read_seq,
			    (void **)&key, NULL)) {
				free(input.read_seq);
				input.read_seq = key;
			} else {
				if (dynhash_add(read_seq_cache, input.read_seq,
				    NULL) == false) {
					fprintf(stderr, "error: failed to add "
					    "to read_seq cache (%d) - probably "
					    "out of memory\n", __LINE__);
					exit(1);
				}
			}
		}

		/*
		 * Cache the contig names similarly, so we don't waste memory.
		 */
		if (dynhash_find(contig_cache, input.genome, (void **)&key,
		    NULL)) {
			free(input.genome);
			input.genome = key;
		} else {
			if (dynhash_add(contig_cache, input.genome,
			    NULL) == false) {
				fprintf(stderr, "error: failed to add to "
				    "contig cache (%d) - probably out of "
				    "memory\n", __LINE__);
				exit(1);
			}
		}

		/*
		 * Finally, cache edit strings for memory happiness.
		 */
		if (dynhash_find(read_edit_cache, input.edit, (void **)&key,
		    NULL)) {
			free(input.edit);
			input.edit = key;
		} else {
			if (dynhash_add(read_edit_cache, input.edit,
			    NULL) == false) {
				fprintf(stderr, "error: failed to add to "
				    "edit cache (%d) - probably out of "
				    "memory\n", __LINE__);
				exit(1);
			}
		}

		total_alignments++;

		if (dynhash_find(read_list, input.read, (void **)&key,
		    (void **)&ri)) {
			/* use only one read name string to save space */
			free(input.read);
			input.read = key;

			/* test if score better than worst, if so, add it */
			if (input.score > ri->top_matches[1].score)
				save_match(ri, &input);
		} else {
			total_unique_reads++;

			/* alloc, init, insert in dynhash */
			ri = (struct readinfo *)xmalloc(sizeof(*ri) +
			    sizeof(ri->top_matches[0]) * (top_matches + 1));
			memset(ri, 0, sizeof(*ri) +
			    sizeof(ri->top_matches[0]) * (top_matches + 1));

			ri->name = input.read;
			ri->top_matches[0].score = top_matches;
			for (i = 1; i <= top_matches; i++)
				ri->top_matches[i].score = 0x80000000 + i;

			save_match(ri, &input);

			if (dynhash_add(read_list, ri->name, ri) == false) {
				fprintf(stderr, "error: failed to add to "
				    "dynhash - probably out of memory\n");
				exit(1);
			}
		}
	}

	gzclose(fp);
}

static double
p_chance(uint64_t l, int k, int nsubs, int nerrors, int nindels, int origlen, int ins, int dels)
{
	double r = 0;

	(void)nindels;

	int corr_fact = (origlen-k+1); // correction factor, normal space

	/* substitutions */
	r += log(subsTable[k][nsubs+nerrors]); // log space
	
	/* indels */
	r += log(0.5*(maxIndelsTable[k][dels][ins]+minIndelsTable[k][dels][ins])); // log space
	
	/* correction factor */
	r += log(corr_fact); // log space

	/* r is now log of fraction of matched words over total words */
	if (r <= k * log(4))
		r -= (k * log(4)); // log space
	else 
		return 1;
	
	r = exp(r); // normal space
	
	/* 
	at this point, r = cf * (Z/4^k) 
	We have the following potential problem: if r is very small (good hit), 
	'1.0-r' will approximate to 1.0.
	
	to avoid this, consider the binomial approximation: (1-r)^x = 1-rx when r is very small
	hence our pchance = 1 - (1-r)^x = 1-(1-xr) = xr, where x is 2*l
	
	Thus: IF (1-r) == 1; use bionmial approx; ELSE; compute exact pchance; FI
	*/
	if( 1-r == 1 ) { // use bionmial approx
		r = (2.0*l)*r;
	} else { // compute exact pchance
		r = 1-r; // normal space
		r = 2*l*log(r); // log space
		r = (1.0 - exp(r)); // normal space
	}
	
	return r;
}

static void
calc_rates(void *arg, void *key, void *val)
{
	struct rates *rates = (struct rates *)arg;
	struct readinfo *ri = (struct readinfo *)val;
	struct input *rs;
	double d;
	int32_t best;
	int i, rlen;

	(void)key;

	if (Sflag) {
		static uint64_t calls;
		PROGRESS_BAR(stderr, calls, total_unique_reads, 100);
		calls++;
	}

	best = 0;
	for (i = 1; i <= ri->top_matches[0].score; i++) {
		if (best == 0 ||
		    ri->top_matches[i].score > ri->top_matches[best].score) {
			/* NB: strictly >, as we want same results for double and single passes
			       if there are two with the same score, but different alignments */
			best = i;
		}
	}

	if (!Sflag) {
		assert(ri->top_matches[0].score == 1);
		assert(ri->top_matches[1].score >= 0);
	}

	/*
	 * Only count the best score with no equivalent best scores.
	 */
	assert(best != 0);
	rs = &ri->top_matches[best];
	rlen =  rs->matches + rs->mismatches + rs->deletions;

	d = p_chance(genome_len, rlen, rs->mismatches, rs->crossovers,
		rs->insertions + rs->deletions, rs->read_length, rs->insertions, rs->deletions);

	if (best != 0 && d < pchance_cutoff) {
		rates->samples++;
		rates->total_len  += rs->matches + rs->mismatches;
		rates->insertions += rs->insertions;
		rates->deletions  += rs->deletions;
		rates->matches    += rs->matches;
		rates->mismatches += rs->mismatches;
		rates->crossovers += rs->crossovers;
	}
}

static double
p_thissource(int k, int nerrors, double erate, int nsubs, double subrate,
    int nindels, double indelrate, int nmatches, double matchrate, int origlen)
{
        double r, p_err, p_sub, p_indel;
        int i;

        (void)origlen;
	(void)nmatches;
	(void)matchrate;

        // This function computes the likelihood that the read will differ by as much or more
        // from the reference, given the mutational parameters.
        //  Each of the loops is the probability that a given read would have
        // that many or more {errors, subs, indels}. Errors and indels happen between letters, hence k-1 possible
        // positions; subs happen at letters, and can't happen at the last letters, hence k-2.
        // k is just matches + mismatches for this function; matchrate is unused, at least for now...
        p_err = 0;
        for (i=0; i < nerrors; i++) {
          p_err  += exp(ls_choose(k-1, i) + i * log(erate) + (k-1-i)*log(1-erate));
        }
        p_err = 1-p_err;
        p_sub = 0;
        for (i=0; i < nsubs; i++) {
          p_sub += exp(ls_choose(k - 2 - nerrors, i) + i * log(subrate) + (k-2-nerrors-i)*log(1-subrate));
        }
        p_sub = 1-p_sub;
        p_indel = 0;
        for (i=0; i < nindels; i++) {
          p_indel += exp(ls_choose(k-1 , i) + i * log(indelrate) + (k-1-i)*log(1-indelrate));
        }
        p_indel = 1-p_indel;
        // now the product is the likelihood that a real alignment would have
        //that many (or more) of each event type
        r = p_err * p_sub * p_indel;

/*
        if (r < ALMOST_ZERO)
                r = ALMOST_ZERO;
        if (r > ALMOST_ONE)
                r = ALMOST_ONE;
*/
        return (r);
}


static int
rspvcmp(const void *a, const void *b)
{
	const struct readstatspval *rspva = (const struct readstatspval *)a;
	const struct readstatspval *rspvb = (const struct readstatspval *)b;
	double cmp_a, cmp_b;

	switch (sort_field) {
	case SORT_PGENOME:
		/* reversed - want big first */
	        cmp_a = rspvb->pgenome;
		cmp_b = rspva->pgenome;
		break;
	case SORT_PCHANCE:
		cmp_a = rspva->pchance;
		cmp_b = rspvb->pchance;
		break;
	case SORT_NORMODDS:
		/* reversed - want big first */
	        cmp_a = rspvb->normodds;
		cmp_b = rspva->normodds;
		break;
	default:
		/* shut up, gcc */
		cmp_a = cmp_b = 0;
		assert(0);
	}

	if (cmp_a == cmp_b)
		return (0);
	else if (cmp_a < cmp_b)
		return (-1);
	else
		return (1);
}

static void
calc_probs(void *arg, void *key, void *val)
{
	static struct readstatspval *rspv;
	static bool called = false;

	struct rates *rates = (struct rates *)arg;
	struct readinfo *ri = (struct readinfo *)val;
	double s, norm;
	int i, j, rlen;

	(void)key;

	if (Sflag) {
		static uint64_t calls;
		PROGRESS_BAR(stderr, calls, total_unique_reads, 10);
		calls++;
	}

	if (rspv == NULL)
		rspv = (struct readstatspval *)xmalloc(sizeof(rspv[0]) * (top_matches + 1));

	/* 1: Calculate P(chance) and P(genome) for each hit */
	norm = 0;
	for (i = 1, j = 0; i <= top_matches; i++) {
		struct input *rs = &ri->top_matches[i];

		if (rs->score < 0)
			continue;

		rlen = rs->matches + rs->mismatches + rs->deletions;
		//fprintf (stdout, "matches: %i, mismatches: %i, rsdels: %i ", rs->matches , rs->mismatches , rs->deletions); 
		s = p_chance(genome_len, rlen, rs->mismatches, rs->crossovers,
		    rs->insertions + rs->deletions, rs->read_length, rs->insertions, rs->deletions);
		    
		//fprintf(stdout,"%f\n", s);
		    
	//	if (s < ALMOST_ZERO || isnan(s))
		//	s = ALMOST_ZERO;

		if (s > pchance_cutoff)
			continue;

		rspv[j].rs = rs;
		rspv[j].pchance = s;

		rlen = rs->matches + rs->mismatches;
		rspv[j].pgenome = p_thissource(rlen, rs->crossovers,
		    rates->erate, rs->mismatches, rates->srate,
		    rs->insertions + rs->deletions, rates->irate,
		    rs->matches, rates->mrate, rs->read_length);
		rspv[j].normodds = rspv[j].pgenome / rspv[j].pchance;
		norm += rspv[j].normodds;
		j++;
	}

	/* 2: Normalise our values */
	for (i = 0; i < j; i++)
		rspv[i].normodds = rspv[i].normodds / norm;

	/* 3: Sort the values ascending */
	qsort(rspv, j, sizeof(*rspv), rspvcmp);

	/* 4: Finally, print out values in ascending order until the cutoff */
	if (!called) {
		printf("#FORMAT: readname contigname strand contigstart contigend readstart readend "
		    "readlength score editstring %snormodds pgenome pchance\n",
		    (Rflag) ? "readsequence " : "");
		called = true;
	}

	for (i = 0; i < j; i++) {
		struct input *rs = rspv[i].rs;
		char *readseq = "";

		if (rspv[i].normodds < normodds_cutoff) {
			if (sort_field == SORT_NORMODDS)
				break;
			else
				continue;
		}
		if (rspv[i].pgenome < pgenome_cutoff) {
			if (sort_field == SORT_PGENOME)
				break;
			else
				continue;
		}
		if (rspv[i].pchance > pchance_cutoff) {
			if (sort_field == SORT_PCHANCE)
				break;
			else
				continue;
		}

		if (rs->read_seq != NULL && rs->read_seq[0] != '\0')
			readseq = rs->read_seq;
		else if (Rflag)
			readseq = " ";

		/* the only sane way is to reproduce the output code, sadly. */
		printf(">%s\t%s\t%c", rs->read, rs->genome,
		    (INPUT_IS_REVCMPL(rs)) ? '-' : '+');

		/* NB: internally 0 is first position, output is 1. adjust */
		printf("\t%u\t%u\t%d\t%d\t%d\t%d\t%s\t%s%s%e\t%e\t%e\n",
		    rs->genome_start + 1, rs->genome_end + 1, rs->read_start + 1,
		    rs->read_end + 1, rs->read_length, rs->score, rs->edit,
		    (Rflag) ? readseq : "", (Rflag) ? "\t" : "",
		    rspv[i].normodds, rspv[i].pgenome, rspv[i].pchance);
	}
}

static unsigned int cleanup_cb_called = 0;

static void
cleanup_cb(void *arg, void *key, void *val)
{

	/* shut up, icc */
	(void)arg;

	cleanup_cb_called++;
	free(key);
	if (val != NULL) {
		assert(arg == read_list);
		free(val);
	} else {
		assert(arg == read_seq_cache ||
		       arg == read_edit_cache);
	}
}

/* Remove everything from read_list and read_seq_cache. */
static void
cleanup()
{

	cleanup_cb_called = 0;
	dynhash_iterate(read_list, cleanup_cb, read_list);
	assert(dynhash_count(read_list) == cleanup_cb_called);
	dynhash_destroy(read_list);

	read_list = dynhash_create(keyhasher, keycomparer);
	if (read_list == NULL) {
		fprintf(stderr, "error: failed to allocate read_list\n");
		exit(1);
	}

	cleanup_cb_called = 0;
	dynhash_iterate(read_seq_cache, cleanup_cb, read_seq_cache);
	assert(dynhash_count(read_seq_cache) == cleanup_cb_called);
	dynhash_destroy(read_seq_cache);

	read_seq_cache = dynhash_create(keyhasher, keycomparer);
	if (read_seq_cache == NULL) {
		fprintf(stderr, "error: failed to allocate read_seq_cache\n");
		exit(1);
	}

	dynhash_iterate(read_edit_cache, cleanup_cb, read_edit_cache);
	dynhash_destroy(read_edit_cache);
	read_edit_cache = dynhash_create(keyhasher, keycomparer);
	if (read_edit_cache == NULL) {
		fprintf(stderr, "error: failed to allocate read_edit_cache\n");
		exit(1);
	}
}

static void
load_rates(const char *rates_file, struct rates *rates)
{
	FILE *fp;
	char buf[512];
	char *b;

	memset(rates, 0, sizeof(*rates));

	fp = fopen(rates_file, "r");
	if (fp == NULL) {
		fprintf(stderr, "error: failed to open rates file [%s]\n",
		    rates_file);
		exit(1);
	}

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		int r;
		uint64_t totaligns, totunique, samples, length, insertions;
		uint64_t deletions, matches, mismatches, xovers;

		if (buf[0] != '>')
			continue;

		b = strtrim(buf + 1);

		/* tot. alignments, tot. unique reads, samples, length, insertions, deletions,
		 * matches, mismatches, xovers */
		r = sscanf(b, "%"PRIu64 "%"PRIu64 "%"PRIu64 "%"PRIu64 "%"PRIu64 "%"PRIu64 "%"PRIu64
		    "%"PRIu64 "%"PRIu64, &totaligns, &totunique, &samples, &length, &insertions,
		    &deletions, &matches, &mismatches, &xovers);

		if (r != 9) {
			fprintf(stderr, "error: failed to parse rates file line [%s]\n", b);
			exit(1);
		}

		total_alignments  += totaligns;
		total_unique_reads+= totunique;
		rates->samples    += samples;
		rates->total_len  += length;
		rates->insertions += insertions;
		rates->deletions  += deletions;
		rates->matches    += matches;
		rates->mismatches += mismatches;
		rates->crossovers += xovers;
	}

	fclose(fp);
}

static void
ratestats(struct rates *rates)
{

	fprintf(stderr, "\nCalculated rates for %s reads\n",
	    comma_integer(total_unique_reads));

	fprintf(stderr, "\nStatistics:\n");
	fprintf(stderr, "    total matches:      %s\n",
	    comma_integer(total_alignments));
	fprintf(stderr, "    total unique reads: %s\n",
	    comma_integer(total_unique_reads));
	fprintf(stderr, "    total samples:      %s\n",
	    comma_integer(rates->samples));
	fprintf(stderr, "    total length:       %s\n",
	    comma_integer(rates->total_len));
	fprintf(stderr, "    insertions:         %s\n",
	    comma_integer(rates->insertions));
	fprintf(stderr, "    deletions:          %s\n",
	    comma_integer(rates->deletions));
	fprintf(stderr, "    matches:            %s\n",
	    comma_integer(rates->matches));
	fprintf(stderr, "    mismatches:         %s\n",
	    comma_integer(rates->mismatches));
	fprintf(stderr, "    crossovers:         %s\n",
	    comma_integer(rates->crossovers));

	if (Gflag) {
		printf(">%" PRIu64 " %" PRIu64 " %"PRIu64 " %" PRIu64 " %" PRIu64
		    " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 "\n",
		    total_alignments, total_unique_reads, rates->samples, rates->total_len,
		    rates->insertions, rates->deletions, rates->matches, rates->mismatches,
		    rates->crossovers);
		exit(0);
	}

	rates->erate = (double)rates->crossovers / (double)rates->total_len;
	rates->srate = (double)rates->mismatches / (double)rates->total_len;
	rates->irate = (double)(rates->insertions + rates->deletions) /
	    (double)rates->total_len;
	rates->mrate = (double)rates->matches / (double)rates->total_len;

	if (rates->erate == 0.0 || rates->srate == 0.0 || rates->irate == 0.0 ||
	    rates->mrate == 0.0) {
		fprintf(stderr, "NB: one or more rates changed from 0 to "
		    "%f\n", ALMOST_ZERO);
	}
	rates->erate = (rates->erate == 0.0) ? ALMOST_ZERO : rates->erate;
	rates->srate = (rates->srate == 0.0) ? ALMOST_ZERO : rates->srate;
	rates->irate = (rates->irate == 0.0) ? ALMOST_ZERO : rates->irate;
	rates->mrate = (rates->mrate == 0.0) ? ALMOST_ZERO : rates->mrate;

	fprintf(stderr, "    error rate:         %.10f\n", rates->erate);
	fprintf(stderr, "    substitution rate:  %.10f\n", rates->srate);
	fprintf(stderr, "    indel rate:         %.10f\n", rates->irate);
	fprintf(stderr, "    match rate:         %.10f\n", rates->mrate);
}

static void
single_pass_cb(char *path, struct stat *sb, void *arg)
{
	struct pass_cb *pcb = (struct pass_cb *)arg;

	read_file(path);
	pcb->nbytes += sb->st_size;
	pcb->nfiles++;
	PROGRESS_BAR(stderr, pcb->nfiles, pcb->total_files, 100);
}

/*
 * Do everything in one pass, saving it all in memory. This is potentially
 * much faster (factor of two or so), but can eat a ton of memory.
 *
 * This mode does _not_ require that all matches for a single individual read
 * are in the same file.
 */
static void
do_single_pass(char **objs, int nobjs, uint64_t files)
{
	struct pass_cb pcb;

	fprintf(stderr, "warning: single pass mode may consume a large amount "
	    "of memory!\n\n");

	memset(&pcb, 0, sizeof(pcb));
	pcb.total_files = files;
	fprintf(stderr, "Parsing %" PRIu64 " file(s)...\n", files);
	PROGRESS_BAR(stderr, 0, 0, 100);
	file_iterator_n(objs, nobjs, single_pass_cb, &pcb);
	PROGRESS_BAR(stderr, files, files, 100);
	fprintf(stderr, "\nParsed %.2f MB in %" PRIu64 " file(s).\n",
	    (double)pcb.nbytes / (1024 * 1024), pcb.nfiles);

	/*
	 * First iterative pass: calculate rates for top matches of reads.
	 */
	if (rates_file == NULL) {
		fprintf(stderr, "\nCalculating top match rates...\n");
		PROGRESS_BAR(stderr, 0, 0, 100);
		dynhash_iterate(read_list, calc_rates, &pcb.rates);
		PROGRESS_BAR(stderr, total_unique_reads, total_unique_reads, 100);
	} else {
		fprintf(stderr, "\nUsing user-defined rates...\n");
		load_rates(rates_file, &pcb.rates);
	}

	if (total_unique_reads == 0) {
		fprintf(stderr, "error: no matches were found in input "
		    "file(s)\n");
		exit(1);
	}

	ratestats(&pcb.rates);

	/*
	 * Second iterative pass: Determine probabilities for all reads' best
	 * alignments.
	 */
	fprintf(stderr, "\nGenerating output...\n");
	PROGRESS_BAR(stderr, 0, 0, 10);
	dynhash_iterate(read_list, calc_probs, &pcb.rates);
	PROGRESS_BAR(stderr, total_unique_reads, total_unique_reads, 10);
	if (Bflag)
		putc('\n', stderr);
}

static void
double_pass_cb(char *path, struct stat *sb, void *arg)
{
	struct pass_cb *pcb = (struct pass_cb *)arg;

	read_file(path);
	pcb->nbytes += sb->st_size;
	pcb->nfiles++;
	PROGRESS_BAR(stderr, pcb->nfiles, pcb->total_files, 100);

	/* iterate over what's been read and free stored data */
	assert(pcb->pass == 1 || pcb->pass == 2);
	if (pcb->pass == 1)
		dynhash_iterate(read_list, calc_rates, &pcb->rates);
	else
		dynhash_iterate(read_list, calc_probs, &pcb->rates);
	cleanup();
}

/*
 * Do everything in two passes, once per file to calculate rates, and another
 * time per file to do the probability calculations.
 *
 * This mode requires that all matches for a single individual read are in
 * the same file.
 */
static void
do_double_pass(char **objs, int nobjs, uint64_t files)
{
	struct pass_cb pcb;
	int tmp_top_matches;

	/*
	 * First iterative pass: Calculate rates.
	 */
	if (rates_file == NULL) {
		/* only need best match for calculating rates */
		tmp_top_matches = top_matches;
		top_matches = 1;

		memset(&pcb, 0, sizeof(pcb));
		pcb.pass = 1;
		pcb.total_files = files;

		fprintf(stderr, "PASS 1: Parsing %" PRIu64 " file(s) to calculate "
		    "rates...\n", files);
		PROGRESS_BAR(stderr, 0, 0, 100);
		file_iterator_n(objs, nobjs, double_pass_cb, &pcb);
		PROGRESS_BAR(stderr, files, files, 100);
		fprintf(stderr, "\nParsed %.2f MB in %" PRIu64 " file(s).\n",
		    (double)pcb.nbytes / (1024 * 1024), pcb.nfiles);

		/* restore desired top matches */
		top_matches = tmp_top_matches;
	} else {
		fprintf(stderr, "\nUsing user-defined rates...\n");
		load_rates(rates_file, &pcb.rates);
	}

	if (total_unique_reads == 0) {
		fprintf(stderr, "error: no matches were found in input "
		    "file(s)\n");
		exit(1);
	}

	ratestats(&pcb.rates);

	/*
	 * Second iterative pass: Determine probabilities for all reads' best
	 * alignments.
	 */
	pcb.pass = 2;
	pcb.nbytes = pcb.nfiles = 0;

	fprintf(stderr, "\n%sParsing %" PRIu64 " file(s) to calculate "
	    "probabilities...\n", (rates_file == NULL) ? "PASS 2: " : "", files);
	PROGRESS_BAR(stderr, 0, 0, 100);
	file_iterator_n(objs, nobjs, double_pass_cb, &pcb);
	PROGRESS_BAR(stderr, files, files, 100);
	fprintf(stderr, "\nParsed %.2f MB in %" PRIu64 " file(s).\n",
	    (double)pcb.nbytes / (1024 * 1024), pcb.nfiles);
}

static void
count_files(char *path, struct stat *sb, void *arg)
{
	uint64_t *i = (uint64_t *)arg;

	/* shut up, icc */
	(void)path;
	(void)sb;

	(*i)++;
}

static void
usage(char *progname)
{
	char *slash;

	slash = strrchr(progname, '/');
	if (slash != NULL)
		progname = slash + 1;

	fprintf(stderr, "usage: %s [-g rates_file] [-n normodds_cutoff] [-o pgenome_cutoff] "
	    "[-p pchance_cutoff] [-s normodds|pgenome|pchance] "
	    "[-t top_matches] [-B] [-G] [-R] [-S] "
	    "total_genome_len results_dir1|results_file1 "
	    "results_dir2|results_file2 ...\n", progname);
	exit(1);
}

int
main(int argc, char **argv)
{



	
	/*************************************************************************
	 * Stats calculations
	 *************************************************************************/
	
	/* parse input */
	int maxins = 70;
	int maxdels = 70;
	int maxlen = 70;
	int maxsubs = 70;
	
	/* max function initialization */
	/* initiate fast factoriaztion table */
	initfastfact();
	
	/* compute maxCount via formula for ins=0:maxins, dels=0:maxdels */
	int ins, dels, len, subs;
	
	maxIndelsTable = (double ***) malloc(sizeof(double **) * (maxlen + 1));
	minIndelsTable = (double ***) malloc(sizeof(double **) * (maxlen + 1));
	subsTable = (double **) malloc(sizeof(double *) * (maxlen + 1));
	
	for (len = 0; len <= maxlen; len++) {
		maxIndelsTable[len] = (double **) malloc(sizeof(double *) * (maxins + 1));
		minIndelsTable[len] = (double **) malloc(sizeof(double *) * (maxins + 1));
		subsTable[len] = (double *) malloc(sizeof(double) * (maxsubs + 1));
		
		for (ins = 0; ins <= maxins; ins++) {
			maxIndelsTable[len][ins] = (double *) malloc(sizeof(double) * (maxdels + 1));
			minIndelsTable[len][ins] = (double *) malloc(sizeof(double) * (maxdels + 1));
			
			for (dels = 0; dels <= maxdels; dels++) {
				
				maxIndelsTable[len][ins][dels] = maxCount(ins, dels, len);
				minIndelsTable[len][ins][dels] = minCount(ins, dels, len);
			}
		}
		
		for (subs = 0; subs <= maxsubs; subs++) {
			subsTable[len][subs] =  subCount(subs, len);
		}
	}
	
	char *progname;
	uint64_t total_files;
	int ch;

	set_mode_from_argv(argv);

	fprintf(stderr, "--------------------------------------------------"
	    "------------------------------\n");
	fprintf(stderr, "probcalc. (SHRiMP %s [%s])\n",
	    SHRIMP_VERSION_STRING, get_compiler());
	fprintf(stderr, "--------------------------------------------------"
	    "------------------------------\n");

	progname = argv[0];
	while ((ch = getopt(argc, argv, "n:o:p:g:s:t:BGRS")) != -1) {
		switch (ch) {
		case 'g':
			rates_file = xstrdup(optarg);
			break;
		case 'n':
			normodds_cutoff = atof(optarg);
			break;
		case 'o':
			pgenome_cutoff = atof(optarg);
			break;
		case 'p':
			pchance_cutoff = atof(optarg);
			break;
		case 's':
			if (strstr(optarg, "pchance") != NULL) {
				sort_field = SORT_PCHANCE;
			} else if (strstr(optarg, "pgenome") != NULL) {
				sort_field = SORT_PGENOME;
			} else if (strstr(optarg, "normodds") != NULL) {
				sort_field = SORT_NORMODDS;
			} else {
				fprintf(stderr, "error: invalid sort "
				    "criteria\n");
				usage(progname);
			}
			break;
		case 't':
			top_matches = atoi(optarg);
			break;
		case 'B':
			Bflag = true;
			break;
		case 'G':
			Gflag = true;
			break;
		case 'R':
			Rflag = true;
			break;
		case 'S':
			Sflag = true;
			break;
		default:
			usage(progname);
		}
	}
	argc -= optind;
	argv += optind;

	if (Gflag && rates_file != NULL) {
		fprintf(stderr, "error: -G and -g flags cannot be mixed\n");
		exit(1);
	}

	if (argc < 2)
		usage(progname);
	
	if (!is_number(argv[0])) {
		fprintf(stderr, "error: expected a number for genome length "
		    "(got: %s)\n", argv[0]);
		usage(progname);
	}

	genome_len = strtoul(argv[0], NULL, 0);

	argc--;
	argv++;

	fprintf(stderr, "\nSettings:\n");
	fprintf(stderr, "    Normodds Cutoff:  < %f\n", normodds_cutoff);
	fprintf(stderr, "    Pgenome Cutoff:   < %f\n", pgenome_cutoff);
	fprintf(stderr, "    Pchance Cutoff:   > %f\n", pchance_cutoff);
	fprintf(stderr, "    Sort Criteria:      %s\n",
	    (sort_field == SORT_PCHANCE)  ? "pchance"  :
	    (sort_field == SORT_PGENOME)  ? "pgenome"  :
	    (sort_field == SORT_NORMODDS) ? "normodds" : "<unknown>");
	fprintf(stderr, "    Top Matches:        %d\n", top_matches);
	fprintf(stderr, "    Genome Length:      %s\n",
	    comma_integer(genome_len));

	fprintf(stderr, "\n");

	if (Gflag)
		fprintf(stderr, "NOTICE: Calculating rates only.\n");

	read_list = dynhash_create(keyhasher, keycomparer);
	if (read_list == NULL) {
		fprintf(stderr, "error: failed to allocate read_list\n");
		exit(1);
	}

	contig_cache = dynhash_create(keyhasher, keycomparer);
	if (contig_cache == NULL) {
		fprintf(stderr, "error: failed to allocate contig_cache\n");
		exit(1);
	}

	read_seq_cache = dynhash_create(keyhasher, keycomparer);
	if (read_seq_cache == NULL) {
		fprintf(stderr, "error: failed to allocate read_seq_cache\n");
		exit(1);
	}

	read_edit_cache = dynhash_create(keyhasher, keycomparer);
	if (read_edit_cache == NULL) {
		fprintf(stderr, "error: failed to allocate read_edit_cache\n");
		exit(1);
	}

	total_files = 0;
	file_iterator_n(argv, argc, count_files, &total_files);

	if (total_files == 0) {
		fprintf(stderr, "error: no files found to parse!\n");
		exit(1);
	}

	if (Sflag)
		do_single_pass(argv, argc, total_files);
	else
		do_double_pass(argv, argc, total_files);

	
	
	

	
	return (0);
}







/*********************************************************
 * STATS FUNCTIONS
 *********************************************************/

/* EXACT FUNCTIONS */

/* exact factorial function */
double fact(double n) {
	if (n > 0) { return n * fact(n-1); }
	else { return 1.0; }
}

/* exact choose function, using exact fact */
double choose(int n, int m) {
	return fact(n)/ ( fact(n-m) * fact(m));
}




/* FAST FUNCTIONS */

/* initiatlize fast factorial lookup table */
void initfastfact() {
	
	factTable = (double *) malloc (sizeof(double) * 21);
	int i;
	for (i = 0; i < 21; i++) {
		factTable[i] = fact(i);
	}
	
	
}

/* initiatlize fast factorial lookup table */
void initlookupchoose() {
	
	int max_choose = 35;
	
	chooseTable = (double **) malloc (sizeof(double *) * (max_choose + 1));
	int i, j;
	for (i = 0; i <= max_choose; i++) {
		chooseTable[i] = (double *) malloc (sizeof(double) * (max_choose + 1));
	
		for (j = 0; j <= max_choose; j++) {
			if (i >= j) { chooseTable[i][j] = choose(i, j); }
			else {chooseTable[i][j] = -1; }
		} 
	}
	
}

/* 
 * compute fast factorial: 
 * for n=0..20 use lookup table, for n>20 use stirling approx 
 */
double fastfact(int n) {
	
	if (n < 21) {
		return factTable[n];
	} else {
		double lnn =  0.5 * log((2*n + 1.0/3.0) * M_PI) + n * log(n) - n;
		return exp(lnn); 
	}
	
}

/* fast choose function: uses fastfact */
double fastchoose(int n, int m) {
	return fastfact(n)/ ( fastfact(n-m) * fastfact(m));

}


/* lookup choose function: uses lookup table*/
double lookupchoose(int n, int m) {
	return chooseTable[n][m];

}





double maxCount(int ins, int dels, int len) {
	
	int i = 0;
	
	double sum = 0;
	
	for (i = 0; i <= ins; i++) {
		sum += fastchoose(len, dels) * fastchoose(len + ins - dels, i) * pow(3, i);
	}
	
	return sum;
}



double minCount(int ins, int dels, int len) {
	
	int i;
	
	double sum = 0;
	
	if (dels == 0) {
		for (i = 0; i <= ins; i++) {
			sum += fastchoose(len + ins, i) * pow(3, i);
		}
	} else {
		sum = choose(len + ins, ins) * pow(3, ins);
	}
		
	return sum;
}

double subCount(int subs, int len) {
	
	//if (len == 21) {
	//	fprintf(stderr, "DETinside: %i, %f, %f\n", subs, fastchoose(len, subs), pow(3, subs));
	//}
	
	double sum = 0;
	
	int i;
	for (i = 0; i <= subs; i++) {
		sum += fastchoose(len, i) * pow(3, i);
	}
	return sum;
	
	
	//double sum = fastchoose(len, subs) * pow(3, subs);
	//return sum;
}
