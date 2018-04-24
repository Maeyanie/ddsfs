#ifndef XPFS_H
#define XPFS_H

#ifndef DEBUG
#define DEBUG config.debug
#endif

extern struct Config {
	char* basepath;
	unsigned int cache;
	int compress;
	int debug;
} config;

extern inline int poweroftwo(unsigned int x) {
  return !(x & (x - 1));
}

void halveimage(const unsigned char* src, int width, int height, unsigned char* dst);

int ddsfs_jpg_dxt1(char* src, unsigned char** dst);
int ddsfs_jpg_rgb(char* src, unsigned char** dst);
int ddsfs_webp_dxt1(char* src, unsigned char** dst);
int ddsfs_webp_rgb(char* src, unsigned char** dst);


#endif