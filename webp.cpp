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
#include <webp/decode.h>
#include <libdxt.h>
#include "ddsfs.h"

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



int ddsfs_webp_dxt1(char* src, unsigned char** dst) {
	struct timeb start, mid, end;
	
	if (DEBUG) {
		printf("DXT: Doing intenal conversion with WebPDecoder.\n");
		ftime(&start);
	}
	
	int fd = open(src, O_RDONLY);
	if (fd == -1) {
		fprintf(stderr, "DXT: Could not open '%s' for read.\n", src);
		return -1;
	}
	
	size_t size = lseek(fd, 0, SEEK_END);
	unsigned char* webp = (unsigned char*)memalign(16, size);
	lseek(fd, 0, SEEK_SET);
	size = read(fd, webp, size);
	close(fd);


	WebPBitstreamFeatures wpbf;
	VP8StatusCode rc = WebPGetFeatures(webp, size, &wpbf);
	if (rc != VP8_STATUS_OK) {
		fprintf(stderr, "DXT: Could not decode header for '%s': %d\n", src, rc);
		free(webp);
		return -1;
	}
	if (DEBUG >= 2) printf("DXT: Decoded %d x %d WebP %s alpha.\n", wpbf.width, wpbf.height, 
							wpbf.has_alpha?"width":"without");

	int width = wpbf.width, height = wpbf.height;
	if (!poweroftwo(width) || !poweroftwo(height)) {
		if (DEBUG) printf("DXT: Not a power-of-two texture, falling back to RGB.\n");
		free(webp);
		return ddsfs_webp_rgb(src, dst);
	}
	
	unsigned char* rgba = (unsigned char*)memalign(16, width*height*4);
		
	if (WebPDecodeRGBAInto(webp, size, rgba, width*height*4, width*4) == NULL) {
		fprintf(stderr, "DXT: Could not decode image for '%s': %d\n", src, rc);
		free(webp);
		free(rgba);
		return -1;		
	}

	if (DEBUG) {
		ftime(&mid);
		int diff = (1000.0 * (mid.time - start.time) + (mid.millitm - start.millitm));
		printf("DXT: WebP decode done in %d ms.\n", diff);
	}

	DDS_HEADER header;
	memset(&header, 0, sizeof(header));
	header.dwMagic = 0x20534444;
	header.dwSize = 124;
	header.dwFlags = 0x1 | 0x2 | 0x4 | 0x1000;
	header.dwHeight = height;
	header.dwWidth = width;
	header.dwCaps = 0x1000;
	
	int mips = 0;
	int totalsize = sizeof(header);
	if (wpbf.has_alpha) {
		while ((height >> mips) > 8 && (width >> mips) > 8) {
			totalsize += (height >> mips) * (width >> mips);
			mips++;
		}
	} else {
		while ((height >> mips) > 8 && (width >> mips) > 8) {
			totalsize += (height >> mips) * (width >> mips) / 2;
			mips++;
		}
	}
	mips++;
	
	if (DEBUG) printf("DXT: Allocating %d bytes for %d mip%s.\n", totalsize, mips, mips==1?"":"s");
	header.dwFlags |= 0x20000;
	header.dwMipMapCount = mips;
	header.dwCaps |= 0x8 | 0x400000;
	
	DDS_PIXELFORMAT ddspix;
	memset(&ddspix, 0, sizeof(ddspix));
	ddspix.dwSize = 32;
	ddspix.dwFlags = 0x4;
	if (wpbf.has_alpha) {
		ddspix.dwFourCC = 'D' | 'X'<<8 | 'T'<<16 | '5'<<24;
	} else {
		ddspix.dwFourCC = 'D' | 'X'<<8 | 'T'<<16 | '1'<<24;
	}
	header.ddspf = ddspix;

	*dst = (unsigned char*)memalign(16, totalsize);
	unsigned char* dstpos = *dst;

	memcpy(dstpos, &header, sizeof(header));
	dstpos += sizeof(header);

	unsigned char* dds = (unsigned char*)memalign(16, width*height*4);
	int bytes;
	if (wpbf.has_alpha) {
		CompressImageDXT5(rgba, dstpos, width, height, bytes);
	} else {
		CompressImageDXT1(rgba, dstpos, width, height, bytes);
	}
	dstpos += bytes;

	int curmip = 0;
	if (mips > 0) {
		while (width > 8 && height > 8) {
			if (DEBUG >= 2) printf("DXT: Resample mip %d (%d x %d)\n", ++curmip, width, height);
			unsigned char* nextmip = (unsigned char*)memalign(16, width * height * 4);
			halveimage(rgba, width, height, nextmip);
			width >>= 1;
			height >>= 1;
			free(rgba);
			rgba = nextmip;
			
			if (DEBUG >= 2) printf("DXT: Compress mip %d (%d x %d)\n", curmip, width, height);
			if (wpbf.has_alpha) {
				CompressImageDXT5(rgba, dstpos, width, height, bytes);
			} else {
				CompressImageDXT1(rgba, dstpos, width, height, bytes);
			}
			dstpos += bytes;
			if (DEBUG >= 2) printf("DXT: Done mip %d.\n", curmip);
		}
	}
	free(rgba);
	
	
	if (DEBUG) {
		ftime(&end);
		int diff = (1000.0 * (end.time - mid.time) + (end.millitm - mid.millitm));
		printf("DXT: DXT encode done in %d ms.\n", diff);
		diff = (1000.0 * (end.time - start.time) + (end.millitm - start.millitm));
		printf("DXT: Total time %d ms.\n", diff);
	}
	return totalsize;
}


