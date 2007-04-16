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
#include <string.h>

#include "psdparse.h"

/* This code could also be used as a template for other file types. */

FILE* rawsetupwrite(psd_file_t psd, char *dir, char *name, psd_pixels_t width, psd_pixels_t height, 
					int channels, int color_type, struct layer_info *li, struct psd_header *h)
{
	char rawname[PATH_MAX],txtname[PATH_MAX];
	FILE *f;

	f = NULL;
	
	if(width && height){
		// summarise metadata in a text file
		setupfile(txtname,dir,name,".txt");
		if( (f = fopen(txtname,"w")) ){
			fprintf(f,"# %s.raw\nmode = %d  # %s\ndepth = %d\n",
					name,h->mode,mode_names[h->mode],h->depth);
			if(li) fprintf(f,"layer = \"%s\"\n",li->name);
			fprintf(f,"width = %ld\nheight = %ld\nchannels = %d  # not interleaved\n",
					width,height,channels);
			fclose(f);
		}else alwayswarn("### can't open \"%s\" for writing\n",txtname);

		// now write the raw binary
		setupfile(rawname,dir,name,".raw");
		if( (f = fopen(rawname,"wb")) ){
			if(xml){
				fputs("\t\t<RAW NAME='",xml);
				fputsxml(name,xml);
				fputs("' DIR='",xml);
				fputsxml(dir,xml);
				fputs("' FILE='",xml);
				fputsxml(rawname,xml);
				fprintf(xml,"' ROWS='%ld' COLS='%ld' CHANNELS='%d' />\n",height,width,channels);
			}
			UNQUIET("# writing raw \"%s\"\n# metadata in \"%s\"\n",rawname,txtname);
		}else alwayswarn("### can't open \"%s\" for writing\n",rawname);

	}else alwayswarn("### skipping layer \"%s\" (%ldx%ld)\n",li->name,width,height);

	return f;
}

void rawwriteimage(FILE *png, psd_file_t psd, int chcomp[], struct layer_info *li, psd_bytes_t **rowpos,
				   int startchan, int chancount, psd_pixels_t rows, psd_pixels_t cols, struct psd_header *h)
{
	psd_pixels_t j, rb = (h->depth*cols+7)/8;
	unsigned char *inrow, *rledata;
	psd_bytes_t savepos = ftello(psd);
	int i;

	rledata = checkmalloc(2*rb);
	inrow = checkmalloc(rb);

	// write channels in a series of planes, not interleaved
	for(i = startchan; i < startchan+chancount; ++i){
		UNQUIET("## rawwriteimage: channel %d\n",i);
		for(j = 0; j < rows; ++j){
			/* get row data */
			readunpackrow(psd, chcomp, rowpos, i, j, rb, inrow, rledata);
			if((psd_pixels_t)fwrite(inrow, 1, rb, png) != rb){
				alwayswarn("# error writing raw data, aborting\n");
				goto done;
			}
		}

	}

done:
	fclose(png);
	free(rledata);
	free(inrow);

	fseeko(psd, savepos, SEEK_SET);
	VERBOSE(">>> restoring filepos= " LL_L("%lld\n","%ld\n"), savepos);
}

