# DDSFS
Transparent DDS-Converting FUSE Filesystem

Originally designed for supporting JPEG X-Plane photoscenery, DDSFS combines the transparency of FUSE with the 
fast decoding of libjpeg-turbo and encoding of FastDXT. It is able to convert textures stored as
small-sized .jpg files into game-friendly .dds files transparently during load.

#### Usage
*ddsfs [options] &lt;source path&gt; &lt;mount path&gt;*  

In addition to the standard FUSE options, DDSFS recognizes:  

| Option             | Description |
| ------------------ | ----------- |
| -o cache (default) | Write encoded DDS files to the source path so they can be retrieved instantly later.
| -o nocache         | DDS files are only stored in memory.
| -o dxt1 (default)  | Produce DDS files as DXT1/DXT5.
| -o rgb             | Produce DDS files as RGB/RGBA.
| -o debug[=#]       | Writes status/debugging information. Values for # range from 1 to 3.

Windows/Cygwin Example: ./ddsfs.exe . X:  
Linux Example: ./ddsfs . /mnt/ddsfs  

#### Formats
Currently supported formats:  
.jpg to DXT1 or RGB  
.webp to DXT1/DXT5 or RGBA

#### Performance
On my system, it will convert a 4096x4096 .jpg texture to .dds encoded with DXT1 in around 100 ms. 
In X-Plane 11, this gave me around a 3 minute initial load time in a photoscenery-covered area, and no noticible pauses
when flying.
Using .webp is significantly slower, it takes around 300 ms to convert a 4096x4096 file on my system.
