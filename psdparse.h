/*
    This file is part of "psdparse"
    Copyright (C) 2004-7 Toby Thain, toby@telegraphics.com.au

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifdef powerc // MPW MrC
	#include <MacTypes.h>

	typedef SInt64 int64_t, off_t;
	typedef unsigned short uint16_t;
	#define fputwc fputc // none of that Unicode nonsense for us! (UniChar)
	#define putwchar putchar
	#define vsnprintf(s,n,f,ap) vsprintf(s,f,ap)
#else
	#include <stdint.h>
	#include <sys/types.h>
	#include <unistd.h>
	#include <wchar.h>
#endif

#ifdef HAVE_ICONV_H
	#include <iconv.h>
#endif

#ifdef PSBSUPPORT
	#define _FILE_OFFSET_BITS 64
	// #define _LARGEFILE_SOURCE // defined in Makefile

	typedef int64_t psd_bytes_t;
	#define GETPSDBYTES(f) (h->version==1 ? get4B(f) : get8B(f))

	// macro chooses the '%ll' version of format strings involving psd_bytes_t type
	#define LL_L(llfmt,lfmt) llfmt
#else
	typedef long psd_bytes_t;
	//typedef int psd_pixels_t;
	#define GETPSDBYTES get4B

	// macro chooses the '%l' version of format strings involving psd_bytes_t type
	#define LL_L(llfmt,lfmt) lfmt
#endif

typedef long psd_pixels_t;

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#ifdef PSDPARSE_PLUGIN
	#include "world.h" // for DIRSEP
	#include "file_compat.h"

	typedef FILEREF psd_file_t; // appropriate file handle type for platform

	#define fgetc pl_fgetc
	#define fread pl_fread
	#define fseeko pl_fseeko
	#define ftello pl_ftello
	#undef feof // it's a macro in Metrowerks stdio
	#define feof pl_feof

	int pl_fgetc(psd_file_t f);
	int pl_feof(psd_file_t f);
	size_t pl_fread(void *ptr, size_t s, size_t n, psd_file_t f);
	int pl_fseeko(psd_file_t f, off_t pos, int wh);
	off_t pl_ftello(psd_file_t f);
	void pl_fatal(char *s);

	Boolean warndialog(char *s);
#else
	typedef FILE *psd_file_t;

	#if defined WIN32 || defined powerc
		// MinGW doesn't have fseeko/ftello, nor does MPW
		#define fseeko fseek
		#define ftello ftell
	#endif
#endif

#ifndef PATH_MAX
	#define PATH_MAX FILENAME_MAX
#endif

enum{RAWDATA,RLECOMP};

/* Photoshop's mode constants */
#define SCAVENGE_MODE -1
#define ModeBitmap		 0
#define ModeGrayScale		 1
#define ModeIndexedColor 2
#define ModeRGBColor		 3
#define ModeCMYKColor		 4
#define ModeHSLColor		 5
#define ModeHSBColor		 6
#define ModeMultichannel 7
#define ModeDuotone		 8
#define ModeLabColor		 9
#define ModeGray16		10
#define ModeRGB48			11
#define ModeLab48			12
#define ModeCMYK64		13
#define ModeDeepMultichannel 14
#define ModeDuotone16		15

#define PAD2(x) (((x)+1) & -2) // same or next even
#define PAD4(x) (((x)+3) & -4) // same or next multiple of 4

#define VERBOSE if(verbose) printf
#define UNQUIET if(!quiet) printf

#define FIXEDPT(x) ((x)/65536.)

struct psd_header{
	char sig[4];
	short version;
	char reserved[6];
	short channels;
	long rows;
	long cols;
	short depth;
	short mode;

	// following fields are for our purposes, not actual header fields
	psd_bytes_t colormodepos; // set by dopsd()
	int nlayers, mergedalpha; // set by dopsd()->dolayermaskinfo()
	struct layer_info *linfo;     // layer info array, set by dopsd()->dolayermaskinfo()
	psd_bytes_t lmistart, lmilen; // layer & mask info section, set by dopsd()->dolayermaskinfo()
	psd_bytes_t layerdatapos; // set by dopsd()
};

struct layer_mask_info{
	psd_bytes_t size;
	long top;
	long left;
	long bottom;
	long right;
	char default_colour;
	char flags;
	//char reserved[2]

	// runtime data
	psd_pixels_t rows,cols;
};

struct blend_mode_info{
	char sig[4];
	char key[4];
	unsigned char opacity;
	unsigned char clipping;
	unsigned char flags;
	//unsigned char filler;
};

struct layer_info{
	long top;
	long left;
	long bottom;
	long right;
	short channels;

	// runtime data (not in file)
	psd_bytes_t *chlengths; // array of channel lengths
	int *chid;       // channel ids
	int *chindex;    // lookup channel number by id (inverse of chid[])
	struct blend_mode_info blend;
	struct layer_mask_info mask;
	char *name;
	char *nameno; // "layerNN"
	psd_bytes_t additionalpos;
	psd_bytes_t additionallen;

