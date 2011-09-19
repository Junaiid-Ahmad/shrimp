#include <CUnit/Basic.h>
#include "test.h"

/*
 * Some global vars; do not affect the tests,
 * but compilation fails without them
 */
shrimp_mode_t shrimp_mode = MODE_COLOUR_SPACE;
int n_seeds = 0;
struct seed_type *	seed = NULL;
int max_seed_span = 0;
int min_seed_span = MAX_SEED_SPAN;
int avg_seed_span = 0;
uint32_t * * seed_hash_mask = NULL;
bool Hflag = false;
count_t mem_small = {};
/*
 * done with fake vars
 */
/*
 * "Real" global vars, used in tests
 */
int qual_delta = DEF_CS_QUAL_DELTA;

int main () {
  CU_TestInfo bitmap_operation_tests[] = {
      {"long bitmap insert", test__bitmap_long_insert_clean},
      {"long bitmap clear", test__bitmap_long_clear},
      {"long bitmap extract", test__bitmap_long_extract},
      CU_TEST_INFO_NULL
  };
  CU_TestInfo seed_tests[] = {
      {"seed string parser", test__parse_spaced_seed},
      CU_TEST_INFO_NULL
  };
  CU_TestInfo fasta_load_tests[] = {
      {"read load", test__fasta_load},
      CU_TEST_INFO_NULL
  };
  CU_TestInfo quality_tests[] = {
      {"read quality pre-process", test__read_quality_preprocess},
      {"seed quality filter", test__seed_quality_filter},
      CU_TEST_INFO_NULL
  };

  CU_SuiteInfo suites[] = {
      {"Bitmap operation suite", NULL, NULL, bitmap_operation_tests},
      {"Load suite", NULL, NULL, fasta_load_tests},
      {"Seeds suite", NULL, NULL, seed_tests},
      {"Quality filter suite", NULL, NULL, quality_tests},
      CU_SUITE_INFO_NULL
  };

  /* initialize the CUnit test registry */
  if (CUE_SUCCESS != CU_initialize_registry()) {
    return CU_get_error();
  }

  /* add test suites to the registry */
  if (CUE_SUCCESS != CU_register_suites(suites)) {
    CU_cleanup_registry();
    return CU_get_error();
  }

  /* run all tests using the CUnit Basic interface */
  CU_basic_set_mode(CU_BRM_VERBOSE);
  CU_basic_run_tests();

  /* cleanup the CUnit registry */
  CU_cleanup_registry();
  return CU_get_error();
}
