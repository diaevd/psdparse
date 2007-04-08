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

#include "png.h"

#define CONTEXTROWS 3

extern int nwarns;

char dirsep[] = {DIRSEP,0};
int verbose = DEFAULT_VERBOSE, quiet = 0, rsrc = 0, extra = 0,
	makedirs = 0, numbered = 0, mergedalpha = 0, help = 0, split = 0;
static char indir[PATH_MAX],*pngdir = indir;
static FILE *listfile = NULL;
FILE *xmlfile = NULL;

#ifdef ALWAYS_WRITE_PNG
	// for the Windows console app, we want to be able to drag and drop a PSD
	// giving us no way to specify a destination directory, so use a default
	static int writepng = 1,writelist = 1,writexml = 1;
#else
	static int writepng = 0,writelist = 0,writexml = 0;
#endif

void skipblock(FILE *f,char *desc){
	long n = get4B(f);
	if(n){
		fseek(f,n,SEEK_CUR);
		VERBOSE("  ...skipped %s (%ld bytes)\n",desc,n);
	}else
		VERBOSE("  (%s is empty)\n",desc);
}

void dumprow(unsigned char *b,int n,int group){
	int k,m,cols = 50;
	m = group ? group*((cols/2)/(group+1)) : cols/2;
	for(k = 0; k < m; ++k){
		if(group && !(k % group)) VERBOSE(" ");
		VERBOSE("%02x",b[k]);
	}
	if(n>m) VERBOSE(" ...%d more",group ? (n-m)/group : n-m);
	VERBOSE("\n");
}

