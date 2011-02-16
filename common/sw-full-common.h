/*	$Id: sw-full-common.h,v 1.3 2008/06/06 18:24:09 rumble Exp $	*/

#ifndef _SW_FULL_COMMON_H
#define _SW_FULL_COMMON_H
#include <assert.h>

struct sw_full_results {
	/* Common fields */
	int read_start;				/* read index of map */
	int rmapped;				/* read mapped length */
	int genome_start;			/* genome index of map (abs) */
	int gmapped;				/* genome mapped len */
	int matches;				/* # of matches */
	int mismatches;				/* # of substitutions */
	int insertions;				/* # of insertions */
	int deletions;				/* # of deletions */
	int score;				/* final SW score */

	char *dbalign;				/* genome align string */
	char *qralign;				/* read align string */

	/* Colour space fields */
	int crossovers;				/* # of mat. xovers */
	bool dup;
};

static inline bool
sw_full_results_equal(struct sw_full_results *sfr1, struct sw_full_results *sfr2)
{
	char *dbalign1, *dbalign2;
	char *qralign1, *qralign2;
	bool dup1, dup2;
	bool equal;

	dup1=sfr1->dup;
	sfr1->dup=false;
	dup2=sfr2->dup;
	sfr2->dup=false;
	dbalign1 = sfr1->dbalign;
	dbalign2 = sfr2->dbalign;
	qralign1 = sfr1->qralign;
	qralign2 = sfr2->qralign;

	sfr1->dbalign = sfr2->dbalign = NULL;
	sfr1->qralign = sfr2->qralign = NULL;

	equal = memcmp(sfr1, sfr2, sizeof(*sfr1)) == 0;
	sfr1->dup=dup1;
	sfr2->dup=dup2;
	sfr1->dbalign = dbalign1;
	sfr2->dbalign = dbalign2;
	sfr1->qralign = qralign1;
	sfr2->qralign = qralign2;

	return (equal);
}


/*
 * Free sfrp for given hit.
 */
static inline void
free_sfrp(struct sw_full_results * * sfrp)
{
  assert(sfrp != NULL);

  if (*sfrp != NULL) {
    free((*sfrp)->dbalign);
    free((*sfrp)->qralign);
    free(*sfrp);
    *sfrp = NULL;
  }
}


#endif
