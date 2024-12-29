//----------------------------------------------------------------
//
//  4190.307 Operating Systems (Fall 2024)
//
//  Project #4: xSwap: Compressed Swap for xv6
//
//  November 7, 2024
//
//  This is a port of the LZO library used in Linux.
//  It was slightly modified specifically for xv6
//  running on 64-bit little-endian RISC-V CPUs.
//  See below for the original license.
//
//  Jin-Soo Kim (jinsoo.kim@snu.ac.kr)
//  Systems Software & Architecture Laboratory
//  Dept. of Computer Science and Engineering
//  Seoul National University
//
//----------------------------------------------------------------

// SPDX-License-Identifier: GPL-2.0-only
/*
 *  LZO1X Compressor from LZO
 *  LZO1X Decompressor from LZO
 *
 *  Copyright (C) 1996-2012 Markus F.X.J. Oberhumer <markus@oberhumer.com>
 *
 *  The full LZO package can be found at:
 *  http://www.oberhumer.com/opensource/lzo/
 *
 *  Changed for Linux kernel use by:
 *  Nitin Gupta <nitingupta910@gmail.com>
 *  Richard Purdie <rpurdie@openedhand.com>
 */

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

typedef unsigned int    size_t;
typedef unsigned long   u64;
typedef unsigned int    u32;
typedef unsigned short  u16;

#define NULL    0
#define LZO_VERSION 1

#define min(a,b)        (((a)<(b))? (a):(b))
#define min_t(type,a,b) min((type)a,(type)b)

// From include/linux/align.h and include/uapi/linux/const.h
typedef unsigned long   uintptr_t;
#define __ALIGN_KERNEL(x, a)          __ALIGN_KERNEL_MASK(x, (__typeof__(x))(a) - 1)
#define __ALIGN_KERNEL_MASK(x, mask)  (((x) + (mask)) & ~(mask))
#define ALIGN(x, a) 
#define IS_ALIGNED(x, a)              (((x) & ((typeof(x))(a) - 1)) == 0)

#define get_unaligned(ptr)            (*((ptr)))
#define put_unaligned(val, ptr)       (*(ptr)) = (val)

#define get_unaligned_le32(ptr)       (*((uint32 *) (ptr)))
#define get_unaligned_le16(ptr)       (*((uint16 *) (ptr)))
#define put_unaligned_le32(val, ptr)  (*((uint32 *) (ptr))) = (val)
#define put_unaligned_le16(val, ptr)  (*((uint16 *) (ptr))) = (val)

#define COPY4(dst, src) \
        put_unaligned(get_unaligned((const u32 *)(src)), (u32 *)(dst))
#define COPY8(dst, src) \
        put_unaligned(get_unaligned((const u64 *)(src)), (u64 *)(dst))

// For LZO
#define M1_MAX_OFFSET       0x0400
#define M2_MAX_OFFSET       0x0800
#define M3_MAX_OFFSET       0x4000
#define M4_MAX_OFFSET_V0    0xbfff
#define M4_MAX_OFFSET_V1    0xbffe

#define M1_MIN_LEN          2
#define M1_MAX_LEN          2
#define M2_MIN_LEN          3
#define M2_MAX_LEN          8
#define M3_MIN_LEN          3
#define M3_MAX_LEN          33
#define M4_MIN_LEN          3
#define M4_MAX_LEN          9

#define M1_MARKER           0
#define M2_MARKER           64
#define M3_MARKER           32
#define M4_MARKER           16

#define MIN_ZERO_RUN_LENGTH 4
#define MAX_ZERO_RUN_LENGTH (2047 + MIN_ZERO_RUN_LENGTH)

#define lzo_dict_t          unsigned short
#define D_BITS              13
#define D_SIZE              (1u << D_BITS)
#define D_MASK              (D_SIZE - 1)
#define D_HIGH              ((D_MASK >> 1) + 1)

#define HAVE_IP(x)          ((size_t)(ip_end - ip) >= (size_t)(x))
#define HAVE_OP(x)          ((size_t)(op_end - op) >= (size_t)(x))
#define NEED_IP(x)          if (!HAVE_IP(x)) goto input_overrun
#define NEED_OP(x)          if (!HAVE_OP(x)) goto output_overrun
#define TEST_LB(m_pos)      if ((m_pos) < out) goto lookbehind_overrun

