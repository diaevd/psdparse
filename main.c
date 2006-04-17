/*
    This file is part of "psdparse"
    Copyright (C) 2004-6 Toby Thain, toby@telegraphics.com.au

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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include "psdparse.h"

enum{ 
	CONTEXTROWS = 3, 
	WARNLIMIT = 10
};

#define DIRSUFFIX "_png"

struct resdesc rdesc[] = {
	{1000,"PS2.0 mode data"},
	{1001,"Macintosh print record"},
	{1003,"PS2.0 indexed color table"},
	{1005,"ResolutionInfo"},
	{1006,"Names of the alpha channels"},
	{1007,"DisplayInfo"},
	{1008,"Caption"},
	{1009,"Border information"},
	{1010,"Background color"},
	{1011,"Print flags"},
	{1012,"Grayscale/multichannel halftoning info"},
	{1013,"Color halftoning info"},
	{1014,"Duotone halftoning info"},
	{1015,"Grayscale/multichannel transfer function"},
	{1016,"Color transfer functions"},
	{1017,"Duotone transfer functions"},
	{1018,"Duotone image info"},
	{1019,"B&W values for the dot range"},
	{1021,"EPS options"},
	{1022,"Quick Mask info"},
	{1024,"Layer state info"},
	{1025,"Working path"},
	{1026,"Layers group info"},
	{1028,"IPTC-NAA record (File Info)"},
	{1029,"Image mode for raw format files"},
	{1030,"JPEG quality"},
	{1032,"Grid and guides info"},
	{1033,"Thumbnail resource"},
	{1034,"Copyright flag"},
	{1035,"URL"},
	{1036,"Thumbnail resource"},
	{1037,"Global Angle"},
	{1038,"Color samplers resource"},
	{1039,"ICC Profile"},
	{1040,"Watermark"},
	{1041,"ICC Untagged"},
	{1042,"Effects visible"},
	{1043,"Spot Halftone"},
	{1044,"Document specific IDs"},
	{1045,"Unicode Alpha Names"},
	{1046,"Indexed Color Table Count"},
	{1047,"Transparent Index"},
	{1049,"Global Altitude"},
	{1050,"Slices"},
	{1051,"Workflow URL"},
	{1052,"Jump To XPEP"},
	{1053,"Alpha Identifiers"},
	{1054,"URL List"},
	{1057,"Version Info"},
	{2999,"Name of clipping path"},
	{10000,"Print flags info"},
	{0,NULL}
};

char *mode_names[]={
	"Bitmap", "GrayScale", "IndexedColor", "RGBColor",
	"CMYKColor", "HSLColor", "HSBColor", "Multichannel",
	"Duotone", "LabColor", "Gray16", "RGB48",
	"Lab48", "CMYK64", "DeepMultichannel", "Duotone16"
};

int verbose = DEFAULT_VERBOSE,mergedalpha = 0,quiet = 0,makedirs = 0,help = 0;
char *pngdir = NULL,indir[PATH_MAX];
FILE *listfile = NULL;
#ifdef ALWAYS_WRITE_PNG
	// for the Windows console app, we want to be able to drag and drop a PSD
	// giving us no way to specify a destination directory, so use a default
	int writepng = 1,writelist = 1;
#else
	int writepng = 0,writelist = 0;
#endif

void fatal(char *s){ fputs(s,stderr); exit(EXIT_FAILURE); }

static int nwarns = 0;
void warn(char *fmt,...){
	char s[0x200];
	va_list v;

	if(nwarns == WARNLIMIT) fputs("# (further warnings suppressed)\n",stderr);
	++nwarns;
	if(nwarns <= WARNLIMIT){
		va_start(v,fmt);
		vsnprintf(s,0x200,fmt,v);
		va_end(v);
		fprintf(stderr,"# warning: %s\n",s);
	}
}

void *checkmalloc(long n){
	void *p = malloc(n);
	if(p) return p;
	else fatal("can't get memory");
	return NULL;
}

long get4B(FILE *f){
	long n = fgetc(f)<<24;
	n |= fgetc(f)<<16;
	n |= fgetc(f)<<8;
	return n | fgetc(f);
}

short get2B(FILE *f){
	short n = fgetc(f)<<8;
	return n | fgetc(f);
}

void skipblock(FILE *f,char *desc){
	long n = get4B(f);
	if(n){
		fseek(f,n,SEEK_CUR);
		VERBOSE("  ...skipped %s (%ld bytes)\n",desc,n);
	}else
		VERBOSE("  (%s is empty)\n",desc);
}

void dumprow(unsigned char *b,int n){
	int k,m = n>25 ? 25 : n;
	for(k=0;k<m;++k) 
		VERBOSE("%02x",b[k]);
	if(n>m) VERBOSE(" ...%d more",n-m);
	VERBOSE("\n");
}

int dochannel(FILE *f,int channels,int rows,int cols,int depth,long **rowpos){
	int j,k,ch,dumpit;
	long pos;
	unsigned char *rowbuf;
	unsigned rb,n,*rlebuf = NULL;
	static char *comptype[] = {"raw","RLE"};

	unsigned comp = get2B(f);
	if(comp>RLECOMP)
		fatal("# bad compression value\n");
	VERBOSE("  compression = %d (%s)\n",comp,comptype[comp]);

	rb = (cols*depth + 7)/8;
	VERBOSE("  uncompressed size %ld bytes (row bytes = %d)\n",(long)channels*rows*rb,rb);

	rowbuf = checkmalloc(rb*2); /* slop for worst case RLE overhead (usually (rb/127+1) ) */
	pos = ftell(f);

	if(comp == RLECOMP){
		pos += 2*channels*rows; /* image data starts after RLE counts */
		rlebuf = checkmalloc(channels*rows*sizeof(unsigned));
		/* accumulate RLE counts, to make array of row start positions */
		for(ch=k=0;ch<channels;++ch){
			for(j=0;j<rows && !feof(f);++j,++k){
				rlebuf[k] = get2B(f);
				//printf("rowpos[%d][%3d]=%6d\n",ch,j,pos);
				if(rowpos) rowpos[ch][j] = pos;
				pos += rlebuf[k];
			}
			if(rowpos) rowpos[ch][j] = pos; /* = end of last row */
			if(j < rows) fatal("# couldn't read RLE counts");
		}
	}else if(rowpos){
		/* make array of row start positions (uncompressed; each row is rb bytes) */
		for(ch=0;ch<channels;++ch){
			for(j=0;j<rows;++j){
				rowpos[ch][j] = pos;
				pos += rb;
			}
			rowpos[ch][j] = pos; /* = end of last row */
		}
	}

	for(ch = k = 0 ; ch < channels ; ++ch){
		
		if(channels>1) VERBOSE("\n    channel %d:\n",ch);

		for(j=0;j<rows;++j){
			if(rows > 3*CONTEXTROWS){
				if(j==rows-CONTEXTROWS) 
					VERBOSE("    ...%d rows not shown...\n",rows-2*CONTEXTROWS);
				dumpit = j<CONTEXTROWS || j>=rows-CONTEXTROWS;
			}else 
				dumpit = 1;

			if(comp == RLECOMP){
				n = rlebuf[k++];
				if(fread(rowbuf,1,n,f) == n){
					if(dumpit){
						VERBOSE("    %4d: <%4d> ",j,n);
						dumprow(rowbuf,n);
					}
				}else fatal("# couldn't read RLE row!\n");
			}
			else if(comp == RAWDATA){
				if(fread(rowbuf,1,rb,f) == rb){
					if(dumpit){
						VERBOSE("    %4d: ",j);
						dumprow(rowbuf,rb);
					}
				}else fatal("# couldn't read raw row!\n");
			}

		}

	}
	VERBOSE("\n");

	if(comp == RLECOMP) free(rlebuf);
	free(rowbuf);

	return comp;
}