	psd_bytes_t filepos; // only used in scavenge layers mode
	psd_bytes_t chpos; // only used in scavenge channels mode
};

struct extra_data{
	char sig[4];
	char key[4];
	psd_bytes_t length;
	//char data[];
};

struct dictentry{
	int id;
	char *key, *tag, *desc;
	void (*func)(psd_file_t f, int level, int printxml, struct dictentry *dict);
};

extern char *channelsuffixes[], *mode_names[], dirsep[], *pngdir;
extern int verbose, quiet, rsrc, extra, makedirs, numbered,
		   help, split, nwarns, writepng, writelist, writexml, xmlout;

extern FILE *xml, *listfile;

void fatal(char *s);
void warn(char *fmt,...);
void alwayswarn(char *fmt,...);
void *checkmalloc(long n);
void fputcxml(unsigned c, FILE *f);
void fputsxml(char *str, FILE *f);
char *getpstr(psd_file_t f);
char *getpstr2(psd_file_t f);
char *getkey(psd_file_t f);
double getdoubleB(psd_file_t f);
long get4B(psd_file_t f);
int64_t get8B(psd_file_t f);
int get2B(psd_file_t f);
unsigned get2Bu(psd_file_t f);
const char *tabs(int n);
void openfiles(char *psdpath, struct psd_header *h);

int dopsd(psd_file_t f, char *fname, struct psd_header *h);
void processlayers(psd_file_t f, struct psd_header *h);

void entertag(psd_file_t f, int level, int printxml, struct dictentry *parent, struct dictentry *d, int resetpos);
struct dictentry *findbykey(psd_file_t f, int level, struct dictentry *dict, char *key, int printxml, int resetpos);
void doadditional(psd_file_t f, int level, psd_bytes_t length, int printxml);
void layerblendmode(psd_file_t f, int level, int printxml, struct blend_mode_info *bm);

void descriptor(psd_file_t f, int level, int printxml, struct dictentry *dict);

void ir_raw(psd_file_t f, int level, int len, struct dictentry *parent);

void ir_icc34profile(psd_file_t f, int level, int len, struct dictentry *parent);
double s15fixed16(psd_file_t f);

void skipblock(psd_file_t f,char *desc);
void dumprow(unsigned char *b,long n,int group);
void readunpackrow(psd_file_t psd, int chcomp[], psd_bytes_t **rowpos,
				   int i/*channel*/, psd_pixels_t j/*row*/, psd_bytes_t rb,
				   unsigned char *inrow/*dest buffer*/,
				   unsigned char *rledata/*temp buffer, 2 x rb in size*/);
int dochannel(psd_file_t f,struct layer_info *li,int idx,int channels,
			  psd_pixels_t rows,psd_pixels_t cols,int depth,psd_bytes_t **rowpos,struct psd_header *h);
void doimage(psd_file_t f,struct layer_info *li,char *name,int channels,
			 psd_pixels_t rows,psd_pixels_t cols,struct psd_header *h);
void readlayerinfo(FILE *f, struct psd_header *h, int i);
void dolayermaskinfo(psd_file_t f,struct psd_header *h);
void doimageresources(psd_file_t f);

int scavenge_psd(void *addr, size_t st_size, struct psd_header *h);
void scan_channels(unsigned char *addr, size_t len, struct psd_header *h);

void setupfile(char *dstname,char *dir,char *name,char *suffix);
FILE* pngsetupwrite(psd_file_t psd, char *dir, char *name, psd_pixels_t width, psd_pixels_t height,
					int channels, int color_type, struct layer_info *li, struct psd_header *h);
void pngwriteimage(FILE *png,psd_file_t psd, int comp[], struct layer_info *li, psd_bytes_t **rowpos,
				   int startchan, int pngchan, psd_pixels_t rows, psd_pixels_t cols, struct psd_header *h);

FILE* rawsetupwrite(psd_file_t psd, char *dir, char *name, psd_pixels_t width, psd_pixels_t height,
					int channels, int color_type, struct layer_info *li, struct psd_header *h);
void rawwriteimage(FILE *png,psd_file_t psd, int comp[], struct layer_info *li, psd_bytes_t **rowpos,
				   int startchan, int pngchan, psd_pixels_t rows, psd_pixels_t cols, struct psd_header *h);

int unpackbits(unsigned char *outp,unsigned char *inp,psd_pixels_t rowbytes,psd_pixels_t inlen);

void *map_file(int fd, size_t len);
void unmap_file(void *addr, size_t len);

#ifdef WIN32
	#include <direct.h>
	#define MKDIR(name,mode) _mkdir(name) // laughable, isn't it.
#else
	#if defined(macintosh) && !defined(_SYS_STAT_H_)
		// don't clash with OS X header -- this prototype is meant for MPW build.
		int mkdir(char *s,int mode);
	#else
		#include <sys/stat.h>
	#endif
	#define MKDIR mkdir
#endif
