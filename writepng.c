/*
    This file is part of "psdparse"
    Copyright (C) 2004-9 Toby Thain, toby@telegraphics.com.au

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

FILE* pngsetupwrite(psd_file_t psd, char *dir, char *name, psd_pixels_t width, psd_pixels_t height, 
					int channels, int color_type, struct layer_info *li, struct psd_header *h)
{
	char pngname[PATH_MAX], *pngtype = NULL;
	static FILE *f; // static, because it might get used post-longjmp()
	png_color *pngpal;
	int i, n;
	psd_bytes_t savepos;

	f = NULL;
	
	if(width && height){
		
		setupfile(pngname, dir, name, ".png");

		if(channels < 1 || channels > 4){
			alwayswarn("## (BUG) bad channel count (%d), writing PNG \"%s\"\n", channels, pngname);
			if(channels > 4)
				channels = 4; // try anyway
			else
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

		if( (f = fopen(pngname, "wb")) ){
			if(xml){
				fputs("\t\t\t<PNG NAME='", xml);
				fputsxml(name, xml);
				fputs("' DIR='", xml);
				fputsxml(dir, xml);
				fputs("' FILE='", xml);
				fputsxml(pngname, xml);
				fprintf(xml, "' WIDTH='%ld' HEIGHT='%ld' CHANNELS='%d' COLORTYPE='%d' COLORTYPENAME='%s' DEPTH='%d'",
						width, height, channels, color_type, pngtype, h->depth);
			}
			UNQUIET("# writing PNG \"%s\"\n", pngname);
			VERBOSE("#             %3ldx%3ld, depth=%d, channels=%d, type=%d(%s)\n", 
					width, height, h->depth, channels, color_type, pngtype);

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
				savepos = ftello(psd);
				fseeko(psd, h->colormodepos, SEEK_SET);
				n = get4B(psd)/3;
				if(n > 256){ // sanity check...
					warn("# more than 256 entries in colour palette! (%d)\n", n);
					n = 256;
				}
				pngpal = checkmalloc(sizeof(png_color)*n);
				for(i = 0; i < n; ++i) pngpal[i].red   = fgetc(psd);
				for(i = 0; i < n; ++i) pngpal[i].green = fgetc(psd);
				for(i = 0; i < n; ++i) pngpal[i].blue  = fgetc(psd);
				fseeko(psd, savepos, SEEK_SET);
				png_set_PLTE(png_ptr, info_ptr, pngpal, n);
				free(pngpal);
			}

			png_write_info(png_ptr, info_ptr);
			
			png_set_compression_level(png_ptr, Z_BEST_COMPRESSION);

		}else alwayswarn("### can't open \"%s\" for writing\n", pngname);

	}else alwayswarn("### skipping layer \"%s\" (%ldx%ld)\n", li->name, width, height);

	return f;
}

void pngwriteimage(FILE *png, psd_file_t psd, int chcomp[], struct layer_info *li, psd_bytes_t **rowpos,
				   int startchan, int chancount, psd_pixels_t rows, psd_pixels_t cols, struct psd_header *h)
{
	psd_pixels_t i, j, rb = (h->depth*cols+7)/8;
	uint16_t *q;
	unsigned char *rowbuf, *inrows[4], *rledata, *p;
	int ch, map[4];
	
	if(xml)
		fprintf(xml, " CHINDEX='%d' />\n", startchan);

	rowbuf = checkmalloc(rb*chancount);
	rledata = checkmalloc(2*rb);

	for(ch = 0; ch < chancount; ++ch){
		inrows[ch] = checkmalloc(rb);
		// build mapping so that png channel 0 --> channel with id 0, etc
		// and png alpha --> channel with id -1
		map[ch] = li && chancount > 1 ? li->chindex[ch] : ch;
	}
	
	// find the alpha channel, if needed
	// FIXME: does this work for the merged image alpha??
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
		goto err;
	}

	for(j = 0; j < rows; ++j){
		for(i = 0; i < chancount; ++i){
			// startchan must be zero for multichannel,
			// and for single channel, map[0] always == 0
			ch = startchan + map[i];
			/* get row data */
			//printf("rowpos[%d][%4d] = %7d\n",ch,j,rowpos[ch][j]);

			if(map[i] < 0 || map[i] > (li ? li->channels : h->channels)){
				warn("bad map[%d]=%d, skipping a channel", i, map[i]);
				memset(inrows[i], 0, rb); // zero out the row
			}else
				readunpackrow(psd, chcomp, rowpos, ch, j, rb, inrows[i], rledata);
		}

		if(chancount > 1){ /* interleave channels */
			if(h->depth == 8)
				for(i = 0, p = rowbuf; i < rb; ++i)
					for(ch = 0; ch < chancount; ++ch)
						*p++ = inrows[ch][i];
			else
				for(i = 0, q = (uint16_t*)rowbuf; i < rb/2; ++i)
					for(ch = 0; ch < chancount; ++ch)
						*q++ = ((uint16_t*)inrows[ch])[i];

			png_write_row(png_ptr, rowbuf);
		}else
			png_write_row(png_ptr, inrows[0]);
	}
	
	png_write_end(png_ptr, NULL /*info_ptr*/);

err:
	fclose(png);
	free(rowbuf);
	free(rledata);
	for(ch = 0; ch < chancount; ++ch)
		free(inrows[ch]);

	png_destroy_write_struct(&png_ptr, &info_ptr);
}

