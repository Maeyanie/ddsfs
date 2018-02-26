#ifndef XPFS_H
#define XPFS_H

#ifndef DEBUG
#define DEBUG config.debug
#endif

extern struct Config {
	char* basepath;
	int cache;
	int compress;
	int debug;
} config;

extern inline int poweroftwo(unsigned int x) {
  return !(x & (x - 1));
}

void halveimage(const unsigned char* src, int width, int height, unsigned char* dst);

int xpfs_jpg_dxt1(char* src, unsigned char** dst);
int xpfs_jpg_rgb(char* src, unsigned char** dst);
int xpfs_webp_dxt1(char* src, unsigned char** dst);
int xpfs_webp_rgb(char* src, unsigned char** dst);


#endif