int dochannel(FILE *f, struct layer_info *li, int idx, int channels,
			  int rows, int cols, int depth, long **rowpos)
{
	int j,k,ch,dumpit,samplebytes = depth > 8 ? depth/8 : 0;
	long pos,chpos = ftell(f),chlen = 0;
	unsigned char *rowbuf;
	unsigned count,rb,comp,last,n,*rlebuf = NULL;
	static char *comptype[] = {"raw","RLE"};

	if(li){
		chlen = li->chlengths[idx];
		VERBOSE(">>> dochannel %d/%d filepos=%7ld bytes=%7ld\n",
				idx,channels,chpos,chlen);
	}else{
		VERBOSE(">>> dochannel %d/%d filepos=%7ld\n",idx,channels,chpos);
	}

	if(li && chlen < 2){
		alwayswarn("## channel too short (%d bytes)\n",chlen);
		if(chlen > 0)
			fseek(f, chlen, SEEK_CUR); // skip it anyway, but not backwards
		return -1;
	}
	
	if(li && li->chid[idx] == -2){
		rows = li->mask.rows;
		cols = li->mask.cols;
		VERBOSE("# layer mask (%4ld,%4ld,%4ld,%4ld) (%4d rows x %4d cols)\n",
				li->mask.top,li->mask.left,li->mask.bottom,li->mask.right,rows,cols);
	}

	rb = (cols*depth + 7)/8;

	comp = get2Bu(f);
	chlen -= 2;
	if(comp > RLECOMP){
		alwayswarn("## bad compression type %d\n",comp);
		if(li){ // make a guess based on channel byte count
			comp = chlen == (long)rows*(long)rb ? RAWDATA : RLECOMP;
			alwayswarn("## guessing: %s\n",comptype[comp]);
		}else{
			alwayswarn("## skipping channel (%d bytes)\n",chlen);
			fseek(f, chlen, SEEK_CUR);
			return -1;
		}
	}else
		VERBOSE("    compression = %d (%s)\n",comp,comptype[comp]);
	VERBOSE("    uncompressed size %ld bytes (row bytes = %d)\n",(long)channels*rows*rb,rb);

	rowbuf = checkmalloc(rb*2); /* slop for worst case RLE overhead (usually (rb/127+1) ) */
	pos = ftell(f);

	if(comp == RLECOMP){
		int rlecounts = 2*channels*rows;
		if(li && chlen < rlecounts)
			alwayswarn("## channel too short for RLE row counts (need %d bytes, have %d bytes)\n",rlecounts,chlen);
			
		pos += rlecounts; /* image data starts after RLE counts */
		rlebuf = checkmalloc(channels*rows*sizeof(unsigned));
		/* accumulate RLE counts, to make array of row start positions */
		for(ch = k = 0; ch < channels; ++ch){
			last = rb;
			for(j = 0; j < rows && !feof(f); ++j, ++k){
				count = get2Bu(f);
				if(count > 2*rb)  // this would be impossible
					count = last; // make a guess, to help recover
				rlebuf[k] = last = count;
				//printf("rowpos[%d][%3d]=%6d\n",ch,j,pos);
				if(rowpos) rowpos[ch][j] = pos;
				pos += count;
			}
			if(rowpos) 
				rowpos[ch][j] = pos; /* = end of last row */
			if(j < rows) fatal("# couldn't read RLE counts");
		}
	}else if(rowpos){
		/* make array of row start positions (uncompressed; each row is rb bytes) */
		for(ch = 0; ch < channels; ++ch){
			for(j = 0; j < rows; ++j){
				rowpos[ch][j] = pos;
				pos += rb;
			}
			rowpos[ch][j] = pos; /* = end of last row */
		}
	}

	for(ch = k = 0; ch < channels; ++ch){
		
		//if(channels>1)
		VERBOSE("\n    channel %d (@ %7ld):\n",ch,ftell(f));

		for(j = 0; j < rows; ++j){
			if(rows > 3*CONTEXTROWS){
				if(j == rows-CONTEXTROWS) 
					VERBOSE("    ...%d rows not shown...\n",rows-2*CONTEXTROWS);
				dumpit = j < CONTEXTROWS || j >= rows-CONTEXTROWS;
			}else 
				dumpit = 1;

			if(comp == RLECOMP){
				n = rlebuf[k++];
				//VERBOSE("rle count[%5d] = %5d\n",j,n);
				if(n > 2*rb){
					warn("bad RLE count %5d @ row %5d",n,j);
					n = 2*rb;
				}
				if(fread(rowbuf,1,n,f) == n){
					if(dumpit){
						VERBOSE("   %5d: <%5d> ",j,n);
						dumprow(rowbuf,n,samplebytes);
					}
				}else{
					memset(rowbuf,0,n);
					warn("couldn't read RLE row!");
				}
			}
			else if(comp == RAWDATA){
				if(fread(rowbuf,1,rb,f) == rb){
					if(dumpit){
						VERBOSE("   %5d: ",j);
						dumprow(rowbuf,rb,samplebytes);
					}
				}else{
					memset(rowbuf,0,rb);
					warn("couldn't read raw row!");
				}
			}

		}

	}
	
	if(li && ftell(f) != (chpos+2+chlen)){
		alwayswarn("### currentpos = %ld, should be %ld !!\n",ftell(f),chpos+2+chlen);
		fseek(f,chpos+2+chlen,SEEK_SET);
	}

	if(comp == RLECOMP) free(rlebuf);
	free(rowbuf);

	return comp;
}

#define BITSTR(f) ((f) ? "(1)" : "(0)")