int ddsfs_webp_rgb(char* src, unsigned char** dst) {
	struct timeb start, mid, end;
	
	if (DEBUG) {
		printf("RGB: Doing intenal conversion with WebPDecoder.\n");
		ftime(&start);
	}
	
	int fd = open(src, O_RDONLY);
	if (fd == -1) {
		fprintf(stderr, "RGB: Could not open '%s' for read.\n", src);
		return -1;
	}
	
	size_t size = lseek(fd, 0, SEEK_END);
	unsigned char* webp = (unsigned char*)memalign(16, size);
	lseek(fd, 0, SEEK_SET);
	size = read(fd, webp, size);
	close(fd);

	WebPBitstreamFeatures wpbf;
	VP8StatusCode rc = WebPGetFeatures(webp, size, &wpbf);
	if (rc != VP8_STATUS_OK) {
		fprintf(stderr, "RGB: Could not decode header for '%s': %d\n", src, rc);
		free(webp);
		return -1;
	}
	if (DEBUG >= 2) printf("RGB: Decoded %d x %d WebP %s alpha.\n", wpbf.width, wpbf.height, 
							wpbf.has_alpha?"width":"without");
	int width = wpbf.width, height = wpbf.height;
	
	
	// We know the dimensions, preallocate everything.
	DDS_HEADER header;
	memset(&header, 0, sizeof(header));
	header.dwMagic = 0x20534444;
	header.dwSize = 124;
	header.dwFlags = 0x1 | 0x2 | 0x4 | 0x1000;
	header.dwHeight = height;
	header.dwWidth = width;
	header.dwCaps = 0x1000;
	
	int mips = 0;
	int totalsize = sizeof(header);
	if (poweroftwo(width) && poweroftwo(height)) {
		while ((height >> mips) > 8 && (width >> mips) > 8) {
			totalsize += (height >> mips) * (width >> mips) * 4;
			mips++;
		}
		mips++;
		if (DEBUG) printf("RGB: Allocating %d bytes for %d mip%s.\n", totalsize, mips, mips==1?"":"s");
		header.dwFlags |= 0x20000;
		header.dwMipMapCount = mips;
		header.dwCaps |= 0x8 | 0x400000;
	} else {
		totalsize += width * height * 4;
		if (DEBUG) printf("RGB: Allocating %d bytes for non-power-of-two texture.\n", totalsize);
	}
	
	DDS_PIXELFORMAT ddspix;
	memset(&ddspix, 0, sizeof(ddspix));
	ddspix.dwSize = 32;
	ddspix.dwFlags = 0x40;
	ddspix.dwRGBBitCount = 32;
	ddspix.dwRBitMask = 0x00FF0000;
	ddspix.dwGBitMask = 0x0000FF00;
	ddspix.dwBBitMask = 0x000000FF;
	if (wpbf.has_alpha) ddspix.dwABitMask = 0xFF000000;
	header.ddspf = ddspix;
	
	*dst = (unsigned char*)memalign(16, totalsize+16);
	unsigned char* dstpos = *dst;

	memcpy(dstpos, &header, sizeof(header));
	dstpos += sizeof(header);
	
	
	// Decompress straight into the output buffer.
	if (WebPDecodeBGRAInto(webp, size, dstpos, width*height*4, width*4) == NULL) {
		fprintf(stderr, "RGB: Could not decode image for '%s'\n", src);
		free(webp);
		free(*dst);
		(*dst) = 0;
		return -1;
	}
	free(webp);
	int bytes = width * height * 4;
	dstpos += bytes;
	
	if (DEBUG) {
		ftime(&mid);
		int diff = (1000.0 * (mid.time - start.time) + (mid.millitm - start.millitm));
		printf("RGB: WebP decode done in %d ms.\n", diff);
	}


	if (mips > 0) {
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