// from include/linux/lzo.h
/*
 * Return values (< 0 = Error)
 */
#define LZO_E_OK                    0
#define LZO_E_ERROR                 (-1)
#define LZO_E_OUT_OF_MEMORY         (-2)
#define LZO_E_NOT_COMPRESSIBLE      (-3)
#define LZO_E_INPUT_OVERRUN         (-4)
#define LZO_E_OUTPUT_OVERRUN        (-5)
#define LZO_E_LOOKBEHIND_OVERRUN    (-6)
#define LZO_E_EOF_NOT_FOUND         (-7)
#define LZO_E_INPUT_NOT_CONSUMED    (-8)
#define LZO_E_NOT_YET_IMPLEMENTED   (-9)
#define LZO_E_INVALID_ARGUMENT      (-10)

// From include/linux/lzo.h
//#define LZO1X_1_MEM_COMPRESS  (D_SIZE * sizeof(lzo_dict_t))

// __ctzdi2() counts the number of trailing zeroes.
// This implementation is from CryptoPkg/IntrinsicLib.
uint32
__ctzdi2(uint64 x)
{
  uint32 ret = 0;

  if (!x)
    return 64;
  if (!(x & 0xffffffff)) {
    x >>= 32;
    ret |= 32;
  }
  if (!(x & 0xffff)) {
    x >>= 16;
    ret |= 16;
  }
  if (!(x & 0xff)) {
    x >>= 8;
    ret |= 8;
  }
  if (!(x & 0xf)) {
    x >>= 4;
    ret |= 4;
  }
  if (!(x & 0x3)) {
    x >>= 2;
    ret |= 2;
  }
  if (!(x & 0x1)) {
    x >>= 1;
    ret |= 1;
  }
  return ret;
}


