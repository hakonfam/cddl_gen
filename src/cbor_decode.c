/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "cbor_decode.h"

/** Enumeration representing the major types available in CBOR.
 *
 * The major type is represented in the 3 first bits of the header byte.
 */
typedef enum
{
	CBOR_MAJOR_TYPE_PINT = 0, ///! Positive Integer
	CBOR_MAJOR_TYPE_NINT = 1, ///! Negative Integer
	CBOR_MAJOR_TYPE_BSTR = 2, ///! Byte String
	CBOR_MAJOR_TYPE_TSTR = 3, ///! Text String
	CBOR_MAJOR_TYPE_LIST = 4, ///! List
	CBOR_MAJOR_TYPE_MAP  = 5, ///! Map
	CBOR_MAJOR_TYPE_TAG  = 6, ///! Semantic Tag
	CBOR_MAJOR_TYPE_PRIM = 7, ///! Primitive Type
} cbor_major_type_t;

/** Return value length from additional value.
 */
static uint32_t additional_len(uint8_t additional)
{
	switch (additional) {
	case 24: return 1;
	case 25: return 2;
	case 26: return 4;
	case 27: return 8;
	default: return 0;
	}
}

/** Extract the major type, i.e. the first 3 bits of the header byte. */
#define MAJOR_TYPE(header_byte) ((header_byte) >> 5) & 0x7

/** Extract the additional info, i.e. the last 5 bits of the header byte. */
#define ADDITIONAL(header_byte) (header_byte) & 0x1F;

/** Shorthand macro to check if a result is within min/max constraints.
 */
#define PTR_VALUE_IN_RANGE(type, p_res, p_min, p_max) \
		(((p_min == NULL) || (*(type *)p_res >= *(type *)p_min)) \
		&& ((p_max == NULL) || (*(type *)p_res <= *(type *)p_max)))

#define FAIL() \
do {\
	cbor_decode_trace(); \
	return false; \
} while(0)

#define FAIL_AND_DECR_IF(expr) \
do {\
	if (expr) { \
		(*pp_payload)--; \
		FAIL(); \
	} \
} while(0)

#define VALUE_IN_HEADER 23 /**! For values below this, the value is encoded
                                directly in the header. */

#define BOOL_TO_PRIM 20 ///! In CBOR, false/true have the values 20/21

/** Get a single value.
 *
 * @details @p pp_payload must point to the header byte. This function will
 *          retrieve the value (either from within the additional info, or from
 *          the subsequent bytes) and return it in the result. The result can
 *          have arbitrary length.
 *
 *          The function will also validate
 *           - Min/max constraints on the value.
 *           - That @p pp_payload doesn't overrun past @p p_payload_end.
 *           - That @p p_elem_count has not been exhausted.
 *
 *          @p pp_payload and @p p_elem_count are updated if the function
 *          succeeds. If not, they are left unchanged.
 *
 *          CBOR values are always big-endian, so this function converts from
 *          big to little-endian if necessary (@ref CONFIG_BIG_ENDIAN).
 */
static bool value_extract(uint8_t const **const pp_payload,
		uint8_t const *const p_payload_end, size_t *const p_elem_count,
		void * const p_result, size_t result_len)
{
	uint8_t *p_u8_result  = (uint8_t *)p_result;
	uint8_t additional = ADDITIONAL(**pp_payload);
	(*pp_payload)++;

	cbor_decode_trace();

	cbor_decode_assert(result_len == 0, "0-length result not supported.");

	FAIL_AND_DECR_IF(*p_elem_count == 0);
	FAIL_AND_DECR_IF(*pp_payload > p_payload_end);

	memset(p_result, 0, result_len);
#ifdef CONFIG_BIG_ENDIAN
	p_u8_result[result_len - 1] = additional;
#else
	p_u8_result[0] = additional;
#endif /* CONFIG_BIG_ENDIAN */
	if (additional > VALUE_IN_HEADER) {
		uint32_t len = additional_len(additional);

		FAIL_AND_DECR_IF(len > result_len);
		FAIL_AND_DECR_IF((*pp_payload + len) > p_payload_end);

#ifdef CONFIG_BIG_ENDIAN
		memcpy(&p_u8_result[result_len - len], *pp_payload, len);
#else
		for (uint32_t i = 0; i < len; i++) {
			p_u8_result[i] = (*pp_payload)[len - i - 1];
		}
#endif /* CONFIG_BIG_ENDIAN */
		(*pp_payload) += len;
	}

	(*p_elem_count)--;
	return true;
}


