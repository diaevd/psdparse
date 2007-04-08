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

#include <stdio.h>
#include <stdlib.h>

#include "psdparse.h"
#include "png.h"

static png_structp png_ptr;
static png_infop info_ptr;

// construct the destination filename, and create enclosing directories
// as needed (and if requested).

void setupfile(char *dstname,char *dir,char *name,char *suffix){
	char *last,d[PATH_MAX];

	MKDIR(dir,0755);

	if(strchr(name,DIRSEP)){
		if(!makedirs)
			alwayswarn("# warning: replaced %c's in filename (use --makedirs if you want subdirectories)\n",DIRSEP);
		for(last = name; (last = strchr(last+1,'/')); )
			if(makedirs){
				last[0] = 0;
				strcpy(d,dir);
				strcat(d,dirsep);
				strcat(d,name);
				if(!MKDIR(d,0755)) VERBOSE("# made subdirectory \"%s\"\n",d);
				last[0] = DIRSEP;
			}else 
				last[0] = '_';
	}

	strcpy(dstname,dir);
	strcat(dstname,dirsep);
	strcat(dstname,name);
	strcat(dstname,suffix);
}

// Prepare to write the PNG file. This function:
// - creates a directory for it, if needed
// - builds the PNG file name and opens the file for writing
// - checks colour mode and sets up libpng parameters
// - fetches the colour palette for Indexed Mode images, and gives to libpng 

// Parameters:
// psd         file handle for input PSD
// dir         pointer to output dir name
// name        name for this PNG (e.g. layer name)
// width,height  image dimensions (may not be the same as PSD header dimensions)
// channels    channel count - this is purely informational
// color_type  identified PNG colour type (determined by doimage())
// li          pointer to layer info for relevant layer, or NULL if no layer (e.g. merged composite)
// h           pointer to PSD file header struct

FILE* pngsetupwrite(FILE *psd, char *dir, char *name, int width, int height, 
					int channels, int color_type, struct layer_info *li, struct psd_header *h)
{
	char pngname[PATH_MAX],*pngtype = NULL;
	static FILE *f; // static, because it might get used post-longjmp()
	unsigned char *palette;
	png_color *pngpal;
	int i,n;
	long savepos;

	f = NULL;
	
	if(width && height){
		
		setupfile(pngname,dir,name,".png");

		if(channels < 1 || channels > 4){
			alwayswarn("## (BUG) bad channel count (%d), writing PNG \"%s\"\n", channels, pngname);
			return NULL;
		}

		switch(color_type){
		case PNG_COLOR_TYPE_GRAY:       pngtype = "GRAY"; break;
		case PNG_COLOR_TYPE_GRAY_ALPHA: pngtype = "GRAY_ALPHA"; break;
		case PNG_COLOR_TYPE_PALETTE:    pngtype = "PALETTE"; break;
		case PNG_COLOR_TYPE_RGB:        pngtype = "RGB"; break;
		case PNG_COLOR_TYPE_RGB_ALPHA:  pngtype = "RGB_ALPHA"; break;
		default:
			alwayswarn("## (BUG) bad color_type (%d), %d channels (%s), writing PNG \"%s\"\n", 
					  color_type, channels, mode_names[h->mode], pngname);
			return NULL;
		}

		if( !(png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL)) ){
			alwayswarn("### pngsetupwrite: png_create_write_struct failed\n");
			return NULL;
		}

		if( (f = fopen(pngname,"wb")) ){
			if(xmlfile){
				fputs("\t\t<PNG NAME='",xmlfile);
				fputsxml(name,xmlfile);
				fputc('\'',xmlfile);
				fputs(" DIR='",xmlfile);
				fputsxml(dir,xmlfile);
				fputc('\'',xmlfile);
				fputs(" FILE='",xmlfile);
				fputsxml(pngname,xmlfile);
				fputc('\'',xmlfile);
				fprintf(xmlfile," WIDTH='%d' HEIGHT='%d' CHANNELS='%d' COLORTYPE='%d' COLORTYPENAME='%s' DEPTH='%d'",
						width,height,channels,color_type,pngtype,h->depth);
			}
			UNQUIET("# writing PNG \"%s\"\n",pngname);
			VERBOSE("#             %3dx%3d, depth=%d, channels=%d, type=%d(%s)\n", 
					width,height,h->depth,channels,color_type,pngtype);

			if( !(info_ptr = png_create_info_struct(png_ptr)) || setjmp(png_jmpbuf(png_ptr)) )
			{ /* If we get here, libpng had a problem */
				alwayswarn("### pngsetupwrite: Fatal error in libpng\n");
				fclose(f);
				png_destroy_write_struct(&png_ptr, &info_ptr);
				return NULL;
			}

			png_init_io(png_ptr, f);

			png_set_IHDR(png_ptr, info_ptr, width, height, h->depth, color_type,
						 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

			if(h->mode == ModeBitmap) 
				png_set_invert_mono(png_ptr);
			else if(h->mode == ModeIndexedColor){
				
				// go get the colour palette
				savepos = ftell(psd);
				fseek(psd,h->colormodepos,SEEK_SET);
				n = get4B(psd);
				palette = checkmalloc(n);
				fread(palette,1,n,psd);
				fseek(psd,savepos,SEEK_SET);
				
				n /= 3;
				pngpal = checkmalloc(sizeof(png_color)*n);
				for(i = 0; i < n; ++i){
					pngpal[i].red   = palette[i];
					pngpal[i].green = palette[i+n];
					pngpal[i].blue  = palette[i+2*n];
				}
				png_set_PLTE(png_ptr, info_ptr, pngpal, n);
				free(pngpal);
				free(palette);		 
			}

			png_write_info(png_ptr, info_ptr);
			
			png_set_compression_level(png_ptr,Z_BEST_COMPRESSION);

		}else alwayswarn("### can't open \"%s\" for writing\n",pngname);

	}else alwayswarn("### skipping layer \"%s\" (%dx%d)\n",li->name,width,height);

	return f;
}