static size_t
lzo1x_1_do_compress(const unsigned char *in, size_t in_len,
		    unsigned char *out, size_t *out_len,
		    size_t ti, void *wrkmem, signed char *state_offset,
		    const unsigned char bitstream_version)
{
	const unsigned char *ip;
	unsigned char *op;
	const unsigned char * const in_end = in + in_len;
	const unsigned char * const ip_end = in + in_len - 20;
	const unsigned char *ii;
	lzo_dict_t * const dict = (lzo_dict_t *) wrkmem;


	op = out;
	ip = in;
	ii = ip;
	ip += ti < 4 ? 4 - ti : 0;

  //printf("sizeof(gwrkmem)=%lu\n", sizeof(gwrkmem));

	for (;;) {
		const unsigned char *m_pos = NULL;
		size_t t, m_len, m_off;
		u32 dv;
		u32 run_length = 0;
literal:
		ip += 1 + ((ip - ii) >> 5);
next:
		if (ip >= ip_end)
			break;
		dv = get_unaligned_le32(ip);

		if (dv == 0 && bitstream_version) {
			const unsigned char *ir = ip + 4;
			const unsigned char *limit = min(ip_end, ip + MAX_ZERO_RUN_LENGTH + 1);
			u64 dv64;

			for (; (ir + 32) <= limit; ir += 32) {
				dv64 = get_unaligned((u64 *)ir);
				dv64 |= get_unaligned((u64 *)ir + 1);
				dv64 |= get_unaligned((u64 *)ir + 2);
				dv64 |= get_unaligned((u64 *)ir + 3);
				if (dv64)
					break;
			}
			for (; (ir + 8) <= limit; ir += 8) {
				dv64 = get_unaligned((u64 *)ir);
				if (dv64) {
					ir += __builtin_ctzll(dv64) >> 3;
					break;
				}
			}
			while ((ir < limit) && (*ir == 0))
				ir++;
			run_length = ir - ip;
			if (run_length > MAX_ZERO_RUN_LENGTH)
				run_length = MAX_ZERO_RUN_LENGTH;
		} else {
			t = ((dv * 0x1824429d) >> (32 - D_BITS)) & D_MASK;
			m_pos = in + dict[t];
			dict[t] = (lzo_dict_t) (ip - in);
			if (dv != get_unaligned_le32(m_pos))
				goto literal;
		}

		ii -= ti;
		ti = 0;
		t = ip - ii;
		if (t != 0) {
			if (t <= 3) {
				op[*state_offset] |= t;
				COPY4(op, ii);
				op += t;
			} else if (t <= 16) {
				*op++ = (t - 3);
				COPY8(op, ii);
				COPY8(op + 8, ii + 8);
				op += t;
			} else {
				if (t <= 18) {
					*op++ = (t - 3);
				} else {
					size_t tt = t - 18;
					*op++ = 0;
					while (tt > 255) {
						tt -= 255;
						*op++ = 0;
					}
					*op++ = tt;
				}
				do {
					COPY8(op, ii);
					COPY8(op + 8, ii + 8);
					op += 16;
					ii += 16;
					t -= 16;
				} while (t >= 16);
				if (t > 0) do {
					*op++ = *ii++;
				} while (--t > 0);
			}
		}

		if (run_length) {
			ip += run_length;
			run_length -= MIN_ZERO_RUN_LENGTH;
			put_unaligned_le32((run_length << 21) | 0xfffc18
					   | (run_length & 0x7), op);
			op += 4;
			run_length = 0;
			*state_offset = -3;
			goto finished_writing_instruction;
		}

		m_len = 4;
		{
		u64 v;
		v = get_unaligned((const u64 *) (ip + m_len)) ^
		    get_unaligned((const u64 *) (m_pos + m_len));
		if (v == 0) {
			do {
				m_len += 8;
				v = get_unaligned((const u64 *) (ip + m_len)) ^
				    get_unaligned((const u64 *) (m_pos + m_len));
				if (ip + m_len >= ip_end)
					goto m_len_done;
			} while (v == 0);
		}
		m_len += (unsigned) __builtin_ctzll(v) / 8;
		}
m_len_done:

		m_off = ip - m_pos;
		ip += m_len;
		if (m_len <= M2_MAX_LEN && m_off <= M2_MAX_OFFSET) {
			m_off -= 1;
			*op++ = (((m_len - 1) << 5) | ((m_off & 7) << 2));
			*op++ = (m_off >> 3);
		} else if (m_off <= M3_MAX_OFFSET) {
			m_off -= 1;
			if (m_len <= M3_MAX_LEN)
				*op++ = (M3_MARKER | (m_len - 2));
			else {
				m_len -= M3_MAX_LEN;
				*op++ = M3_MARKER | 0;
				while (m_len > 255) {
					m_len -= 255;
					*op++ = 0;
				}
				*op++ = (m_len);
			}
			*op++ = (m_off << 2);
			*op++ = (m_off >> 6);
		} else {
			m_off -= 0x4000;
			if (m_len <= M4_MAX_LEN)
				*op++ = (M4_MARKER | ((m_off >> 11) & 8)
						| (m_len - 2));
			else {
				if (((m_off & 0x403f) == 0x403f)
						&& (m_len >= 261)
						&& (m_len <= 264)
						&& bitstream_version) {
					// Under lzo-rle, block copies
					// for 261 <= length <= 264 and
					// (distance & 0x80f3) == 0x80f3
					// can result in ambiguous
					// output. Adjust length
					// to 260 to prevent ambiguity.
					ip -= m_len - 260;
					m_len = 260;
				}
				m_len -= M4_MAX_LEN;
				*op++ = (M4_MARKER | ((m_off >> 11) & 8));
				while (m_len > 255) {
					m_len -= 255;
					*op++ = 0;
				}
				*op++ = (m_len);
			}
			*op++ = (m_off << 2);
			*op++ = (m_off >> 6);
		}
		*state_offset = -2;
finished_writing_instruction:
		ii = ip;
		goto next;
	}
	*out_len = op - out;
	return in_end - (ii - ti);
}

