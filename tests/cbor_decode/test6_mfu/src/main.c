/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ztest.h>
#include "mfu_decode.h"


static void verify_segment(Segment_t *s, uint32_t addr, uint32_t len) {
	zassert_equal(s->_Segment_target_addr, addr, "");
	zassert_equal(s->_Segment_len, len, "");
}

void test_mfu(void)
{
	Segments_t segments;

	uint32_t expected[] =  {0x2000000c, 0x1290, 0x59602000, 0x910, 0x6000,
				0xa000, 0x50000, 0x1de000, 0x27c000, 0x4000};

	uint8_t input[] = { 0x8a, 0x1a, 0x20, 0x00, 0x00, 0x0c, 0x19, 0x12,
		0x90, 0x1a, 0x59, 0x60, 0x20, 0x00, 0x19, 0x09, 0x10, 0x19,
		0x60, 0x00, 0x19, 0xa0, 0x00, 0x1a, 0x00, 0x05, 0x00, 0x00,
		0x1a, 0x00, 0x1d, 0xe0, 0x00, 0x1a, 0x00, 0x27, 0xc0, 0x00,
		0x19, 0x40, 0x00, 0xAA, 0xBB, 0xCC};

	size_t payload_len;
	bool res = cbor_decode_Segments(input, sizeof(input), &segments,
					false, &payload_len);

	zassert_equal(payload_len, 41, "");
	zassert_true(res, "");

	for (int i = 0; i < (sizeof(expected) / sizeof(uint32_t)) / 2; i+=2) {
		verify_segment(&segments._Segments__Segment[i/2],
			       expected[i], expected[i+1]);
	}
}


void test_main(void)
{
	ztest_test_suite(cbor_decode_test6,
			 ztest_unit_test(test_mfu)
	);
	ztest_run_test_suite(cbor_decode_test6);
}
