/*
 * test.h
 *
 *  Created on: Jul 20, 2011
 */

#ifndef TEST_H_
#define TEST_H_

#include <CUnit/Basic.h>
#include "../gmapper/gmapper-definitions.h"
#include "../common/debug.h"
#include "../common/util.h"
#include "../common/bitmap.h"
#include "../gmapper/seeds.h"
#include "../gmapper/gmapper.h"

/* Bitmap tests */

void test__bitmap_long_insert_clean ();
void test__bitmap_long_clear ();
void test__bitmap_long_extract ();

/* Seed tests */

void test__parse_spaced_seed ();

/* Read loading tests */

void test__fasta_load ();

/* Read quality tests */

void test__read_quality_preprocess ();
void test__seed_quality_filter();

#endif /* TEST_H_ */