static int lzogeneric1x_1_compress(const unsigned char *in, size_t in_len,
		     unsigned char *out, size_t *out_len,
		     void *wrkmem, const unsigned char bitstream_version)
{
	const unsigned char *ip = in;
	unsigned char *op = out;
	unsigned char *data_start;
	size_t l = in_len;
	size_t t = 0;
	signed char state_offset = -2;
	unsigned int m4_max_offset;

	// LZO v0 will never write 17 as first byte (except for zero-length
	// input), so this is used to version the bitstream
	if (bitstream_version > 0) {
		*op++ = 17;
		*op++ = bitstream_version;
		m4_max_offset = M4_MAX_OFFSET_V1;
	} else {
		m4_max_offset = M4_MAX_OFFSET_V0;
	}

	data_start = op;

	while (l > 20) {
		size_t ll = min_t(size_t, l, m4_max_offset + 1);
		uintptr_t ll_end = (uintptr_t) ip + ll;
		if ((ll_end + ((t + ll) >> 5)) <= ll_end)
			break;
		memset(wrkmem, 0, D_SIZE * sizeof(lzo_dict_t));
		t = lzo1x_1_do_compress(ip, ll, op, out_len, t, wrkmem,
					&state_offset, bitstream_version);
		ip += ll;
		op += *out_len;
		l  -= ll;
	}
	t += l;

	if (t > 0) {
		const unsigned char *ii = in + in_len - t;

		if (op == data_start && t <= 238) {
			*op++ = (17 + t);
		} else if (t <= 3) {
			op[state_offset] |= t;
		} else if (t <= 18) {
			*op++ = (t - 3);
		} else {
			size_t tt = t - 18;
			*op++ = 0;
			while (tt > 255) {
				tt -= 255;
				*op++ = 0;
			}
			*op++ = tt;
		}
		if (t >= 16) do {
			COPY8(op, ii);
			COPY8(op + 8, ii + 8);
			op += 16;
			ii += 16;
			t -= 16;
		} while (t >= 16);
		if (t > 0) do {
			*op++ = *ii++;
		} while (--t > 0);
	}

	*op++ = M4_MARKER | 1;
	*op++ = 0;
	*op++ = 0;

	*out_len = op - out;
	return LZO_E_OK;
}

int lzo1x_compress(const unsigned char *src, uint32 src_len,
         unsigned char *dst, size_t *dst_len, void *wrkmem)
{
	return lzogeneric1x_1_compress(src, src_len, dst, dst_len, wrkmem , 0);
}


/* This MAX_255_COUNT is the maximum number of times we can add 255 to a base
 * count without overflowing an integer. The multiply will overflow when
 * multiplying 255 by more than MAXINT/255. The sum will overflow earlier
 * depending on the base count. Since the base count is taken from a u8
 * and a few bits, it is safe to assume that it will always be lower than
 * or equal to 2*255, thus we can always prevent any overflow by accepting
 * two less 255 steps. See Documentation/staging/lzo.rst for more information.
 */
#define MAX_255_COUNT      ((((size_t)~0) / 255) - 2)

