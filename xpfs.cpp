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
#include <sys/timeb.h> 
#include <dirent.h>
#include <errno.h>
#include <fuse.h>
#include <turbojpeg.h>
#include <libdxt.h>

#ifndef DEBUG
#define DEBUG 0
#endif

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
struct ddsheader {
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
void halveimage(const unsigned char* src, int width, int height, unsigned char* dst);

static char* basepath;

static int xpfs_opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs) {
	if (key == FUSE_OPT_KEY_NONOPT && basepath == NULL) {
		basepath = strdup(arg);
		return 0;
	}
	return 1;
}

static void xpfs_dds(char* src, char* dst) {
	struct timeb start, mid, end;
	
	if (DEBUG) {
		printf("turbojpeg: Doing intenal conversion with turbojpeg.\n");
		ftime(&start);
	}
	
	int fd = open(src, O_RDONLY);
	if (fd == -1) {
		fprintf(stderr, "turbojpeg: Could not open '%s' for read.\n", src);
		return;
	}
	
	size_t size = lseek(fd, 0, SEEK_END);
	unsigned char* jpeg = (unsigned char*)memalign(16, size);
	lseek(fd, 0, SEEK_SET);
	size = read(fd, jpeg, size);
	close(fd);

	tjhandle tj = tjInitDecompress();
	
	int width, height, subsamp, colourspace;
	if (tjDecompressHeader3(tj, jpeg, size, &width, &height, &subsamp, &colourspace) == -1) {
		fprintf(stderr, "turbojpeg: Could not decode header for '%s': %s\n", src, tjGetErrorStr());
		free(jpeg);
		return;
	}
	
	unsigned char* rgba = (unsigned char*)memalign(16, width * height * 4);
	if (tjDecompress2(tj, jpeg, size, rgba, 0, 0, 0, TJPF_RGBA, TJFLAG_FASTDCT|TJFLAG_FASTUPSAMPLE) == -1) {
		fprintf(stderr, "turbojpeg: Could not decode image for '%s': %s\n", src, tjGetErrorStr());
		free(jpeg);
		free(rgba);
		return;
	}
	free(jpeg);
	
	tjDestroy(tj);
	
	if (DEBUG) {
		ftime(&mid);
		int diff = (1000.0 * (mid.time - start.time) + (mid.millitm - start.millitm));
		printf("turbojpeg: JPEG decode done in %d ms.\n", diff);
	}

	unsigned char* dds = (unsigned char*)memalign(16, width * height * 4);
	ddsheader header;
	memset(&header, 0, sizeof(header));
	header.dwMagic = 0x20534444;
	header.dwSize = 124;
	header.dwFlags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000;
	header.dwHeight = height;
	header.dwWidth = width;
	
	int mips = 0;
	while ((height >> mips) > 8 && (width >> mips) > 8) mips++;
	if (DEBUG >= 2) printf("turbojpeg: Writing header for %d mips.\n", mips+1);
	header.dwMipMapCount = mips+1;
	
	DDS_PIXELFORMAT ddspix;
	memset(&ddspix, 0, sizeof(ddspix));
	ddspix.dwSize = 32;
	ddspix.dwFlags = 0x4;
	ddspix.dwFourCC = 'D' | 'X'<<8 | 'T'<<16 | '1'<<24;
	header.ddspf = ddspix;
	
	header.dwCaps = 0x1000 | 0x8 | 0x400000;
	
	fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd == -1) {
		fprintf(stderr, "turbojpeg: Could not open '%s' for write.\n", dst);
		free(dds);
		return;
	}
	write(fd, &header, sizeof(header));

	int bytes;
	CompressImageDXT1(rgba, dds, width, height, bytes);
	if (bytes != width * height / 2) printf("turbojpeg: DXT data size %d, expected %d.\n", bytes, width * height / 2);

	write(fd, dds, bytes);

	int curmip = 0;
	while (width > 8 && height > 8) {
		if (DEBUG >= 2) printf("turbojpeg: Resample mip %d (%d x %d)\n", ++curmip, width, height);
		unsigned char* nextmip = (unsigned char*)memalign(16, width * height * 4);
		halveimage(rgba, width, height, nextmip);
		width >>= 1;
		height >>= 1;
		free(rgba);
		rgba = nextmip;
		
		if (DEBUG >= 2) printf("turbojpeg: Compress mip %d (%d x %d)\n", curmip, width, height);
		CompressImageDXT1(rgba, dds, width, height, bytes);
		if (bytes != width * height / 2) printf("turbojpeg: DXT data size %d, expected %d.\n", bytes, width * height / 2);
		
		write(fd, dds, bytes);
		if (DEBUG >= 2) printf("turbojpeg: Done mip %d.\n", curmip);
	}
	close(fd);
	free(rgba);
	free(dds);
	
	if (DEBUG) {
		ftime(&end);
		int diff = (1000.0 * (end.time - mid.time) + (end.millitm - mid.millitm));
		printf("turbojpeg: DXT1 encode done in %d ms.\n", diff);
		diff = (1000.0 * (end.time - start.time) + (end.millitm - start.millitm));
		printf("turbojpeg: Total time %d ms.\n", diff);
	}
}

static int xpfs_getattr(const char *path, struct stat *stbuf)
{
	int res;
	char rwpath[strlen(basepath)+strlen(path)+2];
	char* ext;

	sprintf(rwpath, "%s/%s", basepath, path);
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
	char rwpath[strlen(basepath)+strlen(path)+2];
	char* ext;

	(void) offset;
	(void) fi;
	
	sprintf(rwpath, "%s/%s", basepath, path);
	
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
	char rwpath[strlen(basepath)+strlen(path)+2];
	char* ext;
	struct stat st;
	
	sprintf(rwpath, "%s/%s", basepath, path);
	
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
			
			xpfs_dds(srcpath, rwpath);	
			
			res = open(rwpath, fi->flags);
			if (res == -1) return -errno;
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
		char rwpath[strlen(basepath)+strlen(path)+2];
		sprintf(rwpath, "%s/%s", basepath, path);
		fd = open(path, O_RDONLY);
	} else fd = fi->fh;
	
	if (fd == -1) return -errno;

	res = pread(fd, buf, size, offset);
	if (res == -1) res = -errno;

	if (fi == NULL) close(fd);
	
	return res;
}

static int xpfs_release(const char *path, struct fuse_file_info *fi)
{
	if (DEBUG >= 2) printf("release: %s\n", path);
	if (fi != NULL) return close(fi->fh);
	return 0;
}

int main(int argc, char *argv[])
{
	struct fuse_operations oper;
	memset(&oper, 0, sizeof(oper));
	oper.getattr = xpfs_getattr;
	oper.readdir = xpfs_readdir;
	oper.open = xpfs_open;
	oper.read = xpfs_read;
	oper.release = xpfs_release;
	
	umask(0);
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	fuse_opt_parse(&args, NULL, NULL, xpfs_opt_proc);
	
	if (basepath == NULL) {
		fprintf(stderr, "Usage: %s <scenery> <mount>\n", args.argv[0]);
		return 1;
	}
	
	printf("Starting: basepath=%s\n", basepath);
	int ret = fuse_main(args.argc, args.argv, &oper, NULL);
	printf("Exiting.\n");
	
	return ret;
}
