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

#include <getopt.h>

#include "psdparse.h"

extern int nwarns;
extern char indir[];

char *pngdir = indir;
int verbose = DEFAULT_VERBOSE, quiet = 0, rsrc = 0, extra = 0,
	makedirs = 0, numbered = 0, mergedalpha = 0, help = 0, split = 0;

#ifdef ALWAYS_WRITE_PNG
	// for the Windows console app, we want to be able to drag and drop a PSD
	// giving us no way to specify a destination directory, so use a default
	int writepng = 1,writelist = 1,writexml = 1;
#else
	int writepng = 0,writelist = 0,writexml = 0;
#endif

int main(int argc,char *argv[]){
	static struct option longopts[] = {
		{"help",     no_argument, &help, 1},
		{"verbose",  no_argument, &verbose, 1},
		{"quiet",    no_argument, &quiet, 1},
		{"resources",no_argument, &rsrc, 1},
		{"extra",    no_argument, &extra, 1},
		{"writepng", no_argument, &writepng, 1},
		{"numbered", no_argument, &numbered, 1},
		{"pngdir",   required_argument, NULL, 'd'},
		{"makedirs", no_argument, &makedirs, 1},
		{"list",     no_argument, &writelist, 1},
		{"xml",      no_argument, &writexml, 1},
		{"split",    no_argument, &split, 1},
		{NULL,0,NULL,0}
	};
	FILE *f;
	int i,indexptr,opt;

	while( (opt = getopt_long(argc,argv,"hvqrewnd:mlxs",longopts,&indexptr)) != -1 )
		switch(opt){
		default:
		case 'h': help = 1; break;
		case 'v': verbose = 1; break;
		case 'q': quiet = 1; break;
		case 'r': rsrc = 1; break;
		case 'e': extra = 1; break;
		case 'w': writepng = 1; break;
		case 'n': numbered = 1; break;
		case 'd': pngdir = optarg;
		case 'm': makedirs = 1; break;
		case 'l': writelist = 1; break;
		case 'x': writexml = 1; break;
		case 's': split = 1; break;
		}

	if(help || optind >= argc)
		fprintf(stderr,"usage: %s [options] psdfile...\n\
  -h, --help         show this help\n\
  -v, --verbose      print more information\n\
  -q, --quiet        work silently\n\
  -r, --resources    process 'image resources' metadata\n\
  -e, --extra        process 'additional data' (non-image layers, v4 and later)\n\
  -w, --writepng     write PNG files of each raster layer (and merged composite)\n\
  -n, --numbered     use 'layerNN' name for file, instead of actual layer name\n\
  -d, --pngdir dir   put PNGs in directory (implies --writepng)\n\
  -m, --makedirs     create subdirectory for PNG if layer name contains %c's\n\
  -l, --list         write an 'asset list' of layer sizes and positions\n\
  -x, --xml          write XML describing document and layers\n\
  -s, --split        write each composite channel to individual (grey scale) PNG\n", argv[0], DIRSEP);

	for(i = optind; i < argc; ++i){
		if( (f = fopen(argv[i], "rb")) ){
			nwarns = 0;
			UNQUIET("\"%s\"\n", argv[i]);
			dopsd(f, argv[i]);
			fclose(f);
		}else
			alwayswarn("# \"%s\": couldn't open\n", argv[i]);
	}
	return EXIT_SUCCESS;
}