#define BITSTR(f) ((f) ? "(1)" : "(0)")

void doimage(FILE *f,char *indir,char *name,int merged,int channels,
			 int rows,int cols,struct psd_header *h){
	int ch,comp,*chcomp = checkmalloc(sizeof(int)*channels);
	long **rowpos = checkmalloc(sizeof(long*)*channels);
	FILE *png = NULL; /* handle to the output PNG file */

	for(ch=0;ch<channels;++ch) 
		rowpos[ch] = checkmalloc(sizeof(long)*(rows+1));

	if(writepng)
		png = pngsetupwrite(f, pngdir ? pngdir : indir, name, 
							cols, rows, channels, merged, h);

	if(merged){
		VERBOSE("  merged channels:\n");
		comp = dochannel(f,channels,rows,cols,h->depth,rowpos);
		for( ch=0 ; ch < channels ; ++ch ) 
			chcomp[ch] = comp; /* for all merged channels have same compression type */
	}else{
		for( ch=0 ; ch < channels ; ++ch ){
			VERBOSE("  channel %d:\n",ch);
			chcomp[ch] = dochannel(f,1,rows,cols,h->depth,rowpos+ch);
		}
	}

	if(png) pngwriteimage(f,chcomp,rowpos,channels,rows,cols,h->depth);

	for(ch=0;ch<channels;++ch) 
		free(rowpos[ch]);
	free(rowpos);
	free(chcomp);
}

