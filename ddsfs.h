#ifndef XPFS_H
#define XPFS_H

#ifndef DEBUG
#define DEBUG config.debug
#endif

extern struct Config {
	char* basepath;
	unsigned int cache;
	char compress;
	char debug;
	char size;
} config;

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

extern inline int poweroftwo(unsigned int x) {
  return !(x & (x - 1));
}

void halveimage(const unsigned char* src, int width, int height, unsigned char* dst);

int ddsfs_jpg_header(char* src, int* width, int* height);
int ddsfs_jpg_dxt1(char* src, unsigned char** dst);
int ddsfs_jpg_rgb(char* src, unsigned char** dst);

int ddsfs_webp_dxt1(char* src, unsigned char** dst);
int ddsfs_webp_rgb(char* src, unsigned char** dst);


#endif