int lzo1x_decompress_safe(const unsigned char *in, size_t in_len,
			  unsigned char *out, size_t *out_len)
{
	unsigned char *op;
	const unsigned char *ip;
	size_t t, next;
	size_t state = 0;
	const unsigned char *m_pos;
	const unsigned char * const ip_end = in + in_len;
	unsigned char * const op_end = out + *out_len;

	unsigned char bitstream_version;

	op = out;
	ip = in;

	if (in_len < 3)
		goto input_overrun;

	if ((in_len >= 5) && (*ip == 17)) {
		bitstream_version = ip[1];
		ip += 2;
	} else {
		bitstream_version = 0;
	}

	if (*ip > 17) {
		t = *ip++ - 17;
		if (t < 4) {
			next = t;
			goto match_next;
		}
		goto copy_literal_run;
	}

	for (;;) {
		t = *ip++;
		if (t < 16) {
			if (state == 0) {
				if (t == 0) {
					size_t offset;
					const unsigned char *ip_last = ip;

					while (*ip == 0) {
						ip++;
						NEED_IP(1);
					}
					offset = ip - ip_last;
					if (offset > MAX_255_COUNT)
						return LZO_E_ERROR;

					offset = (offset << 8) - offset;
					t += offset + 15 + *ip++;
				}
				t += 3;
copy_literal_run:
				if ((HAVE_IP(t + 15) && HAVE_OP(t + 15))) {
					const unsigned char *ie = ip + t;
					unsigned char *oe = op + t;
					do {
						COPY8(op, ip);
						op += 8;
						ip += 8;
						COPY8(op, ip);
						op += 8;
						ip += 8;
					} while (ip < ie);
					ip = ie;
					op = oe;
				} else
				{
					NEED_OP(t);
					NEED_IP(t + 3);
					do {
						*op++ = *ip++;
					} while (--t > 0);
				}
				state = 4;
				continue;
			} else if (state != 4) {
				next = t & 3;
				m_pos = op - 1;
				m_pos -= t >> 2;
				m_pos -= *ip++ << 2;
				TEST_LB(m_pos);
				NEED_OP(2);
				op[0] = m_pos[0];
				op[1] = m_pos[1];
				op += 2;
				goto match_next;
			} else {
				next = t & 3;
				m_pos = op - (1 + M2_MAX_OFFSET);
				m_pos -= t >> 2;
				m_pos -= *ip++ << 2;
				t = 3;
			}
		} else if (t >= 64) {
			next = t & 3;
			m_pos = op - 1;
			m_pos -= (t >> 2) & 7;
			m_pos -= *ip++ << 3;
			t = (t >> 5) - 1 + (3 - 1);
		} else if (t >= 32) {
			t = (t & 31) + (3 - 1);
			if (t == 2) {
				size_t offset;
				const unsigned char *ip_last = ip;

				while (*ip == 0) {
					ip++;
					NEED_IP(1);
				}
				offset = ip - ip_last;
				if (offset > MAX_255_COUNT)
					return LZO_E_ERROR;

				offset = (offset << 8) - offset;
				t += offset + 31 + *ip++;
				NEED_IP(2);
			}
			m_pos = op - 1;
			next = get_unaligned_le16(ip);
			ip += 2;
			m_pos -= next >> 2;
			next &= 3;
		} else {
			NEED_IP(2);
			next = get_unaligned_le16(ip);
			if (((next & 0xfffc) == 0xfffc) &&
			    ((t & 0xf8) == 0x18) &&
			    bitstream_version) {
				NEED_IP(3);
				t &= 7;
				t |= ip[2] << 3;
				t += MIN_ZERO_RUN_LENGTH;
				NEED_OP(t);
				memset(op, 0, t);
				op += t;
				next &= 3;
				ip += 3;
				goto match_next;
			} else {
				m_pos = op;
				m_pos -= (t & 8) << 11;
				t = (t & 7) + (3 - 1);
				if (t == 2) {
					size_t offset;
					const unsigned char *ip_last = ip;

					while (*ip == 0) {
						ip++;
						NEED_IP(1);
					}
					offset = ip - ip_last;
					if (offset > MAX_255_COUNT)
						return LZO_E_ERROR;

					offset = (offset << 8) - offset;
					t += offset + 7 + *ip++;
					NEED_IP(2);
					next = get_unaligned_le16(ip);
				}
				ip += 2;
				m_pos -= next >> 2;
				next &= 3;
				if (m_pos == op)
					goto eof_found;
				m_pos -= 0x4000;
			}
		}
		TEST_LB(m_pos);
		if (op - m_pos >= 8) {
			unsigned char *oe = op + t;
			if (HAVE_OP(t + 15)) {
				do {
					COPY8(op, m_pos);
					op += 8;
					m_pos += 8;
					COPY8(op, m_pos);
					op += 8;
					m_pos += 8;
				} while (op < oe);
				op = oe;
				if (HAVE_IP(6)) {
					state = next;
					COPY4(op, ip);
					op += next;
					ip += next;
					continue;
				}
			} else {
				NEED_OP(t);
				do {
					*op++ = *m_pos++;
				} while (op < oe);
			}
		} else
		{
			unsigned char *oe = op + t;
			NEED_OP(t);
			op[0] = m_pos[0];
			op[1] = m_pos[1];
			op += 2;
			m_pos += 2;
			do {
				*op++ = *m_pos++;
			} while (op < oe);
		}
match_next:
		state = next;
		t = next;
		if ((HAVE_IP(6) && HAVE_OP(4))) {
			COPY4(op, ip);
			op += t;
			ip += t;
		} else
		{
			NEED_IP(t + 3);
			NEED_OP(t);
			while (t > 0) {
				*op++ = *ip++;
				t--;
			}
		}
	}

eof_found:
	*out_len = op - out;
	return (t != 3       ? LZO_E_ERROR :
		ip == ip_end ? LZO_E_OK :
		ip <  ip_end ? LZO_E_INPUT_NOT_CONSUMED : LZO_E_INPUT_OVERRUN);

input_overrun:
	*out_len = op - out;
	return LZO_E_INPUT_OVERRUN;

output_overrun:
	*out_len = op - out;
	return LZO_E_OUTPUT_OVERRUN;

lookbehind_overrun:
	*out_len = op - out;
	return LZO_E_LOOKBEHIND_OVERRUN;
}

int lzo1x_decompress(const unsigned char *src, size_t src_len,
			  unsigned char *dst, size_t *dst_len)
{
  return lzo1x_decompress_safe(src, src_len, dst, dst_len);
}
