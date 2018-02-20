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

void halveimage(const unsigned char* src, int width, int height, unsigned char* dst);

int xpfs_dds_dxt1(char* src, unsigned char** dst);
int xpfs_dds_rgb(char* src, unsigned char** dst);


#endif