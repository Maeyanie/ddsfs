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
#include <unordered_map>
#include "xpfs.h"
using namespace std;

struct CacheEntry {
	unsigned char* data;
	unsigned int len;
};

struct Config config;
#define XPFS_OPT(t, p, v) { t, offsetof(struct Config, p), v }
static struct fuse_opt xpfs_opts[] = {
	XPFS_OPT("dxt1",			compress, 1),
	XPFS_OPT("rgb",				compress, 0),
	XPFS_OPT("cache",			cache, 1),
	XPFS_OPT("nocache",			cache, 0),
	XPFS_OPT("debug",			debug, 1),
	XPFS_OPT("debug=%i",		debug, 0),
	XPFS_OPT("--debug",			debug, 1),
	XPFS_OPT("--debug=%i",		debug, 0),
	FUSE_OPT_END
};

static unordered_map<int,CacheEntry> memcache;
static int nextfd = 100;
static pthread_mutex_t cachelock = PTHREAD_MUTEX_INITIALIZER;

static int xpfs_opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs) {
	if (key == FUSE_OPT_KEY_NONOPT && config.basepath == NULL) {
		config.basepath = strdup(arg);
		return 0;
	}
	return 1;
}

static int xpfs_getattr(const char *path, struct stat *stbuf)
{
	int res;
	char rwpath[strlen(config.basepath)+strlen(path)+2];
	char* ext;

	sprintf(rwpath, "%s/%s", config.basepath, path);
	if (DEBUG >= 2) printf("getattr: %s\n", rwpath);

	res = lstat(rwpath, stbuf);
	if (res == -1) {
		ext = strrchr(rwpath, '.');
		if (!ext) return -errno;
		if (!strcasecmp(ext, ".dds") || !strcasecmp(ext, ".png")) {
			strcpy(ext, ".jpg");
			res = lstat(rwpath, stbuf);
			if (res == -1) return -errno;
		} else return -errno;
	}

	return 0;
}

static int xpfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
	DIR *dp;
	struct dirent *de;
	char rwpath[strlen(config.basepath)+strlen(path)+2];
	char* ext;

	(void) offset;
	(void) fi;
	
	sprintf(rwpath, "%s/%s", config.basepath, path);
	
	if (DEBUG) printf("readdir: %s\n", rwpath);
	dp = opendir(rwpath);
	if (dp == NULL) return -errno;

	while ((de = readdir(dp))) {
		struct stat st;
		memset(&st, 0, sizeof(st));
		if (DEBUG >= 2) printf("\t%s\n", de->d_name);
		
		st.st_ino = de->d_ino;
		st.st_mode = de->d_type << 12;
		if (filler(buf, de->d_name, &st, 0))
			break;
		
		char* rwname = strdupa(de->d_name);
		ext = strrchr(rwname, '.');
		if (DEBUG >= 3) printf("\t\tpath='%s' ext='%s'\n", rwname, ext);
		if (ext && !strcasecmp(ext, ".jpg")) {
			char testpath[strlen(rwpath)+strlen(rwname)+2];
			if (DEBUG >= 3) printf("\tFound .jpg file.\n");

			strcpy(ext, ".dds");
			sprintf(testpath, "%s/%s", rwpath, rwname);
			if (stat(testpath, &st) == -1) {
				filler(buf, rwname, &st, 0);
				if (DEBUG >= 3) printf("\t\tAdded .dds\n");
			} else {
				if (DEBUG >= 3) printf("\t\tSkipped .dds\n");
			}
		}
	}

	closedir(dp);
	if (DEBUG) printf("readdir: Done.\n");
	return 0;
}

