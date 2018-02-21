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
#include <sys/timeb.h>
#include <errno.h>
#include <turbojpeg.h>
#include <libdxt.h>
#include "xpfs.h"

struct DDS_PIXELFORMAT {
	unsigned int dwSize;
	unsigned int dwFlags;
	unsigned int dwFourCC;
	unsigned int dwRGBBitCount;
	unsigned int dwRBitMask;
	unsigned int dwGBitMask;
	unsigned int dwBBitMask;
	unsigned int dwABitMask;
};
struct DDS_HEADER {
	unsigned int dwMagic;
	unsigned int dwSize;
	unsigned int dwFlags;
	unsigned int dwHeight;
	unsigned int dwWidth;
	unsigned int dwPitchOrLinearSize;
	unsigned int dwDepth;
	unsigned int dwMipMapCount;
	unsigned int dwReserved1[11];
	DDS_PIXELFORMAT ddspf;
	unsigned int dwCaps;
	unsigned int dwCaps2;
	unsigned int dwCaps3;
	unsigned int dwCaps4;
	unsigned int dwReserved2;
};

int xpfs_dds_dxt1(char* src, unsigned char** dst) {
	struct timeb start, mid, end;
	
	if (DEBUG) {
		printf("DXT1: Doing intenal conversion with turbojpeg.\n");
		ftime(&start);
	}
	
	int fd = open(src, O_RDONLY);
	if (fd == -1) {
		fprintf(stderr, "DXT1: Could not open '%s' for read.\n", src);
		return -1;
	}
	
	size_t size = lseek(fd, 0, SEEK_END);
	unsigned char* jpeg = (unsigned char*)memalign(16, size);
	lseek(fd, 0, SEEK_SET);
	size = read(fd, jpeg, size);
	close(fd);

	tjhandle tj = tjInitDecompress();
	
	int width, height, subsamp, colourspace;
	if (tjDecompressHeader3(tj, jpeg, size, &width, &height, &subsamp, &colourspace) == -1) {
		fprintf(stderr, "DXT1: Could not decode header for '%s': %s\n", src, tjGetErrorStr());
		free(jpeg);
		return -1;
	}
	
	unsigned char* rgba = (unsigned char*)memalign(16, width * height * 4);
	if (tjDecompress2(tj, jpeg, size, rgba, 0, 0, 0, TJPF_RGBA, TJFLAG_FASTDCT|TJFLAG_FASTUPSAMPLE) == -1) {
		fprintf(stderr, "DXT1: Could not decode image for '%s': %s\n", src, tjGetErrorStr());
		free(jpeg);
		free(rgba);
		return -1;
	}
	free(jpeg);
	
	tjDestroy(tj);
	
	if (DEBUG) {
		ftime(&mid);
		int diff = (1000.0 * (mid.time - start.time) + (mid.millitm - start.millitm));
		printf("DXT1: JPEG decode done in %d ms.\n", diff);
	}

	unsigned char* dds = (unsigned char*)memalign(16, width * height * 4);
	DDS_HEADER header;
	memset(&header, 0, sizeof(header));
	header.dwMagic = 0x20534444;
	header.dwSize = 124;
	header.dwFlags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000;
	header.dwHeight = height;
	header.dwWidth = width;
	
	int mips = 0;
	int totalsize = sizeof(header);
	while ((height >> mips) >= 8 && (width >> mips) >= 8) {
		totalsize += (height >> mips) * (width >> mips) / 2;
		mips++;
	}
	if (DEBUG) printf("DXT1: Allocating %d bytes for %d mips.\n", totalsize, mips);
	header.dwMipMapCount = mips;
	
	DDS_PIXELFORMAT ddspix;
	memset(&ddspix, 0, sizeof(ddspix));
	ddspix.dwSize = 32;
	ddspix.dwFlags = 0x4;
	ddspix.dwFourCC = 'D' | 'X'<<8 | 'T'<<16 | '1'<<24;
	header.ddspf = ddspix;
	
	header.dwCaps = 0x1000 | 0x8 | 0x400000;

	*dst = (unsigned char*)memalign(16, totalsize);
	unsigned char* dstpos = *dst;

	memcpy(dstpos, &header, sizeof(header));
	dstpos += sizeof(header);

	int bytes;
	CompressImageDXT1(rgba, dds, width, height, bytes);
	if (bytes != width * height / 2) printf("DXT1: DXT data size %d, expected %d.\n", bytes, width * height / 2);
	memcpy(dstpos, dds, bytes);
	dstpos += bytes;

	*dst = (unsigned char*)realloc(*dst, totalsize + bytes);
	memcpy((*dst) + totalsize, dds, bytes);
	totalsize += bytes;

	int curmip = 0;
	while (width > 8 && height > 8) {
		if (DEBUG >= 2) printf("DXT1: Resample mip %d (%d x %d)\n", ++curmip, width, height);
		unsigned char* nextmip = (unsigned char*)memalign(16, width * height * 4);
		halveimage(rgba, width, height, nextmip);
		width >>= 1;
		height >>= 1;
		free(rgba);
		rgba = nextmip;
		
		if (DEBUG >= 2) printf("DXT1: Compress mip %d (%d x %d)\n", curmip, width, height);
		CompressImageDXT1(rgba, dds, width, height, bytes);
		if (bytes != width * height / 2) printf("DXT1: DXT data size %d, expected %d.\n", bytes, width * height / 2);
		
		memcpy(dstpos, dds, bytes);
		dstpos += bytes;
		if (DEBUG >= 2) printf("DXT1: Done mip %d.\n", curmip);
	}
	free(rgba);
	
	
	if (DEBUG) {
		ftime(&end);
		int diff = (1000.0 * (end.time - mid.time) + (end.millitm - mid.millitm));
		printf("DXT1: DXT1 encode done in %d ms.\n", diff);
		diff = (1000.0 * (end.time - start.time) + (end.millitm - start.millitm));
		printf("DXT1: Total time %d ms.\n", diff);
	}
	return totalsize;
}


