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
	
#define FUSE_USE_VERSION 26
#define _XOPEN_SOURCE 700
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h> 
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <fuse.h>
#include <atomic>
#include <algorithm>
#include <unordered_map>
#include <list>
#include "ddsfs.h"
using namespace std;



class CacheEntry {
public:
	string name;
	unsigned char* data;
	unsigned int len;
	int refs;
	
	CacheEntry(const char* n, unsigned char* d, unsigned int l) {
		name = n;
		data = d;
		len = l;
		refs = 1;
	}
	~CacheEntry() {
		if (refs > 1) fprintf(stderr, "Warning: Deleting memory-cached file with %d refs!\n", refs);
		if (data) free(data);
	}
};


struct fuse_operations oper;
struct Config config;
enum {
	KEY_HELP,
};
enum {
	CACHE_NONE,
	CACHE_DISK,
	CACHE_MEM,
};

#define DDSFS_OPT(t, p, v) { t, offsetof(struct Config, p), v }
static struct fuse_opt ddsfs_opts[] = {
	DDSFS_OPT("dxt1",			compress, 1),
	DDSFS_OPT("dxt",			compress, 1),
	DDSFS_OPT("rgb",			compress, 0),
	DDSFS_OPT("cache",			cache, 1),
	DDSFS_OPT("cache=%i",		cache, 0),
	DDSFS_OPT("nocache",		cache, 0),
	DDSFS_OPT("size",			size, 1),
	DDSFS_OPT("nosize",			size, 0),
	DDSFS_OPT("verbose",		debug, 1),
	DDSFS_OPT("verbose=%i",		debug, 0),
	DDSFS_OPT("--verbose",		debug, 1),
	DDSFS_OPT("--verbose=%i",	debug, 0),
	
	FUSE_OPT_KEY("-h",			KEY_HELP),
	FUSE_OPT_KEY("--help",		KEY_HELP),
	FUSE_OPT_END
};

static pthread_rwlock_t cachelock;
static unordered_map<int,CacheEntry*> memcache;
static unordered_map<string,CacheEntry*> memindex;
static list<string> memlru;
static int nextfd = 100;


static int ddsfs_opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs)
{
     switch (key) {
     case KEY_HELP:
		fprintf(stderr,
			"usage: %s <scenery> <mount> [options]\n"
			"\n"
			"general options:\n"
			"    -o opt,[opt...]        mount options\n"
			"    -h   --help            print help\n"
			"\n"
			"DDSFS options:\n"
			"    -o dxt                 Convert to DXT1/DXT5 (default)\n"
			"    -o rgb                 Convert to RGB/RGBA\n"
			"    -o cache=0             Cache DDS files in memory only as long as they are open\n"
			"    -o cache=1             Save DDS files on disk until manually removed (default)\n"
			"    -o cache=#             Cache up to # DDS files in memory, removed on a least-recently-used basis\n"
			"    -o size                Calculate sizes for fake DDS files. Slow, but some programs need it\n"
			"    -o nosize              Give fake DDS file sizes as the source file size (default)\n"
			"    -o nocache             Equivalent to -o cache=0\n"
			"    -o verbose[=#]         Set level of information on DDSFS's operations\n"
			"\n", outargs->argv[0]);
		fuse_opt_add_arg(outargs, "-ho");
		fuse_main(outargs->argc, outargs->argv, &oper, NULL);
		exit(1);
	 case FUSE_OPT_KEY_NONOPT:
		if (config.basepath == NULL) {
			config.basepath = strdup(arg);
			return 0;
		}
     }
     return 1;
}