static bool int32_decode(uint8_t const **const pp_payload,
			uint8_t const *const p_payload_end,
			size_t *const p_elem_count, int32_t *p_result,
			void *p_min_value, void *p_max_value)
{
	uint8_t major_type = MAJOR_TYPE(**pp_payload);

	if (!value_extract(pp_payload, p_payload_end,
			p_elem_count, p_result, 4)) {
		FAIL();
	}
	if (*p_result < 0) {
		/* Value is too large to fit in a signed integer. */
		FAIL();
	}

	if (major_type == CBOR_MAJOR_TYPE_NINT) {
		// Convert from CBOR's representation.
		*p_result = 1 - *p_result;
	}
	if (!PTR_VALUE_IN_RANGE(int32_t, p_result, p_min_value, p_max_value)) {
		FAIL();
	}
	cbor_decode_print("val: %d\r\n", *p_result);
	return true;
}


bool intx32_decode(uint8_t const **const pp_payload,
			uint8_t const *const p_payload_end,
			size_t *const p_elem_count, int32_t *p_result,
			void *p_min_value, void *p_max_value)
{
	uint8_t major_type = MAJOR_TYPE(**pp_payload);

	if (major_type != CBOR_MAJOR_TYPE_PINT
		&& major_type != CBOR_MAJOR_TYPE_NINT) {
		/* Value to be read doesn't have the right type. */
		FAIL();
	}

	if (!int32_decode(pp_payload, p_payload_end, p_elem_count,
				p_result, p_min_value,
				p_max_value)){
		FAIL();
	}
	return true;
}


static bool uint32_decode(uint8_t const **const pp_payload,
			uint8_t const *const p_payload_end,
			size_t *const p_elem_count,
			void *p_result, void *p_min_value,
			void *p_max_value)
{
	if (!value_extract(pp_payload, p_payload_end,
			p_elem_count, p_result, 4)) {
		FAIL();
	}

	if (!PTR_VALUE_IN_RANGE(uint32_t, p_result, p_min_value, p_max_value)) {
		FAIL();
	}
	cbor_decode_print("val: %u\r\n", *p_result);
	return true;
}


bool uintx32_decode(uint8_t const **const pp_payload,
		uint8_t const *const p_payload_end,
		size_t *const p_elem_count,
		uint32_t *p_result, void *p_min_value,
		void *p_max_value)
{
	uint8_t major_type = MAJOR_TYPE(**pp_payload);

	if (major_type != CBOR_MAJOR_TYPE_PINT) {
		/* Value to be read doesn't have the right type. */
		FAIL();
	}
	if (!uint32_decode(pp_payload, p_payload_end, p_elem_count,
				p_result, p_min_value, p_max_value)){
		FAIL();
	}
	return true;
}


bool size_decode(uint8_t const **const pp_payload,
		uint8_t const *const p_payload_end,
		size_t *const p_elem_count,
		size_t *p_result, size_t *p_min_value,
		size_t *p_max_value)
{
	_Static_assert((sizeof(size_t) == sizeof(uint32_t)),
			"This code needs size_t to be 4 bytes long.");
	return uint32_decode(pp_payload, p_payload_end, p_elem_count,
			p_result, p_min_value, p_max_value);
}


bool strx_start_decode(uint8_t const **const pp_payload,
				uint8_t const *const p_payload_end,
				size_t *const p_elem_count,
				cbor_string_type_t *p_result, void *p_min_len,
				void *p_max_len)
{
	uint8_t major_type = MAJOR_TYPE(**pp_payload);
	cbor_string_type_t *p_str_result = (cbor_string_type_t *)p_result;

	if (major_type != CBOR_MAJOR_TYPE_BSTR
		&& major_type != CBOR_MAJOR_TYPE_TSTR) {
		/* Value to be read doesn't have the right type. */
		FAIL();
	}
	if (!size_decode(pp_payload, p_payload_end, p_elem_count,
			&p_str_result->len, (size_t *)p_min_len,
			(size_t *)p_max_len)) {
		FAIL();
	}
	p_str_result->value = *pp_payload;
	return true;
}

bool strx_decode(uint8_t const **const pp_payload,
				uint8_t const *const p_payload_end,
				size_t *const p_elem_count,
				cbor_string_type_t *p_result, void *p_min_len,
				void *p_max_len)
{
	if (!strx_start_decode(pp_payload, p_payload_end, p_elem_count, p_result,
				p_min_len, p_max_len)) {
		return false;
	}
	(*pp_payload) += p_result->len;
	return true;
}


