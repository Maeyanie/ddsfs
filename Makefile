xpfs: xpfs.cpp
	$(CXX) -Wall -Wno-unused-variable -O3 -msse -msse2 -flto -o xpfs xpfs.cpp halveimage.cpp \
		-IFastDXT FastDXT/util.cpp FastDXT/dxt.cpp FastDXT/intrinsic.cpp \
		-D_FILE_OFFSET_BITS=64 `pkg-config fuse libturbojpeg --cflags --libs`