static void writechannels(FILE *f, char *dir, char *name, int chcomp[], 
						  struct layer_info *li, long **rowpos, int startchan, 
						  int channels, int rows, int cols, struct psd_header *h)
{
	FILE *png;
	char pngname[FILENAME_MAX];
	int i,ch;

	for(i = 0; i < channels; ++i){
		// build PNG file name
		strcpy(pngname,name);
		ch = li ? li->chid[startchan + i] : startchan + i;
		if(ch == -2){
			if(xmlfile)
				fprintf(xmlfile,"\t\t<LAYERMASK TOP='%ld' LEFT='%ld' BOTTOM='%ld' RIGHT='%ld' ROWS='%ld' COLUMNS='%ld'>\n",
						li->mask.top, li->mask.left, li->mask.bottom, li->mask.right, li->mask.rows, li->mask.cols);
			strcat(pngname,".lmask");
			// layer mask channel is a special case, gets its own dimensions
			rows = li->mask.rows;
			cols = li->mask.cols;
		}else if(ch == -1){
			if(xmlfile) fputs("\t\t<TRANSPARENCY>\n",xmlfile);
			strcat(pngname,li ? ".trans" : ".alpha");
		}else if(ch < (int)strlen(channelsuffixes[h->mode])) // can identify channel by letter
			sprintf(pngname+strlen(pngname),".%c",channelsuffixes[h->mode][ch]);
		else // give up an use a number
			sprintf(pngname+strlen(pngname),".%d",ch);
			
		if(chcomp[i] == -1)
			alwayswarn("## not writing \"%s\", bad channel compression type\n",pngname);
		else if(h->depth == 32){
			if((png = rawsetupwrite(f,dir,pngname,cols,rows,1,0,li,h)))
				rawwriteimage(png,f,chcomp,li,rowpos,startchan+i,1,rows,cols,h);
		}else if((png = pngsetupwrite(f,dir,pngname,cols,rows,1,PNG_COLOR_TYPE_GRAY,li,h)))
			pngwriteimage(png,f,chcomp,li,rowpos,startchan+i,1,rows,cols,h);

		if(ch == -2){
			if(xmlfile) fputs("\t\t</LAYERMASK>\n",xmlfile);
		}else if(ch == -1){
			if(xmlfile) fputs("\t\t</TRANSPARENCY>\n",xmlfile);
		}
	}
}

