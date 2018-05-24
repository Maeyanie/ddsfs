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
#include <zlib.h>
#include "ddsfs.h"


struct __attribute__((packed)) GZHeader {
	uint8_t id1, id2, cm, flg;
	uint32_t mtime;
	uint8_t xfl, os;
};
struct __attribute__((packed)) GZFooter {
	uint32_t crc, len;
};


int ddsfs_gzip_header(const char* src, int* size) {
	int fd = open(src, O_RDONLY);
	if (fd <= 0) return -1;
	
	GZHeader header;
	read(fd, &header, 2);
	if (header.id1 != 31 || header.id2 != 139) {
		close(fd);
		fprintf(stderr, "GZIP: Bad ID in .gz file: %s\n", src);
		return -1;
	}
	
	lseek(fd, -4, SEEK_END);
	read(fd, size, 4);
	close(fd);
	return 0;
}



int ddsfs_gzip(const char* src, unsigned char** dst) {
	int fd = open(src, O_RDONLY);
	if (fd <= 0) {
		fprintf(stderr, "GZIP: Could not open .gz file: %s\n", src);
		return -1;
	}
	
	GZFooter footer;
	int len = lseek(fd, -sizeof(footer), SEEK_END);
	read(fd, &footer, sizeof(footer));
	lseek(fd, 0, SEEK_SET);
	
	if (DEBUG >= 2) printf("GZIP: Decompressing %d bytes to %u for .gz file: %s\n", len, footer.len, src);
	
	gzFile gd = gzdopen(fd, "rb");
	if (!gd) {
		close(fd);
		return -1;
	}
	
	*dst = (unsigned char*)memalign(16, footer.len);
	
	len = gzread(gd, *dst, footer.len);
	if ((unsigned)len != footer.len) {
		fprintf(stderr, "GZIP: Decompressing gave %d bytes of %u expected for .gz file: %s\n\tError was: %s\n", 
			len, footer.len, src, gzerror(gd, NULL));
	}
	
	int ret = gzclose(gd);
	if (ret != Z_OK) {
		fprintf(stderr, "GZIP: Decompressing gave error code %d for .gz file: %s\n", ret, src);
		free(*dst);
		*dst = NULL;
		return -1;
	}
	
	return len;
}


