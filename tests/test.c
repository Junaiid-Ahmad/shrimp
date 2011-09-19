/*
 * test.c
 *
 *  Created on: Jul 20, 2011
 */

#include "test.h"

/* Bitmap tests */

#define TEST__BITMAP_SIZE 4

void __CU_ASSERT_BITMAP_EQUAL(const bitmap_type * actual, const bitmap_type * expected) {
	int i;
	for (i = 0; i < TEST__BITMAP_SIZE; ++i) {
		CU_ASSERT_EQUAL(actual[i], expected[i]);
	}
}

void __bitmap_print (const bitmap_type * b, const char* label) {
	int i;
	printf ("\n");
	printf ("%6s: ", label);
	for (i = 0; i < TEST__BITMAP_SIZE; ++i) {
		printf ("%16lx ", b[i]);
	}
	//printf ("\n");
}

void test__bitmap_long_insert_clean (){
        bitmap_type b[] = {0x0, 0x0, 0x0, 0x0};
        char label[100];

        const bitmap_type e1_1[] = {0x0, 0x1000000000, 0x0, 0x0};
        const bitmap_type e1_2[] = {0x0, 0x1000000002, 0x0, 0x0};
        const bitmap_type e1_3[] = {0x1, 0x1000000002, 0x0, 0x0};

        const bitmap_type e2_1[] = {0x0, 0x0, 0x0, 0x300};
        const bitmap_type e2_2[] = {0x0, 0x0, 0x8, 0x300};
        const bitmap_type e2_3[] = {0x3, 0x0, 0x8, 0x300};

        bitmap_long_insert_clean(b, 1, 4, 100, 1);
        __CU_ASSERT_BITMAP_EQUAL(b, e1_1);
        bitmap_long_insert_clean(b, 1, 4, 65, 1);
        __CU_ASSERT_BITMAP_EQUAL(b, e1_2);
        bitmap_long_insert_clean(b, 1, 4, 0, 1);
        __CU_ASSERT_BITMAP_EQUAL(b, e1_3);

        bitmap_long_reset(b, 4);

        bitmap_long_insert_clean(b, 2, 4, 100, 0x3);
        CU_ASSERT_EQUAL(b[0], 0x0);
        __CU_ASSERT_BITMAP_EQUAL(b, e2_1);
        bitmap_long_insert_clean(b, 2, 4, 65, 0x2);
        __CU_ASSERT_BITMAP_EQUAL(b, e2_2);
        bitmap_long_insert_clean(b, 2, 4, 0, 0x3);
        __CU_ASSERT_BITMAP_EQUAL(b, e2_3);

}
void test__bitmap_long_clear (){
        bitmap_type b[4] = {0x1, 0x1000000002, ~0x0, ~0x0};

        bitmap_long_clear(b, 1, 4, 100);
        CU_ASSERT_EQUAL(b[0], 0x1);
        CU_ASSERT_EQUAL(b[1], 0x2);
        CU_ASSERT_EQUAL(b[2], ~(bitmap_type)0x0);
        CU_ASSERT_EQUAL(b[3], ~(bitmap_type)0x0);
        bitmap_long_clear(b, 1, 4, 65);
        CU_ASSERT_EQUAL(b[0], 0x1);
        CU_ASSERT_EQUAL(b[1], 0x0);
        CU_ASSERT_EQUAL(b[2], ~(bitmap_type)0x0);
        CU_ASSERT_EQUAL(b[3], ~(bitmap_type)0x0);
        bitmap_long_clear(b, 1, 4, 0);
        CU_ASSERT_EQUAL(b[0], 0x0);
        CU_ASSERT_EQUAL(b[1], 0x0);
        CU_ASSERT_EQUAL(b[2], ~(bitmap_type)0x0);
        CU_ASSERT_EQUAL(b[3], ~(bitmap_type)0x0);

        b[0] = 0x3;
        b[1] = 0x0;
        b[2] = 0x8;
        b[3] = 0x300;

        bitmap_long_clear(b, 2, 4, 55);
        CU_ASSERT_EQUAL(b[0], 0x3);
        CU_ASSERT_EQUAL(b[1], 0x0);
        CU_ASSERT_EQUAL(b[2], 0x8);
        CU_ASSERT_EQUAL(b[3], 0x300);
        bitmap_long_clear(b, 2, 4, 100);
        CU_ASSERT_EQUAL(b[0], 0x3);
        CU_ASSERT_EQUAL(b[1], 0x0);
        CU_ASSERT_EQUAL(b[2], 0x8);
        CU_ASSERT_EQUAL(b[3], 0x0);
        bitmap_long_clear(b, 2, 4, 65);
        CU_ASSERT_EQUAL(b[0], 0x3);
        CU_ASSERT_EQUAL(b[1], 0x0);
        CU_ASSERT_EQUAL(b[2], 0x0);
        CU_ASSERT_EQUAL(b[3], 0x0);
        bitmap_long_clear(b, 2, 4, 0);
        CU_ASSERT_EQUAL(b[0], 0x0);
        CU_ASSERT_EQUAL(b[1], 0x0);
        CU_ASSERT_EQUAL(b[2], 0x0);
        CU_ASSERT_EQUAL(b[3], 0x0);

}
void test__bitmap_long_extract (){
        bitmap_type b[4] = {0x1, 0x1000000002, 0x0, 0x0};
        CU_ASSERT_EQUAL(bitmap_long_extract(b, 1, 4, 100), 1);
        CU_ASSERT_EQUAL(bitmap_long_extract(b, 1, 4, 65), 1);
        CU_ASSERT_EQUAL(bitmap_long_extract(b, 1, 4, 0), 1);
        CU_ASSERT_EQUAL(bitmap_long_extract(b, 1, 4, 22), 0);


        b[0] = 0x3;
        b[1] = 0x0;
        b[2] = 0x8;
        b[3] = 0x300;

        CU_ASSERT_EQUAL(bitmap_long_extract(b, 2, 4, 100), 0x3);
        CU_ASSERT_EQUAL(bitmap_long_extract(b, 2, 4, 65), 0x2);
        CU_ASSERT_EQUAL(bitmap_long_extract(b, 2, 4, 0), 0x3);
        CU_ASSERT_EQUAL(bitmap_long_extract(b, 2, 4, 22), 0x0);
}