bool list_start_decode(uint8_t const **const pp_payload,
			uint8_t const *const p_payload_end,
			size_t *const p_elem_count,
			size_t *p_result, size_t min_num, size_t max_num)
{
	uint8_t major_type = MAJOR_TYPE(**pp_payload);

	if (major_type != CBOR_MAJOR_TYPE_LIST
		&& major_type != CBOR_MAJOR_TYPE_MAP) {
		FAIL();
	}
	if (!uint32_decode(pp_payload, p_payload_end, p_elem_count,
			p_result, &min_num, &max_num)) {
		FAIL();
	}
	return true;
}


bool primx_decode(uint8_t const **const pp_payload,
				uint8_t const *const p_payload_end,
				size_t *const p_elem_count,
				uint8_t *p_result, void *p_min_result,
				void *p_max_result)
{
	uint8_t major_type = MAJOR_TYPE(**pp_payload);
	uint32_t val;


	if (major_type != CBOR_MAJOR_TYPE_PRIM) {
		/* Value to be read doesn't have the right type. */
		FAIL();
	}
	if (!uint32_decode(pp_payload, p_payload_end, p_elem_count,
			&val, p_min_result, p_max_result)) {
		FAIL();
	}
	if (p_result != NULL) {
		*p_result = val;
	}
	return true;
}


bool boolx_decode(uint8_t const **const pp_payload,
				uint8_t const *const p_payload_end,
				size_t *const p_elem_count,
				bool *p_result, void *p_min_result,
				void *p_max_result)
{
	uint8_t min_result = *(uint8_t *)p_min_result + BOOL_TO_PRIM;
	uint8_t max_result = *(uint8_t *)p_max_result + BOOL_TO_PRIM;

	if (!primx_decode(pp_payload, p_payload_end, p_elem_count,
			(uint8_t *)p_result, &min_result, &max_result)) {
		FAIL();
	}
	(*p_result) -= BOOL_TO_PRIM;
	return true;
}


bool double_decode(uint8_t const **const pp_payload,
				uint8_t const *const p_payload_end,
				size_t *const p_elem_count,
				double *p_result, void *p_min_result,
				void *p_max_result)
{
	uint8_t major_type = MAJOR_TYPE(**pp_payload);

	if (major_type != CBOR_MAJOR_TYPE_PRIM) {
		/* Value to be read doesn't have the right type. */
		FAIL();
	}
	if (!value_extract(pp_payload, p_payload_end, p_elem_count, p_result,
			sizeof(*p_result))) {
		FAIL();
	}

	if (!PTR_VALUE_IN_RANGE(double, p_result, p_min_result, p_max_result)) {
		FAIL();
	}
	return true;
}


bool any_decode(uint8_t const **const pp_payload,
				uint8_t const *const p_payload_end,
				size_t *const p_elem_count,
				void *p_result, void *p_min_result,
				void *p_max_result)
{
	cbor_decode_assert(p_result != NULL,
			"'any' type cannot be returned, only skipped.");

	uint8_t major_type = MAJOR_TYPE(**pp_payload);
	uint64_t value;

	if (!value_extract(pp_payload, p_payload_end,
			p_elem_count, &value, 8)) {
		/* Should not happen? */
		FAIL();
	}

	switch (major_type) {
		case CBOR_MAJOR_TYPE_BSTR:
		case CBOR_MAJOR_TYPE_TSTR:
		case CBOR_MAJOR_TYPE_LIST:
		case CBOR_MAJOR_TYPE_MAP:
			(*pp_payload) += value;
			break;
		default:
			/* Do nothing */
			break;
	}

	return true;
}


bool multi_decode(size_t min_decode,
		size_t max_decode,
		size_t *p_num_decode,
		decoder_t decoder,
		uint8_t const **const pp_payload,
		uint8_t const *const p_payload_end,
		size_t *const p_elem_count,
		void *p_result,
		void *p_min_result,
		void *p_max_result,
		size_t result_len)
{
	for (size_t i = 0; i < max_decode; i++) {
		uint8_t const *p_payload_bak = *pp_payload;
		size_t elem_count_bak = *p_elem_count;

		if (!decoder(pp_payload,
				p_payload_end,
				p_elem_count,
				(uint8_t *)p_result + i*result_len,
				p_min_result,
				p_max_result)) {
			*p_num_decode = i;
			*pp_payload = p_payload_bak;
			*p_elem_count = elem_count_bak;
			if (i < min_decode) {
				FAIL();
			} else {
				cbor_decode_print("Found %d elements.\n", i);
			}
			return true;
		}
	}
	cbor_decode_print("Found %d elements.\n", max_decode);
	*p_num_decode = max_decode;
	return true;
}