int xpfs_dds_rgb(char* src, unsigned char** dst) {
	struct timeb start, mid, end;
	
	if (DEBUG) {
		printf("RGB: Doing intenal conversion with turbojpeg.\n");
		ftime(&start);
	}
	
	int fd = open(src, O_RDONLY);
	if (fd == -1) {
		fprintf(stderr, "RGB: Could not open '%s' for read.\n", src);
		return -1;
	}
	
	size_t size = lseek(fd, 0, SEEK_END);
	unsigned char* jpeg = (unsigned char*)memalign(16, size);
	lseek(fd, 0, SEEK_SET);
	size = read(fd, jpeg, size);
	close(fd);

	tjhandle tj = tjInitDecompress();
	
	int width, height, subsamp, colourspace;
	if (tjDecompressHeader3(tj, jpeg, size, &width, &height, &subsamp, &colourspace) == -1) {
		fprintf(stderr, "RGB: Could not decode header for '%s': %s\n", src, tjGetErrorStr());
		free(jpeg);
		return -1;
	}
	
	
	// We know the dimensions, preallocate everything.
	DDS_HEADER header;
	memset(&header, 0, sizeof(header));
	header.dwMagic = 0x20534444;
	header.dwSize = 124;
	header.dwFlags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000;
	header.dwHeight = height;
	header.dwWidth = width;
	
	int mips = 0;
	int totalsize = sizeof(header);
	while ((height >> mips) >= 8 && (width >> mips) >= 8) {
		totalsize += (height >> mips) * (width >> mips) * 4;
		mips++;
	}
	if (DEBUG) printf("RGB: Allocating %d bytes for %d mips.\n", totalsize, mips);
	header.dwMipMapCount = mips;
	
	DDS_PIXELFORMAT ddspix;
	memset(&ddspix, 0, sizeof(ddspix));
	ddspix.dwSize = 32;
	ddspix.dwFlags = 0x40;
	ddspix.dwRGBBitCount = 32;
	ddspix.dwRBitMask = 0x00FF0000;
	ddspix.dwGBitMask = 0x0000FF00;
	ddspix.dwBBitMask = 0x000000FF;
	header.ddspf = ddspix;
	
	header.dwCaps = 0x1000 | 0x8 | 0x400000;
	
	*dst = (unsigned char*)memalign(16, totalsize);
	unsigned char* dstpos = *dst;

	memcpy(dstpos, &header, sizeof(header));
	dstpos += sizeof(header);
	
	
	// Decompress straight into the output buffer.
	if (tjDecompress2(tj, jpeg, size, dstpos, 0, 0, 0, TJPF_BGRA, TJFLAG_FASTDCT|TJFLAG_FASTUPSAMPLE) == -1) {
		fprintf(stderr, "RGB: Could not decode image for '%s': %s\n", src, tjGetErrorStr());
		free(jpeg);
		free(*dst);
		(*dst) = 0;
		return -1;
	}
	free(jpeg);
	int bytes = width * height * 4;
	dstpos += bytes;	
	tjDestroy(tj);
	
	if (DEBUG) {
		ftime(&mid);
		int diff = (1000.0 * (mid.time - start.time) + (mid.millitm - start.millitm));
		printf("RGB: JPEG decode done in %d ms.\n", diff);
	}


	int curmip = 0;
	while (width > 8 && height > 8) {
		if (DEBUG >= 2) printf("RGB: Resample mip %d (%d x %d)\n", ++curmip, width, height);
		unsigned char* nextmip = (unsigned char*)memalign(16, width * height * 4);
		halveimage(dstpos-bytes, width, height, dstpos);
		width >>= 1;
		height >>= 1;
		bytes = width * height * 4;
		dstpos += bytes;
		if (DEBUG >= 2) printf("RGB: Done mip %d.\n", curmip);
	}
	
	if (DEBUG >= 2) printf("RGB: Wrote a total of %d bytes.\n", (int)(dstpos-(*dst)));
	
	if (DEBUG) {
		ftime(&end);
		int diff = (1000.0 * (end.time - mid.time) + (end.millitm - mid.millitm));
		printf("RGB: Mipmap generation done in %d ms.\n", diff);
		diff = (1000.0 * (end.time - start.time) + (end.millitm - start.millitm));
		printf("RGB: Total time %d ms.\n", diff);
	}
	return totalsize;
}