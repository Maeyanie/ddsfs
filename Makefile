ddsfs: Makefile ddsfs.cpp halveimage.cpp sizecache.cpp jpg.cpp webp.cpp
	$(CXX) $(CXXFLAGS) -Wall -Wno-unused-variable -o ddsfs ddsfs.cpp \
		halveimage.cpp sizecache.cpp jpg.cpp webp.cpp gzip.cpp xz.cpp \
		-IFastDXT FastDXT/util.cpp FastDXT/dxt.cpp FastDXT/intrinsic.cpp \
		-D_FILE_OFFSET_BITS=64 `pkg-config fuse libturbojpeg libwebpdecoder zlib liblzma --cflags --libs`

clean:
	rm -f ddsfs ddsfs.exe
