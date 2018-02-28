CXXFLAGS?=-O3 -march=haswell -mtune=intel -msse -msse2 -flto -fuse-linker-plugin

ddsfs: ddsfs.cpp halveimage.cpp jpg.cpp webp.cpp
	$(CXX) $(CXXFLAGS) -Wall -Wno-unused-variable -o ddsfs$(EXE) ddsfs.cpp halveimage.cpp jpg.cpp webp.cpp \
		-IFastDXT FastDXT/util.cpp FastDXT/dxt.cpp FastDXT/intrinsic.cpp \
		-D_FILE_OFFSET_BITS=64 `pkg-config fuse libturbojpeg libwebpdecoder --cflags --libs` -lpthread

clean:
	rm -f ddsfs ddsfs.exe