void dolayermaskinfo(FILE *f,struct psd_header *h){
	long miscstart,misclen,layerlen,chlen,skip,extrastart,extralen;
	short nlayers;
	int i,j,chid,namelen;
	struct layer_info *linfo;
	char **lname;
	struct blend_mode_info bm;

	if( (misclen = get4B(f)) ){
		miscstart = ftell(f);

		// process layer info section
		if( (layerlen = get4B(f)) ){
			// layers structure
			nlayers = get2B(f);
			if(nlayers<0){
				nlayers = -nlayers;
				VERBOSE("  (first alpha is transparency for merged image)");
				mergedalpha = 1;
			}
			UNQUIET("  nlayers = %d\n",nlayers);
			linfo = checkmalloc(nlayers*sizeof(struct layer_info));
			lname = checkmalloc(nlayers*sizeof(char*));

			for(i=0;i<nlayers;++i){
				// process layer record
				linfo[i].top = get4B(f);
				linfo[i].left = get4B(f);
				linfo[i].bottom = get4B(f);
				linfo[i].right = get4B(f);
				linfo[i].channels = get2B(f);

				UNQUIET("  layer %d: (%ld,%ld,%ld,%ld), %d channels (%ld rows x %ld cols)\n",
						i, linfo[i].top, linfo[i].left, linfo[i].bottom, linfo[i].right, linfo[i].channels,
						linfo[i].bottom-linfo[i].top, linfo[i].right-linfo[i].left);
	
				for( j=0 ; j < linfo[i].channels ; ++j ){
					chid = get2B(f);
					chlen = get4B(f);
					VERBOSE("    channel %2d: id=%2d, %5ld bytes\n",j,chid,chlen);
				}

				fread(bm.sig,1,4,f);
				fread(bm.key,1,4,f);
				bm.opacity = fgetc(f);
				bm.clipping = fgetc(f);
				bm.flags = fgetc(f);
				bm.filler = fgetc(f);
				VERBOSE("  blending mode: sig='%c%c%c%c' key='%c%c%c%c' opacity=%d(%d%%) clipping=%d(%s)\n\
                 flags=%#x(transp_prot%s visible%s bit4valid%s pixel_data_relevant%s)\n",
						bm.sig[0],bm.sig[1],bm.sig[2],bm.sig[3],
						bm.key[0],bm.key[1],bm.key[2],bm.key[3],
						bm.opacity,(bm.opacity*100+127)/255,
						bm.clipping,bm.clipping ? "non-base" : "base",
						bm.flags, BITSTR(bm.flags&1),BITSTR(bm.flags&2),BITSTR(bm.flags&8),BITSTR(bm.flags&16) );

				//skipblock(f,"layer info: extra data");
				extralen = get4B(f);
				extrastart = ftell(f);
				//printf("  (extra data: %d bytes @ %d)\n",extralen,extrastart);

				skipblock(f,"layer mask data");
				skipblock(f,"layer blending ranges");
				
				// layer name
				namelen = fgetc(f);
				lname[i] = checkmalloc(PAD4(1+namelen));
				fread(lname[i],1,PAD4(1+namelen),f);
				lname[i][namelen] = 0;
				UNQUIET("  layer name: \"%s\"\n",lname[i]);
		
				fseek(f,extrastart+extralen,SEEK_SET); // skip over any extra data
			}
      
			if(listfile) fputs("assetlist = {\n",listfile);
				
			for(i=0;i<nlayers;++i){
				long pixw = linfo[i].right-linfo[i].left,
					 pixh = linfo[i].bottom-linfo[i].top;
				VERBOSE("  layer %d (\"%s\"):\n",i,lname[i]);
			  
				if(listfile && pixw && pixh)
					fprintf(listfile,"\t\"%s\" = { pos={%3ld,%3ld}, size={%3ld,%3ld} },\n",
							lname[i], linfo[i].left, linfo[i].top, pixw, pixh);
		
				doimage(f, indir,lname[i], 0/*not merged*/, linfo[i].channels, 
						linfo[i].bottom-linfo[i].top, linfo[i].right-linfo[i].left, h);
			}

			if(listfile) fputs("}\n",listfile);
      
		}else VERBOSE("  (layer info section is empty)\n");
		
		// process global layer mask info section
		skipblock(f,"global layer mask info");

		skip = miscstart+misclen - ftell(f);
		if(skip){
			warn("skipped %d bytes at end of misc data?",skip);
			fseek(f,skip,SEEK_CUR);
		}
		
	}else VERBOSE("  (misc info section is empty)\n");
	
}

char *finddesc(int id){
	/* dumb linear lookup of description string from resource id */
	/* assumes array ends with a NULL string pointer */
	struct resdesc *p = rdesc;
	if(id>=2000 && id<2999) return "path"; // special case
	while(p->str && p->id != id)
		++p;
	return p->str;
}

