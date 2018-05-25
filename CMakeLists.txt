cmake_minimum_required(VERSION 2.6)
set(CMAKE_LEGACY_CYGWIN_WIN32 0)
project(ddsfs)

set(WANT_JPG ON CACHE BOOL "Support .jpg->.dds conversion")
set(WANT_WEBP ON CACHE BOOL "Support .webp->.dds conversion")
set(WANT_GZIP ON CACHE BOOL "Support .ext.gz->.ext decompression")
set(WANT_XZ ON CACHE BOOL "Support .ext.xz->.ext decompression")

find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE REQUIRED fuse)
pkg_check_modules(JPEG libturbojpeg)
pkg_check_modules(WEBP libwebpdecoder)
pkg_check_modules(GZIP zlib)
pkg_check_modules(XZ liblzma)

set(SOURCES ddsfs.cpp halveimage.cpp sizecache.cpp)
set(INCLUDEDIRS ${FUSE_INCLUDE_DIRS})
set(COMPILEOPTS ${FUSE_CFLAGS_OTHER})
set(LIBRARIES ${FUSE_LDFLAGS})

if(JPEG_FOUND AND WANT_JPG)
	set(USE_JPG 1)
	set(SOURCES ${SOURCES} jpg.cpp)
	set(INCLUDEDIRS ${INCLUDEDIRS} ${JPEG_INCLUDE_DIRS})
	set(COMPILEOPTS ${COMPILEOPTS} ${JPEG_CFLAGS_OTHER})
	set(LIBRARIES ${LIBRARIES} ${JPEG_LDFLAGS})
endif(JPEG_FOUND AND WANT_JPG)

if(WEBP_FOUND AND WANT_WEBP)
	set(USE_WEBP 1)
	set(SOURCES ${SOURCES} webp.cpp)
	set(INCLUDEDIRS ${INCLUDEDIRS} ${WEBP_INCLUDE_DIRS})
	set(COMPILEOPTS ${COMPILEOPTS} ${WEBP_CFLAGS_OTHER})
	set(LIBRARIES ${LIBRARIES} ${WEBP_LDFLAGS})
endif(WEBP_FOUND AND WANT_WEBP)

if(GZIP_FOUND AND WANT_GZIP)
	set(USE_GZIP 1)
	set(SOURCES ${SOURCES} gzip.cpp)
	set(INCLUDEDIRS ${INCLUDEDIRS} ${GZIP_INCLUDE_DIRS})
	set(COMPILEOPTS ${COMPILEOPTS} ${GZIP_CFLAGS_OTHER})
	set(LIBRARIES ${LIBRARIES} ${GZIP_LDFLAGS})
endif(GZIP_FOUND AND WANT_GZIP)

if(XZ_FOUND AND WANT_XZ)
	set(USE_XZ 1)
	set(SOURCES ${SOURCES} xz.cpp)
	set(INCLUDEDIRS ${INCLUDEDIRS} ${XZ_INCLUDE_DIRS})
	set(COMPILEOPTS ${COMPILEOPTS} ${XZ_CFLAGS_OTHER})
	set(LIBRARIES ${LIBRARIES} ${XZ_LDFLAGS})
endif(XZ_FOUND AND WANT_XZ)

configure_file (
	"${PROJECT_SOURCE_DIR}/config.h.in"
	"${PROJECT_BINARY_DIR}/config.h"
)
include_directories("${PROJECT_BINARY_DIR}")

include_directories(FastDXT)
set(FASTDXT FastDXT/util.cpp FastDXT/dxt.cpp FastDXT/intrinsic.cpp)

add_executable(ddsfs ${SOURCES} ${FASTDXT})
target_include_directories(ddsfs PUBLIC ${INCLUDEDIRS})
target_compile_options(ddsfs PUBLIC ${COMPILEOPTS})
target_link_libraries(ddsfs PUBLIC ${LIBRARIES})
