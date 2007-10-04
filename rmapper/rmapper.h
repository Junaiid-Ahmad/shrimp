/*	$Id$	*/

#define RMAPPER_VERSION_STR	"1.0.0"

extern const int use_colours;

#define DEF_SPACED_SEED		"11110111"
#define DEF_WINDOW_LEN		30
#define DEF_NUM_MATCHES		2
#define DEF_TABOO_LEN		4
#define DEF_NUM_OUTPUTS		100
#define DEF_MAX_READ_LEN	64
#define DEF_KMER_STDDEV_LIMIT	-1

#define DEF_MATCH_VALUE		100
#define DEF_MISMATCH_VALUE	-70
#define DEF_GAP_OPEN		-100
#define DEF_GAP_EXTEND		-70
#define DEF_XOVER_PENALTY	-200
#define DEF_SW_VECT_THRESHOLD	1875	/* == DEF_SW_FULL_THRESHOLD in lspace */
#define DEF_SW_FULL_THRESHOLD	1975

/*
 * The maximum seed weight (maximum number of 1's in the seed) sets an
 * upper limit on our lookup table allocation size. The memory usage of
 * rmapper corresponds strongly to 4^MAX_SEED_WEIGHT * (sizeof(void *) +
 * sizeof(uint32_t)). At 16, this is 32GB on 32-bit and 48GB on 64-bit
 * architectures.
 */
#ifndef MAX_SEED_WEIGHT
#define MAX_SEED_WEIGHT		16
#endif

struct re_score {
	int32_t  score;				/* doubles as heap cnt in [0] */
	uint32_t index;
};

struct read_elem {
	char		 *name;
	struct read_elem *next;			/* next in read list */
	uint32_t	 *read;			/* the read as a bitstring */
	uint32_t	  read_len;

	int		  swhits;		/* num of hits with sw */
	uint32_t	  last_swhit_idx;	/* index of last sw hit */
	struct re_score  *scores;		/* top 'num_ouputs' scores */

	int		  initbp;		/* colour space init letter */

	uint32_t	  prev_hit;		/* prev index in 'hits' */
	uint32_t	  next_hit;		/* next index in 'hits' */
	uint32_t	  hits[0];		/* size depends on num_matches*/
};

/* for kmer to read map */
struct read_node {
	struct read_elem *read;
	struct read_node *next;
};