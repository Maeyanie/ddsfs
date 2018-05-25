#ifndef XPFS_H
#define XPFS_H

#include "config.h"

#ifndef DEBUG
#define DEBUG config.debug
#endif

#define MINSIZE 16

#include <unordered_map>
#include <string>

enum {
	CACHE_NONE,
	CACHE_DISK,
	CACHE_MEM,
};
extern struct Config {
	char* basepath;
	unsigned int cache;
	char compress;
	char debug;
	char size;
	// ASan reports fuse option parsing going off the end of the array, and I can't be bothered fixing fuse.
	char deadspace[32];
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

int dds_size(int width, int height, int alpha=0);
int sizecache_get(const char* name);
void sizecache_set(const char* name, int size);

void memcache_init();
int memcache_getfd(const std::string& name);
int memcache_store(const std::string& name, unsigned char* dds, unsigned int len);
int memcache_read(int fd, char* buf, size_t size, off_t offset);
int memcache_release(int fd);

#if USE_JPG
int ddsfs_jpg_header(const char* src, int* width, int* height);
int ddsfs_jpg_dxt1(char* src, unsigned char** dst);
int ddsfs_jpg_rgb(char* src, unsigned char** dst);
#endif

#if USE_WEBP
int ddsfs_webp_header(const char* src, int* width, int* height, int* alpha);
int ddsfs_webp_dxt1(char* src, unsigned char** dst);
int ddsfs_webp_rgb(char* src, unsigned char** dst);
#endif

#if USE_GZIP
int ddsfs_gzip_header(const char* src, int* size);
int ddsfs_gzip(const char* src, unsigned char** dst);
#endif

#if USE_XZ
int ddsfs_xz_header(const char* src, int* size);
int ddsfs_xz(const char* src, unsigned char** dst);
#endif



#endif