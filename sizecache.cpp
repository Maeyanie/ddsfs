#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <unordered_map>
#include "ddsfs.h"
using namespace std;

// Using int will fail with DDS files over 2 GB... but I think that's acceptable for now.
// Not bothering to use any sort of expiry, since this will be able to store thousands of files per MB of RAM.
static unordered_map<string,int> sizecache;


int dds_size(int width, int height, int alpha) {
	int pixels = 0;
	int mips = 0;
	// This really seems like it could be done with a single formula.
	while ((height >> mips) >= MINSIZE && (width >> mips) >= MINSIZE) {
		pixels += (height >> mips) * (width >> mips);
		mips++;
	}
	if (config.compress) {
		if (alpha) return sizeof(DDS_HEADER) + (pixels);
		return sizeof(DDS_HEADER) + (pixels/2);
	} else {
		return sizeof(DDS_HEADER) + (pixels*4);
	}
}

int sizecache_get(const char* name) {
	auto i = sizecache.find(name);
	if (i != sizecache.end()) {
		return i->second;
	}
	return -1;
}

void sizecache_set(const char* name, int size) {
	sizecache[name] = size;
}