static int ddsfs_getattr(const char *path, struct stat *stbuf)
{
	int res;
	char rwpath[strlen(config.basepath)+strlen(path)+1];
	char* ext;

	sprintf(rwpath, "%s%s", config.basepath, path);
	if (DEBUG >= 3) printf("getattr: %s\n", rwpath);

	res = lstat(rwpath, stbuf);
	if (res == -1) {
		if (DEBUG >= 3) printf("getattr: lstat failed on '%s', looking for alternates.\n", rwpath);
		ext = strrchr(rwpath, '.');
		if (!ext) return -errno;
		if (!strcasecmp(ext, ".dds")) {
			strcpy(ext, ".jpg");
			res = lstat(rwpath, stbuf);
			if (res == 0) {
				int size = sizecache_get(rwpath);
				if (size != -1) {
					stbuf->st_size = size;
				} else if (config.size) {
					int width, height;
					res = ddsfs_jpg_header(rwpath, &width, &height);
					if (res != 0) return -errno;
					
					stbuf->st_size = dds_size(width, height);
					sizecache_set(rwpath, stbuf->st_size);
				}
				return 0;
			}
			
			strcpy(ext, ".webp");
			res = lstat(rwpath, stbuf);
			if (res == 0) {
				int size = sizecache_get(rwpath);
				if (size != -1) {
					stbuf->st_size = size;
				} else if (config.size) {
					int width, height, alpha;
					res = ddsfs_webp_header(rwpath, &width, &height, &alpha);
					if (res != 0) return -errno;
					
					stbuf->st_size = dds_size(width, height, alpha);
					sizecache_set(rwpath, stbuf->st_size);
				}
				return 0;
			}
			
			return -errno;
		} else return -errno;
	}

	return 0;
}

static int ddsfs_access(const char *path, int mask)
{
	char rwpath[strlen(config.basepath)+strlen(path)+1];
	int res;

	sprintf(rwpath, "%s%s", config.basepath, path);
	res = access(rwpath, mask);
	if (res == -1)
		return -errno;

	return 0;
}

static int ddsfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
	DIR *dp;
	struct dirent *de;
	char rwpath[strlen(config.basepath)+strlen(path)+1];
	char* rwname = (char*)malloc(1024);
	unsigned int rwnamelen = 1024;
	char* testpath = (char*)malloc(1024);
	unsigned int testpathlen = 1024;
	char* ext;

	(void) fi;
	
	sprintf(rwpath, "%s%s", config.basepath, path);
	
	if (DEBUG) printf("readdir: path=%s offset=%ld\n", rwpath, offset);
	dp = opendir(rwpath);
	if (dp == NULL) return -errno;

	struct stat st;
	while ((de = readdir(dp))) {
		memset(&st, 0, sizeof(st));
		if (DEBUG >= 2) printf("\t%s\n", de->d_name);
		
		st.st_ino = de->d_ino;
		st.st_mode = de->d_type << 12;
		if (filler(buf, de->d_name, &st, 0))
			break;
		
		if (strlen(de->d_name) >= rwnamelen) {
			rwnamelen = strlen(de->d_name)+1;
			rwname = (char*)realloc(rwname, rwnamelen);
		}
		strcpy(rwname, de->d_name);
		ext = strrchr(rwname, '.');
		if (DEBUG >= 3) printf("\t\tpath='%s' ext='%s'\n", rwname, ext);
		if (!ext) {
			// Meh.
		} else if (!strcasecmp(ext, ".jpg")) {
			if (strlen(rwpath)+strlen(rwname)+1 >= testpathlen) {
				testpathlen = strlen(rwpath)+strlen(rwname)+2;
				testpath = (char*)realloc(testpath, testpathlen);
			}
			if (DEBUG >= 3) printf("\tFound .jpg file.\n");

			strcpy(ext, ".dds");
			sprintf(testpath, "%s/%s", rwpath, rwname);
			if (stat(testpath, &st) == -1) {
				int size = sizecache_get(testpath);
				if (size != -1) st.st_size = size;
				filler(buf, rwname, &st, 0);
				if (DEBUG >= 3) printf("\t\tAdded .dds\n");
			} else {
				if (DEBUG >= 3) printf("\t\tSkipped .dds\n");
			}
		} else if (!strcasecmp(ext, ".webp")) {
			if (strlen(rwpath)+strlen(rwname)+1 >= testpathlen) {
				testpathlen = strlen(rwpath)+strlen(rwname)+2;
				testpath = (char*)realloc(testpath, testpathlen);
			}
			if (DEBUG >= 3) printf("\tFound .webp file.\n");

			strcpy(ext, ".dds");
			sprintf(testpath, "%s/%s", rwpath, rwname);
			if (stat(testpath, &st) == -1) {
				int size = sizecache_get(testpath);
				if (size != -1) st.st_size = size;
				filler(buf, rwname, &st, 0);
				if (DEBUG >= 3) printf("\t\tAdded .dds\n");
			} else {
				if (DEBUG >= 3) printf("\t\tSkipped .dds\n");
			}
		}
	}

	closedir(dp);
	free(rwname);
	free(testpath);
	if (DEBUG >= 2) printf("readdir: Done.\n");
	return 0;
}