void pngwriteimage(FILE *png, FILE *psd, int chcomp[], struct layer_info *li, long **rowpos,
				   int startchan, int chancount, int rows, int cols, struct psd_header *h)
{
	unsigned n,rb = (h->depth*cols+7)/8,rlebytes;
	unsigned char *rowbuf,*inrows[4],*rledata,*p;
	short *q;
	long savepos = ftell(psd);
	int i,j,ch,map[4];
	
	if(xmlfile)
		fprintf(xmlfile," CHINDEX='%d' />\n",startchan);

	rowbuf = checkmalloc(rb*chancount);
	rledata = checkmalloc(2*rb);

	for(ch = 0; ch < chancount; ++ch){
		inrows[ch] = checkmalloc(rb);
		// build mapping so that png channel 0 --> channel with id 0, etc
		// and png alpha --> channel with id -1
		map[ch] = li && chancount>1 ? li->chindex[ch] : ch;
	}
	
	// find the alpha channel, if needed
	if(chancount == 2 && li){ // grey+alpha
		if(li->chindex[-1] == -1)
			alwayswarn("### writing Grey+Alpha PNG, but no alpha found?\n");
		else
			map[1] = li->chindex[-1];
	}else if(chancount == 4 && li){ // RGB+alpha
		if(li->chindex[-1] == -1)
			alwayswarn("### writing RGB+Alpha PNG, but no alpha found?\n");
		else
			map[3] = li->chindex[-1];
	}
	
	//for( ch = 0 ; ch < chancount ; ++ch )
	//	alwayswarn("# channel map[%d] -> %d\n",ch,map[ch]);

	if( setjmp(png_jmpbuf(png_ptr)) )
	{ /* If we get here, libpng had a problem writing the file */
		alwayswarn("### pngwriteimage: Fatal error in libpng\n");
		goto done;
	}

	for(j = 0; j < rows; ++j){
		for(i = 0; i < chancount; ++i){
			// startchan must be zero for multichannel,
			// and for single channel, map[0] always == 0
			ch = startchan + map[i];
			/* get row data */
			//printf("rowpos[%d][%4d] = %7d\n",ch,j,rowpos[ch][j]);

			if(map[i] < 0 || map[i] > (li ? li->channels : h->channels)){
				warn("bad map[%d]=%d, skipping a channel",i,map[i]);
				memset(inrows[i],0,rb); // zero out the row
			}else if(fseek(psd,rowpos[ch][j],SEEK_SET) == -1){
				alwayswarn("# error seeking to %ld\n",rowpos[ch][j]);
				memset(inrows[i],0,rb); // zero out the row
			}else{

				if(chcomp[ch] == RAWDATA){ /* uncompressed row */
					n = fread(inrows[i],1,rb,psd);
					if(n != rb){
						warn("error reading row data (raw) @ %ld",rowpos[ch][j]);
						memset(inrows[i]+n,0,rb-n); // zero out the rest of the row
					}
				}
				else if(chcomp[ch] == RLECOMP){ /* RLE compressed row */
					n = rowpos[ch][j+1] - rowpos[ch][j];
					if(n > 2*rb){
						n = 2*rb; // sanity check
						warn("bad RLE count %5d @ channel %2d, row %5d",n,ch,j);
					}
					rlebytes = fread(rledata,1,n,psd);
					if(rlebytes < n){
						warn("error reading row data (RLE) @ %ld",rowpos[ch][j]);
						memset(inrows[i],0,rb); // zero it out, will probably unpack short
					}
					unpackbits(inrows[i],rledata,rb,rlebytes);
				}else // assume it is bad
					memset(inrows[i],0,rb);

			}
		}

		if(chancount>1){ /* interleave channels */
			if(h->depth == 8)
				for(i = 0, p = rowbuf; i < (int)rb; ++i)
					for(ch = 0; ch < chancount; ++ch)
						*p++ = inrows[ch][i];
			else
				for(i = 0, q = (short*)rowbuf; i < (int)rb/2; ++i)
					for(ch = 0; ch < chancount; ++ch)
						*q++ = ((short*)inrows[ch])[i];

			png_write_row(png_ptr, rowbuf);
		}else
			png_write_row(png_ptr, inrows[0]);
	}
	
	png_write_end(png_ptr, NULL /*info_ptr*/);

done:
	fclose(png);
	free(rowbuf);
	free(rledata);
	for(ch = 0; ch < chancount; ++ch)
		free(inrows[ch]);

	fseek(psd,savepos,SEEK_SET); 
	VERBOSE(">>> restoring filepos= %ld\n",savepos);

	png_destroy_write_struct(&png_ptr, &info_ptr);
}