long doirb(FILE *f){
	char type[4],name[0x100],*d;
	int id,namelen;
	long size;

	fread(type,1,4,f);
	id = get2B(f);
	namelen = fgetc(f);
	fread(name,1,PAD2(1+namelen)-1,f);
	name[namelen] = 0;
	size = get4B(f);
	fseek(f,PAD2(size),SEEK_CUR);

	VERBOSE("  resource '%c%c%c%c' (%5d,\"%s\"):%5ld bytes",
			type[0],type[1],type[2],type[3],id,name,size);
	if( (d = finddesc(id)) ) VERBOSE(" [%s]",d);
	VERBOSE("\n");

	return 4+2+PAD2(1+namelen)+4+PAD2(size); /* returns total bytes in block */
}

void doimageresources(FILE *f){
	long len = get4B(f);
	while(len>0)
		len -= doirb(f);
	if(len != 0) warn("image resources overran expected size by %d bytes\n",-len);
}

int main(int argc,char *argv[]){
	struct psd_header h;
	FILE *f;
	int i,indexptr,opt;
	char *base,*ext;

	do{
		static struct option longopts[]={
			{"verbose",no_argument,&verbose,1},
			{"quiet",no_argument,&quiet,1},
			{"help",no_argument,&help,1},
			{"pngdir",required_argument,NULL,'d'},
			{"writepng",no_argument,&writepng,1},
			{"makedirs",no_argument,&makedirs,1},
			{"writelist",no_argument,&writelist,1},
			{NULL,0,NULL,0}
		};

		switch(opt = getopt_long(argc,argv,"vqhd:wml",longopts,&indexptr)){
		case 'v': verbose = 1; break;
		case 'q': quiet = 1; break;
		case 'd': pngdir = optarg;
		case 'w': writepng = 1; break;
		case 'm': makedirs = 1; break;
		case 'l': writelist = 1; break;
		case 'h':
		case '?': help = 1; break;
		}
	}while(opt != -1);

	if(help)
		fprintf(stderr,"usage: %s [options] psdfile...\n\
  -h, --help         show this help\n\
  -v, --verbose      print more information\n\
  -q, --quiet        work silently\n\
  -w, --writepng     write PNG files of each raster layer (and merged composite)\n\
  -d, --pngdir dir   put PNGs in directory (implies --writepng)\n\
  -m, --makedirs     create subdirectory for PNG if layer name contains %c's\n\
  -l, --writelist    write an 'asset list' of layer sizes and positions\n", argv[0],DIRSEP);

	for( i=optind ; i<argc ; ++i ){
		if( (f = fopen(argv[i],"rb")) ){
			nwarns = 0;

			UNQUIET("\"%s\"\n",argv[i]);

			strcpy(indir,argv[i]);
			ext = strrchr(indir,'.');
			ext ? strcpy(ext,DIRSUFFIX) : strcat(indir,DIRSUFFIX);

			if(writelist){
				char fname[FILENAME_MAX];
				extern char dirsep[];

				strcpy(fname,pngdir ? pngdir : indir);
				MKDIR(fname,0755);
				strcat(fname,dirsep);
				strcat(fname,"list.txt");
				if( (listfile = fopen(fname,"w")) )
					fprintf(listfile,"-- source file: %s\n",argv[i]);
			}

			// file header
			h.sig = get4B(f);
			h.version = get2B(f);
			get4B(f); get2B(f); // reserved[6];
			h.channels = get2B(f);
			h.rows = get4B(f);
			h.cols = get4B(f);
			h.depth = get2B(f);
			h.mode = get2B(f);

			if(!feof(f) && h.sig == '8BPS' && h.version == 1){

				UNQUIET("  channels = %d, rows = %ld, cols = %ld, depth = %d, mode = %d (%s)\n",
						h.channels, h.rows, h.cols, h.depth,
						h.mode, h.mode >= 0 && h.mode < 16 ? mode_names[h.mode] : "???");

				h.colormodepos = ftell(f);
				skipblock(f,"color mode data");
				doimageresources(f); //skipblock(f,"image resources");
				dolayermaskinfo(f,&h); //skipblock(f,"layer & mask info");

				// now process image data
				base = strrchr(argv[i],DIRSEP);
				doimage(f,indir,base ? base+1 : argv[i],1/*merged*/,h.channels,h.rows,h.cols,&h);
				//dochannel(f,h.channels,h.rows,h.cols,h.depth,NULL);

				UNQUIET("  done.\n");
			}else
				fprintf(stderr,"# \"%s\": couldn't read header, is not a PSD, or version is not 1!\n",argv[i]);

			if(listfile) fclose(listfile);
			fclose(f);
		}else fprintf(stderr,"# \"%s\": couldn't open\n",argv[i]);
	}
	return EXIT_SUCCESS;
}