static void tidycache()
{
	list<string>::iterator i;
	CacheEntry* j;
	
	while (memlru.size() > config.cache) {
		i = memlru.begin();
		do {
			if (i == memlru.end()) return;
			j = memindex[*i];
			if (j == NULL) {
				fprintf(stderr, "Error: Tidy found LRU list item '%s' which isn't in index.\n", i->c_str());
				break;
			}
			if (j->refs == 0) break;
			i++;
		} while (1);

		if (j) {
			printf("memcache: Removed '%s' from cache.\n", j->name.c_str());
			memindex.erase(j->name);
			delete j;
		}
		memlru.erase(i);
	}
}
static void refresh(const string& name) {
	for (auto i = memlru.begin(); i != memlru.end(); i++) {
		if (*i == name) {
			memlru.erase(i);
			break;
		}
	}
	memlru.push_back(name);	
}

static int ddsfs_open(const char *path, struct fuse_file_info *fi)
{
	int res;
	char rwpath[strlen(config.basepath)+strlen(path)+1];
	char* ext;
	struct stat st;
	
	sprintf(rwpath, "%s%s", config.basepath, path);
	
	if (DEBUG) printf("open: %s\n", rwpath);
	res = open(rwpath, fi->flags);
	if (res == -1) {
		ext = strrchr(rwpath, '.');
		if (!ext) return -errno;
		if (!strcasecmp(ext, ".dds")) {
			if (DEBUG) printf("\tOpening .dds which does not exist.\n");
			char srcpath[strlen(rwpath)+2];
			unsigned char* dds = NULL;
			int len = 0;
			
			if (config.cache != CACHE_DISK) {
				pthread_rwlock_wrlock(&cachelock);
				auto i = memindex.find(rwpath);
				if (i != memindex.end()) {
					int fd;
					do {
						fd = nextfd++;
						if (nextfd > 1000000) nextfd = 100;
					} while (memcache.find(fd) != memcache.end());
					if (DEBUG) printf("memcache: Using FD %d for existing reference.\n", fd);
					memcache[fd] = i->second;
					i->second->refs++;
					refresh(rwpath);
					pthread_rwlock_unlock(&cachelock);
					fi->fh = fd;
					return 0;
				}
				pthread_rwlock_unlock(&cachelock);
			}
			
			// Try to decode a .jpg alternate
			strcpy(srcpath, rwpath);
			ext = strrchr(srcpath, '.');
			strcpy(ext, ".jpg");
			res = stat(srcpath, &st);
			if (res == 0) {
				if (config.compress) len = ddsfs_jpg_dxt1(srcpath, &dds);
				else len = ddsfs_jpg_rgb(srcpath, &dds);
				if (len == -1) return -errno;
			}
			
			if (!dds) {
				// Try to decode a .webp alternate.
				strcpy(srcpath, rwpath);
				ext = strrchr(srcpath, '.');
				strcpy(ext, ".webp");
				res = stat(srcpath, &st);
				if (res == 0) {
					if (config.compress) len = ddsfs_webp_dxt1(srcpath, &dds);
					else len = ddsfs_webp_rgb(srcpath, &dds);
					if (len == -1) return -errno;
				}
			}
			
			// Failed to decode another file, bail out.
			if (!dds) return -ENOENT;
						
			if (config.cache == CACHE_DISK) {
				if (DEBUG >= 2) printf("cache: Writing %d bytes to '%s'\n", len, rwpath);
				int fd = open(rwpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
				if (fd == -1) {
					fprintf(stderr, "cache: Could not open '%s' for write.\n", rwpath);
					free(dds);
					return -errno;
				}
				len = write(fd, dds, len);
				close(fd);
				free(dds);
				if (DEBUG >= 2) printf("cache: Wrote %d bytes.\n", len);
				
				res = open(rwpath, fi->flags);
				if (DEBUG >= 2) printf("cache: Reopen returned %d.\n", res);
				if (res == -1) return -errno;
				fi->fh = res;
				return 0;
			} else {
				pthread_rwlock_wrlock(&cachelock);
				int fd;
				do {
					fd = nextfd++;
					if (nextfd > 1000000) nextfd = 100;
				} while (memcache.find(fd) != memcache.end());
				fi->fh = fd;

				auto i = memindex.find(rwpath);
				if (i != memindex.end()) {
					if (DEBUG) printf("memcache: Recheck found FD %d for existing reference.\n", fd);
					memcache[fd] = i->second;
					i->second->refs++;
					free(dds);
					refresh(rwpath);
				} else {
					if (DEBUG) printf("memcache: Using FD %d for %d bytes: '%s'\n", fd, len, rwpath);
					CacheEntry* ce = new CacheEntry(rwpath, dds, len);
					memcache[fd] = ce;
					memindex[rwpath] = ce;
					memlru.push_back(rwpath);
					if (config.cache >= CACHE_MEM) tidycache();
					sizecache_set(rwpath, len);
				}
				pthread_rwlock_unlock(&cachelock);
				return 0;
			}
		} else {
			return -errno;
		}
	}

	fi->fh = res;
	return 0;
}

static int ddsfs_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{
	int fd;
	int res;

	if (fi == NULL) {
		char rwpath[strlen(config.basepath)+strlen(path)+1];
		sprintf(rwpath, "%s%s", config.basepath, path);
		fd = open(path, O_RDONLY);
		if (DEBUG) printf("read: Called with no info for file '%s'\n", path);
	} else fd = fi->fh;
	if (fd == -1) return -errno;
	
	if (config.cache != CACHE_DISK) {
		pthread_rwlock_rdlock(&cachelock);
		if (DEBUG >= 2) printf("read: Checking memory cache for FD %d.\n", fd);
		auto i = memcache.find(fd);
		if (i != memcache.end()) {
			if (DEBUG >= 2) printf("read: %lu bytes at %ld from memory for FD %d.\n", size, offset, i->first);
			if (size+offset > i->second->len) {
				size = (i->second->len)-offset;
				if (DEBUG) printf("read: Read would have exceeded length, reducing to %lu.\n", size);
			}
			memcpy(buf, (i->second->data)+offset, size);
			pthread_rwlock_unlock(&cachelock);
			return size;
		}
		pthread_rwlock_unlock(&cachelock);
	}

	res = pread(fd, buf, size, offset);
	if (res == -1) res = -errno;

	if (fi == NULL) close(fd);
	
	return res;
}

static int ddsfs_release(const char *path, struct fuse_file_info *fi)
{
	if (DEBUG >= 2) printf("release: %s\n", path);
	if (fi == NULL || fi->fh == 0) return 0;
	if (config.cache != CACHE_DISK) {
		pthread_rwlock_wrlock(&cachelock);
		auto i = memcache.find(fi->fh);
		if (i != memcache.end()) {
			if (config.cache == CACHE_NONE && i->second->refs <= 1) {
				if (DEBUG) printf("release: Freeing %d bytes of memory for FD %d.\n", i->second->len, i->first);
				memindex.erase(i->second->name);
				delete i->second;
			} else {
				i->second->refs--;
				if (DEBUG) printf("release: FD %d now has %d ref%s.\n", i->first, i->second->refs, i->second->refs==1?"":"s");
			}
			memcache.erase(i);
			pthread_rwlock_unlock(&cachelock);
			return 0;
		}
		pthread_rwlock_unlock(&cachelock);
	}
	return close(fi->fh);
}

int main(int argc, char *argv[])
{
	umask(0);
	config.cache = 1;
	config.compress = 1;
	config.size = 1;
	
	pthread_rwlock_init(&cachelock, NULL);

	memset(&oper, 0, sizeof(oper));
	oper.getattr = ddsfs_getattr;
	oper.access = ddsfs_access;
	oper.readdir = ddsfs_readdir;
	oper.open = ddsfs_open;
	oper.read = ddsfs_read;
	oper.release = ddsfs_release;

	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	fuse_opt_parse(&args, &config, ddsfs_opts, ddsfs_opt_proc);
	
	if (config.basepath == NULL) {
		fprintf(stderr, "Usage: %s <scenery> <mount>\n", args.argv[0]);
		return 1;
	}
	
	printf("Starting: basepath=%s cache=%d format=%s\n", config.basepath, config.cache, config.compress?"DXT":"RGB");
	int ret = fuse_main(args.argc, args.argv, &oper, NULL);
	printf("Exiting.\n");
	
	return ret;
}