static int xpfs_open(const char *path, struct fuse_file_info *fi)
{
	int res;
	char rwpath[strlen(config.basepath)+strlen(path)+2];
	char* ext;
	struct stat st;
	
	sprintf(rwpath, "%s/%s", config.basepath, path);
	
	if (DEBUG) printf("open: %s\n", rwpath);
	res = open(rwpath, fi->flags);
	if (res == -1) {
		ext = strrchr(rwpath, '.');
		if (!ext) return -errno;
		if (!strcasecmp(ext, ".dds")) {
			if (DEBUG) printf("\tOpening .dds which does not exist.\n");
			char* srcpath = strdupa(rwpath);
			ext = strrchr(srcpath, '.');
			strcpy(ext, ".jpg");
			
			
			res = stat(srcpath, &st);
			if (res) return -errno;
			
			unsigned char* dds;
			int len;
			if (config.compress) len = xpfs_dds_dxt1(srcpath, &dds);
			else len = xpfs_dds_rgb(srcpath, &dds);
			if (len == -1) return -errno;
			
			if (config.cache) {
				if (DEBUG >= 2) printf("cache: Writing %d bytes to '%s'\n", len, rwpath);
				int fd = open(rwpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
				if (fd == -1) {
					fprintf(stderr, "cache: Could not open '%s' for write.\n", rwpath);
					free(dds);
					return -errno;
				}
				len = write(fd, dds, len);
				close(fd);
				if (DEBUG >= 2) printf("cache: Wrote %d bytes.\n", len);
				
				res = open(rwpath, fi->flags);
				if (DEBUG >= 2) printf("cache: Reopen returned %d.\n", res);
				if (res == -1) return -errno;
				fi->fh = res;
				return res;
			} else {
				pthread_mutex_lock(&cachelock);
				int fd;
				do {
					fd = nextfd++;
					if (nextfd > 1000000) nextfd = 100;
				} while (memcache.find(fd) != memcache.end());
				if (DEBUG >= 2) printf("nocache: Using FD %d for %d bytes.\n", fd, len);
				memcache[fd] = {dds, (unsigned int)len};
				pthread_mutex_unlock(&cachelock);
				fi->fh = fd;
				return fd;
			}
		} else {
			return -errno;
		}
	}

	fi->fh = res;
	return 0;
}

static int xpfs_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{
	int fd;
	int res;

	if (fi == NULL) {
		char rwpath[strlen(config.basepath)+strlen(path)+2];
		sprintf(rwpath, "%s/%s", config.basepath, path);
		fd = open(path, O_RDONLY);
	} else fd = fi->fh;
	if (fd == -1) return -errno;
	
	if (!config.cache) {
		pthread_mutex_lock(&cachelock);
		if (DEBUG >= 2) printf("read: Uncached mode, looking for FD %d.\n", fd);
		auto i = memcache.find(fd);
		if (i != memcache.end()) {
			if (DEBUG >= 2) printf("read: %lu bytes at %ld from memory for FD %d.\n", size, offset, i->first);
			if (size+offset > i->second.len) {
				size = (i->second.len)-offset;
				if (DEBUG) printf("read: Read would have exceeded length, reducing to %lu.\n", size);
			}
			memcpy(buf, (i->second.data)+offset, size);
			pthread_mutex_unlock(&cachelock);
			return size;
		}
		pthread_mutex_unlock(&cachelock);
	}

	res = pread(fd, buf, size, offset);
	if (res == -1) res = -errno;

	if (fi == NULL) close(fd);
	
	return res;
}

static int xpfs_release(const char *path, struct fuse_file_info *fi)
{
	if (DEBUG >= 2) printf("release: %s\n", path);
	if (fi == NULL || fi->fh == 0) return 0;
	if (!config.cache) {
		pthread_mutex_lock(&cachelock);
		auto i = memcache.find(fi->fh);
		if (i != memcache.end()) {
			if (DEBUG >= 2) printf("release: Freeing %d bytes of memory for FD %d.\n", i->second.len, i->first);
			free(i->second.data);
			memcache.erase(i);
			pthread_mutex_unlock(&cachelock);
			return 0;
		}
		pthread_mutex_unlock(&cachelock);
	}
	return close(fi->fh);
}

int main(int argc, char *argv[])
{
	umask(0);
	config.cache = 1;
	config.compress = 1;

	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	fuse_opt_parse(&args, &config, xpfs_opts, xpfs_opt_proc);

	struct fuse_operations oper;
	memset(&oper, 0, sizeof(oper));
	oper.getattr = xpfs_getattr;
	oper.readdir = xpfs_readdir;
	oper.open = xpfs_open;
	oper.read = xpfs_read;
	oper.release = xpfs_release;
	
	if (config.basepath == NULL) {
		fprintf(stderr, "Usage: %s <scenery> <mount>\n", args.argv[0]);
		return 1;
	}
	
	printf("Starting: basepath=%s cache=%d format=%s\n", config.basepath, config.cache, config.compress?"DXT1":"RGB");
	int ret = fuse_main(args.argc, args.argv, &oper, NULL);
	printf("Exiting.\n");
	
	return ret;
}
