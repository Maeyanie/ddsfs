// Based on code from https://stackoverflow.com/questions/8052040/fastest-50-scaling-of-argb32-images-using-sse-intrinsics

#include <intrin.h>

/*
 * Calculates the average of two rgb32 pixels.
 */
inline static unsigned int avg2(unsigned int a, unsigned int b) {
    return (((a^b) & 0xfefefefeUL) >> 1) + (a&b);
}

/*
 * Calculates the average of four rgb32 pixels.
 */
inline static unsigned int avg4(const unsigned int a[2], const unsigned int b[2]) {
    return avg2(avg2(a[0], a[1]), avg2(b[0], b[1]));
}

/*
 * Calculates the average of two rows of rgb32 pixels.
 */
inline static void average2Rows(const unsigned int* src_row1, const unsigned int* src_row2, unsigned int* dst_row, int w) {
#if !defined(__SSE__)
#warning Warning: Compiled without SSE support.
	for (int x = w; x; --x, dst_row++, src_row1 += 2, src_row2 += 2) {
		*dst_row = avg4(src_row1, src_row2);
	}
#else
	for (int x = w; x; x-=4, dst_row+=4, src_row1 += 8, src_row2 += 8) {
		__m128i left  = _mm_avg_epu8(_mm_load_si128((__m128i const*)src_row1), _mm_load_si128((__m128i const*)src_row2));
		__m128i right = _mm_avg_epu8(_mm_load_si128((__m128i const*)(src_row1+4)), _mm_load_si128((__m128i const*)(src_row2+4)));

		__m128i t0 = _mm_unpacklo_epi32( left, right ); // right.m128i_u32[1] left.m128i_u32[1] right.m128i_u32[0] left.m128i_u32[0]
		__m128i t1 = _mm_unpackhi_epi32( left, right ); // right.m128i_u32[3] left.m128i_u32[3] right.m128i_u32[2] left.m128i_u32[2]
		__m128i shuffle1 = _mm_unpacklo_epi32( t0, t1 );    // right.m128i_u32[2] right.m128i_u32[0] left.m128i_u32[2] left.m128i_u32[0]
		__m128i shuffle2 = _mm_unpackhi_epi32( t0, t1 );    // right.m128i_u32[3] right.m128i_u32[1] left.m128i_u32[3] left.m128i_u32[1]

		_mm_store_si128((__m128i *)dst_row, _mm_avg_epu8(shuffle1, shuffle2));
	}
#endif
}

void halveimage(const unsigned char* src, int width, int height, unsigned char* dst) {
	const unsigned int* isrc = (const unsigned int*)src;
	unsigned int* idst = (unsigned int*)dst;
	for (int r = 0; r < height; r+=2) {
		average2Rows(isrc + (r*width), isrc + ((r+1)*width), idst + ((r/2)*(width/2)), width);
	}
}