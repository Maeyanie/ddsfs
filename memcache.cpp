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
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <list>
#include "ddsfs.h"
using namespace std;

class CacheEntry {
public:
	std::string name;
	unsigned char* data;
	unsigned int len;
	int refs;
	
	CacheEntry(const char* n, unsigned char* d, unsigned int l) {
		name = n;
		data = d;
		len = l;
		refs = 1;
	}
	CacheEntry(const string& n, unsigned char* d, unsigned int l) {
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

static int nextfd = 100;

static unordered_map<int,CacheEntry*>* memcache = new unordered_map<int,CacheEntry*>();
static unordered_map<string,CacheEntry*>* memindex = new unordered_map<string,CacheEntry*>();
static pthread_rwlock_t cachelock;

static list<string>* memlru = new list<string>();
static pthread_mutex_t lrulock = PTHREAD_MUTEX_INITIALIZER;

static void lru_tidy() {
	list<string>::iterator i;
	CacheEntry* j;

	while (memlru->size() > config.cache) {
		i = memlru->begin();
		do {
			if (i == memlru->end()) return;

			j = memindex->at(*i);
			if (j == NULL) {
				fprintf(stderr, "Error: Tidy found LRU list item '%s' which isn't in index.\n", i->c_str());
				break;
			}
			if (j->refs == 0) break;

			i++;
		} while (1);

		if (j) {
			printf("memcache: Removed '%s' from cache.\n", j->name.c_str());
			memindex->erase(j->name);
			delete j;
		}
		memlru->erase(i);
	}
}
static void lru_hit(const string& name) {
	pthread_mutex_lock(&lrulock);

	for (auto i = memlru->begin(); i != memlru->end(); i++) {
		if (*i == name) {
			memlru->erase(i);
			break;
		}
	}
	memlru->push_back(name);
	
	lru_tidy();

	pthread_mutex_unlock(&lrulock);
}

void memcache_init() {
	pthread_rwlock_init(&cachelock, NULL);
}

static int memcache_nextfd() {
	int fd;
	do {
		fd = nextfd++;
		if (nextfd > 1000000) nextfd = 100;
	} while (memcache->find(fd) != memcache->end());
	return fd;
}

int memcache_getfd(const string& name) {
	pthread_rwlock_wrlock(&cachelock);
	int fd;
	
	auto i = memindex->find(name);
	if (i != memindex->end()) {
		fd = memcache_nextfd();
		memcache->emplace(fd, i->second);
		i->second->refs++;
		lru_hit(name);
	} else {
		fd = 0;
	}
	
	pthread_rwlock_unlock(&cachelock);
	return fd;
}

int memcache_store(const string& name, unsigned char* dds, unsigned int len) {
	pthread_rwlock_wrlock(&cachelock);

	CacheEntry* ce = new CacheEntry(name, dds, len);
	int fd = memcache_nextfd();
	
	memcache->emplace(fd, ce);
	memindex->emplace(name, ce);
	lru_hit(name);
	
	pthread_rwlock_unlock(&cachelock);
	return fd;
}

int memcache_read(int fd, char* buf, size_t size, off_t offset) {
	pthread_rwlock_rdlock(&cachelock);
	
	auto i = memcache->find(fd);
	if (i != memcache->end()) {
		if (size+offset > i->second->len) {
			size = (i->second->len)-offset;
			if (DEBUG) printf("read: Read would have exceeded length, reducing to %lu.\n", size);
		}
		memcpy(buf, (i->second->data)+offset, size);
	} else {
		size = -1;
	}
	
	pthread_rwlock_unlock(&cachelock);
	return size;
}

int memcache_release(int fd) {
	int ret = 0;
	pthread_rwlock_wrlock(&cachelock);
	
	auto i = memcache->find(fd);
	if (i != memcache->end()) {
		if (config.cache == CACHE_NONE && i->second->refs <= 1) {
			if (DEBUG) printf("release: Freeing %d bytes of memory for FD %d.\n", i->second->len, i->first);
			memindex->erase(i->second->name);
			delete i->second;
		} else {
			i->second->refs--;
			if (DEBUG) printf("release: FD %d now has %d ref%s.\n", i->first, i->second->refs, i->second->refs==1?"":"s");
		}
		memcache->erase(i);
		ret = 1;
	}
	
	pthread_rwlock_unlock(&cachelock);
	return ret;
}


