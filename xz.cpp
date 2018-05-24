/*
	This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <lzma.h>
#include "ddsfs.h"

static const uint8_t XZ_MAGIC[6] = { 0xFD, '7', 'z', 'X', 'Z', 0x00 };

struct __attribute__((packed)) XZHeader {
	uint8_t magic[6];
	uint8_t flags1;
	uint8_t flags2;
	uint32_t crc;
};
struct __attribute__((packed)) XZFooter {
	uint32_t crc;
	uint32_t len;
	uint16_t flags;
	uint8_t magic[2];
};
struct __attribute__((packed)) XZBlockHeader {
	uint8_t headersize, flags;
	uint8_t data[];
};


static size_t decode(const uint8_t* buf, size_t size_max, uint64_t *num) {
	if (size_max == 0)
		return 0;

	if (size_max > 9)
		size_max = 9;

	*num = buf[0] & 0x7F;
	size_t i = 0;

	while (buf[i++] & 0x80) {
		if (i >= size_max || buf[i] == 0x00)
			return 0;

		*num |= (uint64_t)(buf[i] & 0x7F) << (i * 7);
	}

	return i;
}
 
 
int ddsfs_xz_header(const char* src, int* size) {
	int fd = open(src, O_RDONLY);
	if (fd <= 0) return -1;
	
	XZHeader header;
	read(fd, &header, sizeof(header));
	
	if (memcmp(header.magic, XZ_MAGIC, 6)) {
		close(fd);
		fprintf(stderr, "XZ: Bad ID in .xz file: %s\n", src);
		return -1;
	}
	
	XZBlockHeader bh;
	read(fd, &bh, 2);
	
	int len = (bh.headersize + 1) * 4;
	uint8_t headerdata[len-2];
	read(fd, headerdata, len-2);
	
	uint64_t csize = 0, usize = 0;
	uint8_t* cur =  headerdata;
	
	if (bh.flags & 0x40) cur += decode(cur, 9, &csize);
	if (bh.flags & 0x80) {
		cur += decode(cur, 9, &usize);
		close(fd);
		*size = usize;
		return 0;
	}
	
	close(fd);
	fprintf(stderr, "XZ: Header did not contain uncompressed size in .xz file: %s\n", src);
	return -1;
}



int ddsfs_xz(const char* src, unsigned char** dst) {
	int fd = open(src, O_RDONLY);
	if (fd <= 0) return -1;
	
	long len = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);
	
	uint8_t* data = (uint8_t*)malloc(len);
	read(fd, data, len);
	close(fd);
	
	
	XZHeader* header = (XZHeader*)data;
	
	if (memcmp(header->magic, XZ_MAGIC, 6)) {
		free(data);
		fprintf(stderr, "XZ: Bad ID in .xz file: %s\n", src);
		return -1;
	}
	
	
	XZBlockHeader* bh = (XZBlockHeader*)(data + sizeof(XZHeader));
	uint64_t csize = 0, usize = 0;
	uint8_t* cur =  bh->data;
	
	if (bh->flags & 0x40) cur += decode(cur, 9, &csize);
	if (bh->flags & 0x80) {
		cur += decode(cur, 9, &usize);
	} else {
		usize = len * 2;
		fprintf(stderr, "XZ: Header did not contain uncompressed size, allocating %lu bytes, in .xz file: %s\n", usize, src);
	}
	
	
	*dst = (unsigned char*)memalign(16, usize);
	
	lzma_stream xz = LZMA_STREAM_INIT;
	lzma_ret ret = lzma_stream_decoder(&xz, UINT64_MAX, 0);
	xz.next_in = data;
	xz.avail_in = len;
	xz.next_out = *dst;
	xz.avail_out = usize;
	do {
		ret = lzma_code(&xz, LZMA_FINISH);
		if (ret == LZMA_OK) {
			fprintf(stderr, "XZ: Allocated size %lu was insufficient, in .xz file: %s\n", usize, src);
			xz.avail_out += usize;
			usize *= 2;
			*dst = (unsigned char*)realloc(*dst, usize);
			continue;
		}
	} while (false);
	
	lzma_end(&xz);
	
	if (ret != LZMA_STREAM_END) {
		fprintf(stderr, "XZ: Error %d decoding .xz file: %s\n", ret, src);
		free(*dst);
		*dst = NULL;
		return -1;
	} else {
		return (usize - xz.avail_out);
	}
}