void doimage(FILE *f, struct layer_info *li, char *name,
			 int channels, int rows, int cols, struct psd_header *h)
{
	FILE *png;
	int ch,comp,startchan,pngchan,color_type,
		*chcomp = checkmalloc(sizeof(int)*channels);
	long **rowpos = checkmalloc(sizeof(long*)*channels);

	for(ch = 0; ch < channels; ++ch){
		// is it a layer mask? if so, use special case row count
		int chrows = li && li->chid[ch] == -2 ? li->mask.rows : rows;
		rowpos[ch] = checkmalloc(sizeof(long)*(chrows+1));
	}

	pngchan = color_type = 0;
	switch(h->mode){
	case ModeBitmap:
	case ModeGrayScale:
	case ModeGray16:
	case ModeDuotone:
	case ModeDuotone16:
		color_type = PNG_COLOR_TYPE_GRAY;
		pngchan = 1;
		// check if there is an alpha channel, or if merged data has alpha
		if( (li && li->chindex[-1] != -1) || (channels>1 && mergedalpha) ){
			color_type = PNG_COLOR_TYPE_GRAY_ALPHA;
			pngchan = 2;
		}
		break;
	case ModeIndexedColor:
		color_type = PNG_COLOR_TYPE_PALETTE;
		pngchan = 1;
		break;
	case ModeRGBColor:
	case ModeRGB48:
		color_type = PNG_COLOR_TYPE_RGB;
		pngchan = 3;
		if( (li && li->chindex[-1] != -1) || (channels>3 && mergedalpha) ){
			color_type = PNG_COLOR_TYPE_RGB_ALPHA;
			pngchan = 4;
		}
		break;
	}

	if(!li){
		VERBOSE("\n  merged channels:\n");
		
		// The 'merged' or 'composite' image is where the flattened image is stored
		// when 'Maximise Compatibility' is used.
		// It consists of:
		// - the alpha channel for merged image (if mergedalpha is TRUE)
		// - the merged image (1 or 3 channels)
		// - any remaining alpha or spot channels.
		// For an identifiable image mode (Bitmap, GreyScale, Duotone, Indexed or RGB), 
		// we should ideally 
		// 1) write the first 1[2] or 3[4] channels in appropriate PNG format
		// 2) write the remaining channels as extra GRAY PNG files.
		// (For multichannel (and maybe other?) modes, we should just write all
		// channels per step 2)
		
		comp = dochannel(f,NULL,0/*no index*/,channels,rows,cols,h->depth,rowpos);
		for(ch = 0; ch < channels; ++ch) 
			chcomp[ch] = comp; /* merged channels share same compression type */
		
		if(xmlfile)
			fprintf(xmlfile,"\t<COMPOSITE CHANNELS='%d' HEIGHT='%d' WIDTH='%d'>\n",
					channels,rows,cols);
		if(writepng){
			nwarns = 0;
			startchan = 0;
			if(pngchan && !split){
				if(h->depth == 32){
					if((png = rawsetupwrite(f, pngdir, name, cols, rows, channels, 0, li, h)))
						rawwriteimage(png, f, chcomp, NULL, rowpos, 0, channels, rows, cols, h);
				}else{
					// recognisable PNG mode, so spit out the merged image
					if((png = pngsetupwrite(f, pngdir, name, cols, rows, pngchan, color_type, li, h)))
						pngwriteimage(png, f, chcomp, NULL, rowpos, 0, pngchan, rows, cols, h);
				}
				startchan += pngchan;
			}
			if(startchan < channels){
				if(!pngchan)
					UNQUIET("# writing %s image as split channels...\n",mode_names[h->mode]);
				writechannels(f, pngdir, name, chcomp, NULL, rowpos, 
							  startchan, channels-startchan, rows, cols, h);
			}
		}
		if(xmlfile) fputs("\t</COMPOSITE>\n",xmlfile);
	}else{
		// Process layer:
		// for each channel, store its row pointers sequentially 
		// in the rowpos[] array, and its compression type in chcomp[] array
		// (pngwriteimage() will take care of interleaving this data for libpng)
		for(ch = 0; ch < channels; ++ch){
			VERBOSE("  channel %d:\n",ch);
			chcomp[ch] = dochannel(f,li,ch,1/*count*/,rows,cols,h->depth,rowpos+ch);
		}
		if(writepng){
			nwarns = 0;
			if(pngchan && !split){
				if(h->depth == 32){
					if((png = rawsetupwrite(f, pngdir, name, cols, rows, channels, 0, li, h)))
						rawwriteimage(png, f, chcomp, li, rowpos, 0, channels, rows, cols, h);
				}else{
					if((png = pngsetupwrite(f, pngdir, name, cols, rows, pngchan, color_type, li, h)))
						pngwriteimage(png, f, chcomp, li, rowpos, 0, pngchan, rows, cols, h);
				}
				// spit out any 'extra' channels (e.g. layer transparency)
				for(ch = 0; ch < channels; ++ch)
					if(li->chid[ch] < -1 || li->chid[ch] > pngchan)
						writechannels(f, pngdir, name, chcomp, li, rowpos,
									  ch, 1, rows, cols, h);
			}else{
				UNQUIET("# writing layer as split channels...\n");
				writechannels(f, pngdir, name, chcomp, li, rowpos,
							  0, channels, rows, cols, h);
			}
		}
	}

	for(ch = 0; ch < channels; ++ch) 
		free(rowpos[ch]);
	free(rowpos);
	free(chcomp);
}