/* Seed tests */

void test__parse_spaced_seed (){
  struct seed_type seed;
  char* seed_string_1 = "100011110101";
  CU_ASSERT_TRUE_FATAL(parse_spaced_seed(seed_string_1, &seed));
  CU_ASSERT_EQUAL(seed.span, 12);
  CU_ASSERT_EQUAL(seed.weight, 7);
  CU_ASSERT_EQUAL(seed.mask[0], 0x8F5);
  CU_ASSERT_EQUAL(seed.positions[0], ~(bitmap_type)0x0);


  char* seed_string_2 = "11110011011001111:0|1|3|11|22|25|77";
  CU_ASSERT_TRUE_FATAL(parse_spaced_seed(seed_string_2, &seed));
  CU_ASSERT_EQUAL(seed.span, 17);
  CU_ASSERT_EQUAL(seed.weight, 12);
  CU_ASSERT_EQUAL(seed.mask[0], 0x1E6CF);
  CU_ASSERT_EQUAL(seed.positions[1], 0x2000);

}

/* Read loading tests */

#define TEST_READS_FILE_FASTQ "tests/pairs20.fq"

void test__fasta_load () {
	fasta_t f = fasta_open(TEST_READS_FILE_FASTQ, COLOUR_SPACE, true, NULL);
	read_entry re;
	int i, r = 0;
	int expected_quality[][50] = {
			{47, 50, 41, 42, 42, 38, 38, 38, 38, 41, 38, 54, 33, 49, 33, 33, 40, 33, 33, 40, 39, 40, 33, 33, 39, 33, 33, 38, 41, 46, 38, 42, 44, 41, 46,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
			{38, 57, 39, 49, 38, 39, 46, 38, 40, 40, 51, 43, 38, 39, 41, 45, 44, 39, 41, 42, 44, 43, 38, 40, 43, 41, 44, 46, 41, 38, 38, 41, 45, 38, 39, 41, 38, 43, 38, 44, 39, 38, 49, 42, 38, 38, 41, 41, 42, 38},
			{61, 65, 40, 44, 53, 38, 39, 58, 46, 39, 40, 56, 33, 49, 33, 33, 57, 33, 33, 38, 38, 41, 33, 33, 38, 33, 33, 39, 40, 45, 52, 47, 46, 39, 41,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
			{61, 53, 61, 65, 61, 64, 42, 65, 63, 61, 60, 58, 65, 62, 59, 51, 43, 64, 62, 49, 56, 60, 65, 62, 56, 61, 52, 58, 47, 57, 58, 43, 62, 58, 41, 59, 38, 58, 63, 61, 57, 48, 56, 61, 41, 60, 52, 39, 47, 42},
			{66, 66, 67, 33, 66, 66, 61, 40, 43, 45, 38, 42, 62, 44, 38, 60, 38, 64, 44, 45, 49, 41, 33, 49, 46, 54, 41, 55, 54, 44, 38, 60, 38, 63, 40,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
			{51, 65, 38, 38, 55, 58, 67, 59, 40, 62, 57, 54, 57, 43, 61, 40, 66, 38, 39, 66, 59, 62, 57, 49, 66, 65, 60, 62, 47, 48, 48, 63, 57, 40, 38, 47, 67, 64, 38, 65, 41, 61, 33, 42, 67, 57, 65, 38, 47, 46},
			{65, 39, 50, 33, 47, 38, 40, 52, 45, 40, 55, 42, 56, 42, 57, 54, 47, 61, 52, 56, 59, 51, 33, 58, 60, 44, 41, 42, 38, 60, 40, 46, 61, 49, 61,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
			{62, 51, 42, 39, 61, 58, 58, 62, 40, 49, 40, 56, 38, 60, 57, 42, 44, 59, 46, 56, 67, 38, 40, 64, 57, 42, 59, 50, 42, 62, 61, 48, 46, 48, 48, 50, 42, 38, 44, 48, 58, 57, 33, 42, 38, 67, 55, 42, 50, 44},
			{65, 50, 65, 33, 56, 60, 64, 48, 62, 65, 51, 53, 63, 63, 61, 56, 63, 49, 55, 47, 48, 49, 33, 51, 50, 55, 38, 45, 38, 55, 58, 62, 59, 56, 61,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
			{38, 46, 39, 41, 39, 38, 44, 49, 43, 38, 38, 47, 49, 42, 40, 41, 56, 49, 47, 41, 38, 41, 54, 45, 40, 38, 43, 60, 47, 42, 38, 41, 58, 56, 43, 43, 38, 61, 53, 51, 43, 38, 33, 56, 56, 42, 40, 41, 59, 57},
			{40, 65, 56, 33, 38, 63, 63, 45, 51, 51, 57, 58, 56, 63, 55, 42, 52, 44, 42, 48, 38, 42, 33, 49, 42, 38, 40, 42, 38, 38, 38, 66, 38, 51, 44,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
			{67, 67, 38, 62, 45, 46, 63, 64, 56, 55, 54, 63, 60, 53, 38, 57, 58, 42, 56, 44, 63, 67, 62, 51, 44, 56, 59, 62, 57, 50, 42, 66, 42, 61, 38, 42, 42, 38, 44, 44, 58, 66, 33, 54, 38, 38, 66, 39, 38, 40},
			{38, 38, 42, 33, 46, 44, 38, 40, 43, 43, 61, 61, 48, 47, 55, 43, 52, 63, 58, 52, 46, 38, 33, 58, 61, 50, 54, 54, 58, 64, 61, 60, 62, 51, 60,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
			{46, 41, 42, 38, 39, 46, 50, 49, 58, 45, 49, 39, 44, 43, 38, 43, 58, 45, 41, 43, 38, 40, 62, 53, 47, 39, 39, 58, 39, 47, 38, 41, 58, 56, 55, 46, 38, 53, 59, 55, 42, 43, 33, 57, 64, 43, 42, 54, 59, 60},
			{66, 64, 66, 33, 45, 54, 62, 61, 42, 51, 51, 52, 42, 44, 45, 38, 58, 57, 51, 45, 55, 54, 33, 51, 52, 62, 50, 42, 44, 61, 52, 52, 60, 38, 54,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
			{66, 66, 49, 57, 55, 50, 64, 54, 51, 58, 61, 67, 54, 57, 60, 62, 65, 46, 56, 63, 38, 66, 47, 58, 64, 46, 67, 42, 59, 65, 56, 67, 62, 62, 58, 62, 53, 61, 38, 43, 52, 60, 33, 45, 46, 62, 57, 39, 51, 39},
			{66, 33, 53, 67, 33, 54, 49, 53, 67, 41, 65, 65, 42, 42, 38, 55, 44, 42, 50, 56, 44, 57, 51, 55, 55, 61, 42, 40, 57, 57, 56, 39, 66, 44, 60,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
			{64, 33, 63, 67, 60, 63, 67, 65, 61, 63, 66, 67, 66, 67, 54, 64, 65, 38, 58, 65, 64, 57, 50, 67, 40, 61, 65, 50, 67, 42, 51, 62, 41, 50, 64, 65, 62, 38, 46, 40, 65, 65, 42, 46, 40, 44, 63, 33, 46, 57},
			{66, 67, 55, 53, 33, 47, 39, 39, 60, 39, 58, 62, 60, 44, 44, 44, 62, 66, 46, 48, 44, 49, 65, 40, 50, 40, 45, 65, 38, 45, 42, 65, 66, 42, 44,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
			{39, 67, 42, 40, 38, 38, 67, 52, 39, 39, 40, 67, 49, 41, 44, 42, 67, 53, 41, 47, 38, 53, 53, 40, 40, 41, 64, 56, 47, 52, 44, 40, 58, 56, 50, 46, 42, 60, 57, 58, 41, 41, 60, 59, 61, 48, 45, 33, 57, 60}
	};
	int expected_filtered_quality[][50] = {
			{10, 10,  8,  9,  9,  5,  5,  5,  5,  8,  5, 10,  0, 10,  0,  0,  7,  0,  0,  7,  6,  7,  0,  0,  6,  0,  0,  5,  8, 10,  5,  9, 10,  8, 10,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
			{ 5, 10,  6, 10,  5,  6, 10,  5,  7,  7, 10, 10,  5,  6,  8, 10, 10,  6,  8,  9, 10, 10,  5,  7, 10,  8, 10, 10,  8,  5,  5,  8, 10,  5,  6,  8,  5, 10,  5, 10,  6,  5, 10,  9,  5,  5,  8,  8,  9,  5},
			{10, 10,  7, 10, 10,  5,  6, 10, 10,  6,  7, 10,  0, 10,  0,  0, 10,  0,  0,  5,  5,  8,  0,  0,  5,  0,  0,  6,  7, 10, 10, 10, 10,  6,  8,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
			{10, 10, 10, 10, 10, 10,  9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,  8, 10,  5, 10, 10, 10, 10, 10, 10, 10,  8, 10, 10,  6, 10,  9},
			{10, 10, 10,  0, 10, 10, 10,  7, 10, 10,  5,  9, 10, 10,  5, 10,  5, 10, 10, 10, 10,  8,  0, 10, 10, 10,  8, 10, 10, 10,  5, 10,  5, 10,  7,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
			{10, 10,  5,  5, 10, 10, 10, 10,  7, 10, 10, 10, 10, 10, 10,  7, 10,  5,  6, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,  7,  5, 10, 10, 10,  5, 10,  8, 10,  0,  9, 10, 10, 10,  5, 10, 10},
			{10,  6, 10,  0, 10,  5,  7, 10, 10,  7, 10,  9, 10,  9, 10, 10, 10, 10, 10, 10, 10, 10,  0, 10, 10, 10,  8,  9,  5, 10,  7, 10, 10, 10, 10,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
			{10, 10,  9,  6, 10, 10, 10, 10,  7, 10,  7, 10,  5, 10, 10,  9, 10, 10, 10, 10, 10,  5,  7, 10, 10,  9, 10, 10,  9, 10, 10, 10, 10, 10, 10, 10,  9,  5, 10, 10, 10, 10,  0,  9,  5, 10, 10,  9, 10, 10},
			{10, 10, 10,  0, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,  0, 10, 10, 10,  5, 10,  5, 10, 10, 10, 10, 10, 10,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
			{ 5, 10,  6,  8,  6,  5, 10, 10, 10,  5,  5, 10, 10,  9,  7,  8, 10, 10, 10,  8,  5,  8, 10, 10,  7,  5, 10, 10, 10,  9,  5,  8, 10, 10, 10, 10,  5, 10, 10, 10, 10,  5,  0, 10, 10,  9,  7,  8, 10, 10},
			{ 7, 10, 10,  0,  5, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,  9, 10, 10,  9, 10,  5,  9,  0, 10,  9,  5,  7,  9,  5,  5,  5, 10,  5, 10, 10,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
			{10, 10,  5, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,  5, 10, 10,  9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,  9, 10,  9, 10,  5,  9,  9,  5, 10, 10, 10, 10,  0, 10,  5,  5, 10,  6,  5,  7},
			{ 5,  5,  9,  0, 10, 10,  5,  7, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,  5,  0, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
			{10,  8,  9,  5,  6, 10, 10, 10, 10, 10, 10,  6, 10, 10,  5, 10, 10, 10,  8, 10,  5,  7, 10, 10, 10,  6,  6, 10,  6, 10,  5,  8, 10, 10, 10, 10,  5, 10, 10, 10,  9, 10,  0, 10, 10, 10,  9, 10, 10, 10},
			{10, 10, 10,  0, 10, 10, 10, 10,  9, 10, 10, 10,  9, 10, 10,  5, 10, 10, 10, 10, 10, 10,  0, 10, 10, 10, 10,  9, 10, 10, 10, 10, 10,  5, 10,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
			{10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,  5, 10, 10, 10, 10, 10, 10,  9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,  5, 10, 10, 10,  0, 10, 10, 10, 10,  6, 10,  6},
			{10,  0, 10, 10,  0, 10, 10, 10, 10,  8, 10, 10,  9,  9,  5, 10, 10,  9, 10, 10, 10, 10, 10, 10, 10, 10,  9,  7, 10, 10, 10,  6, 10, 10, 10,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
			{10,  0, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,  5, 10, 10, 10, 10, 10, 10,  7, 10, 10, 10, 10,  9, 10, 10,  8, 10, 10, 10, 10,  5, 10,  7, 10, 10,  9, 10,  7, 10, 10,  0, 10, 10},
			{10, 10, 10, 10,  0, 10,  6,  6, 10,  6, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,  7, 10,  7, 10, 10,  5, 10,  9, 10, 10,  9, 10,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
			{ 6, 10,  9,  7,  5,  5, 10, 10,  6,  6,  7, 10, 10,  8, 10,  9, 10, 10,  8, 10,  5, 10, 10,  7,  7,  8, 10, 10, 10, 10, 10,  7, 10, 10, 10, 10,  9, 10, 10, 10,  8,  8, 10, 10, 10, 10, 10,  0, 10, 10}
	};
	while (fasta_get_next_read_with_range(f, &re)) {
		re.filter_qual = (char *)xmalloc(strlen(re.qual) + 17);
		read_quality_filter_preprocess(re.qual, re.filter_qual);
		for (i = 0; i < strlen(re.qual); ++i) {
			CU_ASSERT_EQUAL(re.qual[i], expected_quality[r][i]);
			CU_ASSERT_EQUAL(re.filter_qual[i], expected_filtered_quality[r][i]);
		}
		++r;
	}
	CU_ASSERT_EQUAL(r, 20);
}

/* Read quality tests */

#ifdef ENABLE_LOW_QUALITY_FILTER

#define __QUAL_LEN 20
void test__read_quality_preprocess () {
  const char original_qual[__QUAL_LEN + 1]           = {73, 64, 55, 49, 55, 43, 38, 40, 35, 36, 36, 67, 53, 53, 34, 39, 34, 37, 37, 36, 0};
  char processed_qual[__QUAL_LEN];
  const char expected_processed_qual[__QUAL_LEN] = {10, 10, 10, 10, 10, 10,  5,  7,  UNTRUSTED_QUALITY,  3,  3, 10, 10, 10,  UNTRUSTED_QUALITY,  6,  UNTRUSTED_QUALITY,  4,  4,  3};
  int i;
  read_quality_filter_preprocess(original_qual, processed_qual);
  for (i = 0; i < __QUAL_LEN; ++i) {
    // printf("%d %d \n", processed_qual[i], expected_processed_qual[i]);
    CU_ASSERT_EQUAL(processed_qual[i], expected_processed_qual[i]);
  }
}

void test__seed_quality_filter() {
  const char processed_qual[__QUAL_LEN]         = {10, 10, 10, 10, 10, 10,  5,  7,  UNTRUSTED_QUALITY,  3,  3, 10, 10, 10,  UNTRUSTED_QUALITY,  6,  UNTRUSTED_QUALITY,  4,  4,  3};
                                        //                              1   1   1   0                   0   1   1   0   1   0   1   1
#ifdef AUTOMATICALLY_DISCARD_LOW_QUAL_POSITIONS
  bool expected_low_quality_results[__QUAL_LEN] = { 0,  1,  0,  0,  0,  0,  0,  0,  0,  0};
#else
  bool expected_low_quality_results[__QUAL_LEN] = { 1,  1,  1,  1,  0,  1,  0,  0,  0,  0};
#endif

  char* seed_string = "111001101011";
  //char* seed_string = "110101100111";
  struct seed_type seed;
  int i;
  parse_spaced_seed(seed_string, &seed);
  for (i = 0; i < __QUAL_LEN - seed.span + 1; ++i) {
	// printf(" %d: %d (expected %d)\n", i, is_low_quality_read_segment(processed_qual, i, seed), expected_low_quality_results[i]);
	CU_ASSERT_EQUAL(is_low_quality_read_subsequence(processed_qual, i, seed), expected_low_quality_results[i]);
  }
}

#endif