void dolayermaskinfo(FILE *f, struct psd_header *h){
	long miscstart,misclen,layerlen,chlen,skip,extrastart,extralen;
	int nlayers,i,j,chid,namelen;
	struct layer_info *linfo;
	char *chidstr,tmp[10];
	struct blend_mode_info bm;

	if( (misclen = get4B(f)) ){
		miscstart = ftell(f);

		// process layer info section
		if( (layerlen = get4B(f)) ){
			// layers structure
			nlayers = get2B(f);
			if(nlayers < 0){
				nlayers = -nlayers;
				VERBOSE("  (first alpha is transparency for merged image)\n");
				mergedalpha = 1;
			}
			UNQUIET("\n%d layers:\n",nlayers);
			
			if( nlayers*(18+6*h->channels) > layerlen ){ // sanity check
				alwayswarn("### unlikely number of layers, giving up.\n");
				return;
			}
			
			linfo = checkmalloc(nlayers*sizeof(struct layer_info));

			// load linfo[] array with each layer's info

			for(i = 0; i < nlayers; ++i){
				// process layer record
				linfo[i].top = get4B(f);
				linfo[i].left = get4B(f);
				linfo[i].bottom = get4B(f);
				linfo[i].right = get4B(f);
				linfo[i].channels = get2Bu(f);
				
				VERBOSE("\n");
				UNQUIET("  layer %d: (%4ld,%4ld,%4ld,%4ld), %d channels (%4ld rows x %4ld cols)\n",
						i, linfo[i].top, linfo[i].left, linfo[i].bottom, linfo[i].right, linfo[i].channels,
						linfo[i].bottom-linfo[i].top, linfo[i].right-linfo[i].left);

				if( linfo[i].bottom < linfo[i].top || linfo[i].right < linfo[i].left || linfo[i].channels > 64 ){ // sanity check
					alwayswarn("### something's not right about that, trying to skip layer.\n");
					fseek(f,6*linfo[i].channels+12,SEEK_CUR);
					skipblock(f,"layer info: extra data");
				}else{

					linfo[i].chlengths = checkmalloc(linfo[i].channels*sizeof(long));
					linfo[i].chid = checkmalloc(linfo[i].channels*sizeof(int));
					linfo[i].chindex = checkmalloc((linfo[i].channels+2)*sizeof(int));
					linfo[i].chindex += 2; // so we can index array from [-2] (hackish)
					
					for(j = -2; j < linfo[i].channels; ++j)
						linfo[i].chindex[j] = -1;
		
					// fetch info on each of the layer's channels
					
					for(j = 0; j < linfo[i].channels; ++j){
						chid = linfo[i].chid[j] = get2B(f);
						chlen = linfo[i].chlengths[j] = get4B(f);
						
						if(chid >= -2 && chid < linfo[i].channels)
							linfo[i].chindex[chid] = j;
						else
							warn("unexpected channel id %d",chid);
							
						switch(chid){
						case -2: chidstr = " (layer mask)"; break;
						case -1: chidstr = " (transparency mask)"; break;
						default:
							if(chid < (int)strlen(channelsuffixes[h->mode]))
								sprintf(chidstr = tmp, " (%c)", channelsuffixes[h->mode][chid]); // it's a mode-ish channel
							else
								chidstr = ""; // can't explain it
						}
						VERBOSE("    channel %2d: %7ld bytes, id=%2d %s\n",j,chlen,chid,chidstr);
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
					VERBOSE("  (extra data: %ld bytes @ %ld)\n",extralen,extrastart);

					// fetch layer mask data
					if( (linfo[i].mask.size = get4B(f)) ){
						linfo[i].mask.top = get4B(f);
						linfo[i].mask.left = get4B(f);
						linfo[i].mask.bottom = get4B(f);
						linfo[i].mask.right = get4B(f);
						linfo[i].mask.default_colour = fgetc(f);
						linfo[i].mask.flags = fgetc(f);
						fseek(f,linfo[i].mask.size-18,SEEK_CUR); // skip remainder
						linfo[i].mask.rows = linfo[i].mask.bottom - linfo[i].mask.top;
						linfo[i].mask.cols = linfo[i].mask.right - linfo[i].mask.left;
					}else
						VERBOSE("  (no layer mask)\n");
			
					skipblock(f,"layer blending ranges");
					
					// layer name
					linfo[i].nameno = malloc(16);
					sprintf(linfo[i].nameno,"layer%d",i+1);
					namelen = fgetc(f);
					linfo[i].name = checkmalloc(PAD4(namelen+1));
					fread(linfo[i].name,1,PAD4(namelen+1)-1,f);
					linfo[i].name[namelen] = 0;
					if(namelen){
						UNQUIET("    name: \"%s\"\n",linfo[i].name);
						if(linfo[i].name[0] == '.')
							linfo[i].name[0] = '_';
					}
					
					linfo[i].extradatapos = ftell(f);
					linfo[i].extradatalen = extrastart + extralen - linfo[i].extradatapos;
					if(extra)
						doextradata(f, linfo[i].extradatalen, 0);
			
					fseek(f,extrastart+extralen,SEEK_SET);
				}
			}
      
      		// loop over each layer described by layer info section,
      		// spit out a line in asset list if requested, and call
      		// doimage() to process its image data
      
			if(listfile) fputs("assetlist = {\n",listfile);
				
			for(i = 0; i < nlayers; ++i){
				long pixw = linfo[i].right - linfo[i].left,
					 pixh = linfo[i].bottom - linfo[i].top,
					 savepos;
				VERBOSE("\n  layer %d (\"%s\"):\n",i,linfo[i].name);
			  
				if(listfile && pixw && pixh){
					if(numbered)
						fprintf(listfile,"\t\"%s\" = { pos={%4ld,%4ld}, size={%4ld,%4ld} }, -- %s\n",
								linfo[i].nameno, linfo[i].left, linfo[i].top, pixw, pixh, linfo[i].name);
					else
						fprintf(listfile,"\t\"%s\" = { pos={%4ld,%4ld}, size={%4ld,%4ld} },\n",
								linfo[i].name, linfo[i].left, linfo[i].top, pixw, pixh);
				}
				if(xmlfile){
					fputs("\t<LAYER NAME='",xmlfile);
					fputsxml(linfo[i].name,xmlfile);
					fprintf(xmlfile,"' TOP='%ld' LEFT='%ld' BOTTOM='%ld' RIGHT='%ld' WIDTH='%ld' HEIGHT='%ld'>\n",
							linfo[i].top, linfo[i].left, linfo[i].bottom, linfo[i].right, pixw, pixh);
				}
				doimage(f, linfo+i, numbered ? linfo[i].nameno : linfo[i].name,
						linfo[i].channels, pixh, pixw, h);

				if(extra){
					// Process 'extra data' (non-image layer data,
					// such as adjustments, effects, type tool).
					savepos = ftell(f);
					fseek(f, linfo[i].extradatapos, SEEK_SET);
					doextradata(f, linfo[i].extradatalen, 1);
					fseek(f, savepos, SEEK_SET); // restore file position
				}
				if(xmlfile) fputs("\t</LAYER>\n",xmlfile);
			}

			if(listfile) fputs("}\n",listfile);
      
		}else VERBOSE("  (layer info section is empty)\n");
		
		// process global layer mask info section
		skipblock(f,"global layer mask info");

		skip = miscstart + misclen - ftell(f);
		if(extra){
			// skip undocumented block before 'global'(?) 'extra data'
			int n = get2B(f); // I am guessing it's preceded by a count
			fseek(f, n, SEEK_CUR);
			doextradata(f, skip-2, 1);
		}else
			if(skip)
				warn("skipped %d bytes of extra data at the end of misc info",skip);

		fseek(f, miscstart + misclen, SEEK_SET);
		
	}else VERBOSE("  (misc info section is empty)\n");
	
}

int main(int argc,char *argv[]){
	struct psd_header h;
	FILE *f;
	int i,indexptr,opt;
	char *base,*ext,fname[PATH_MAX],*dirsuffix;
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

	while( (opt = getopt_long(argc,argv,"hvqrewnd:mlxs",longopts,&indexptr)) != -1 )
		switch(opt){
		case 'h':
		default:  help = 1; break;
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
  -e, --extra        process 'extra data' (non-image layers, v4 and later)\n\
  -w, --writepng     write PNG files of each raster layer (and merged composite)\n\
  -n, --numbered     use 'layerNN' name for file, instead of actual layer name\n\
  -d, --pngdir dir   put PNGs in directory (implies --writepng)\n\
  -m, --makedirs     create subdirectory for PNG if layer name contains %c's\n\
  -l, --list         write an 'asset list' of layer sizes and positions\n\
  -x, --xml          write XML describing document and layers\n\
  -s, --split        write each composite channel to individual (grey scale) PNG\n", argv[0],DIRSEP);

	for(i = optind; i < argc; ++i){
		if( (f = fopen(argv[i],"rb")) ){
			nwarns = 0;

			UNQUIET("\"%s\"\n",argv[i]);

			// file header
			fread(h.sig,1,4,f);
			h.version  = get2Bu(f);
			get4B(f); get2B(f); // reserved[6];
			h.channels = get2Bu(f);
			h.rows     = get4B(f);
			h.cols     = get4B(f);
			h.depth    = get2Bu(f);
			h.mode     = get2Bu(f);

			strcpy(indir,argv[i]);
			ext = strrchr(indir,'.');
			dirsuffix = h.depth < 32 ? "_png" : "_raw";
			ext ? strcpy(ext,dirsuffix) : strcat(indir,dirsuffix);

			if(writelist){
				setupfile(fname,pngdir,"list",".txt");
				listfile = fopen(fname,"w");
			}
			if(writexml){
				setupfile(fname,pngdir,"psd",".xml");
				if( (xmlfile = fopen(fname,"w")) )
					fputs("<?xml version=\"1.0\"?>\n",xmlfile);
			}

			if(!feof(f) && !memcmp(h.sig,"8BPS",4) && h.version == 1){
				if(listfile) fprintf(listfile,"-- PSD file: %s\n",argv[i]);
				if(xmlfile){
					fputs("<PSD FILE='",xmlfile);
					fputsxml(argv[i],xmlfile);
					fprintf(xmlfile,"' VERSION='%d' CHANNELS='%d' ROWS='%ld' COLUMNS='%ld' DEPTH='%d' MODE='%d' MODENAME='%s'>\n",
							h.version,h.channels,h.rows,h.cols,h.depth,h.mode,
							h.mode >= 0 && h.mode < 16 ? mode_names[h.mode] : "unknown");
				}
				UNQUIET("  channels = %d, rows = %ld, cols = %ld, depth = %d, mode = %d (%s)\n",
						h.channels, h.rows, h.cols, h.depth,
						h.mode, h.mode >= 0 && h.mode < 16 ? mode_names[h.mode] : "???");
				
				if(h.channels <= 0 || h.channels > 64 || h.rows <= 0 || 
					 h.cols <= 0 || h.depth < 0 || h.depth > 32 || h.mode < 0)
					alwayswarn("### something isn't right about that header, giving up now.\n");
				else{
					h.colormodepos = ftell(f);
					skipblock(f,"color mode data");

					if(rsrc)
						doimageresources(f, xmlfile);
					else
						skipblock(f,"image resources");

					dolayermaskinfo(f,&h); //skipblock(f,"layer & mask info");
	
					// now process image data
					base = strrchr(argv[i],DIRSEP);
					doimage(f,NULL,base ? base+1 : argv[i],h.channels,h.rows,h.cols,&h);
	
					UNQUIET("  done.\n\n");
				}
				if(xmlfile) fputs("</PSD>\n",xmlfile);
			}else
				alwayswarn("# \"%s\": couldn't read header, is not a PSD, or version is not 1!\n",argv[i]);

			if(listfile) fclose(listfile);
			if(xmlfile) fclose(xmlfile);
			fclose(f);
			
			// parsing completed.
		}else
			alwayswarn("# \"%s\": couldn't open\n",argv[i]);
	}
	return EXIT_SUCCESS;
}
