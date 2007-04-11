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

FILE* rawsetupwrite(FILE *psd, char *dir, char *name, psd_rle_t width, psd_rle_t height, 
					int channels, int color_type, struct layer_info *li, struct psd_header *h){
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
			fprintf(f,"width = %d\nheight = %d\nchannels = %d  # not interleaved\n",
					width,height,channels);
			fclose(f);
		}else alwayswarn("### can't open \"%s\" for writing\n",txtname);

		// now write the raw binary
		setupfile(rawname,dir,name,".raw");
		if( (f = fopen(rawname,"wb")) ){
			if(xml){
				fputs("\t\t<RAW NAME='",xml);
				fputsxml(name,xml);
				fputc('\'',xml);
				fputs(" DIR='",xml);
				fputsxml(dir,xml);
				fputc('\'',xml);
				fputs(" FILE='",xml);
				fputsxml(rawname,xml);
				fputc('\'',xml);
				fprintf(xml," ROWS='%d' COLS='%d' CHANNELS='%d' />\n",height,width,channels);
			}
			UNQUIET("# writing raw \"%s\"\n# metadata in \"%s\"\n",rawname,txtname);
		}else alwayswarn("### can't open \"%s\" for writing\n",rawname);

	}else alwayswarn("### skipping layer \"%s\" (%dx%d)\n",li->name,width,height);

	return f;
}

void rawwriteimage(FILE *png, FILE *psd, int chcomp[], struct layer_info *li, psd_size_t **rowpos,
				   int startchan, int chancount, psd_rle_t rows, psd_rle_t cols, struct psd_header *h){
	psd_rle_t j,n,rb = (h->depth*cols+7)/8,rlebytes;
	unsigned char *rowbuf,*inrow,*rledata;
	long savepos = ftell(psd);
	int i;

	rowbuf = checkmalloc(rb*chancount);
	rledata = checkmalloc(2*rb);
	inrow = checkmalloc(rb);

	// write channels in a series of planes, not interleaved
	for(i = startchan; i < startchan+chancount; ++i){
		UNQUIET("## rawwriteimage: channel %d\n",i);
		for(j = 0; j < rows; ++j){
			/* get row data */
			//printf("rowpos[%d][%4d] = %7d\n",ch,j,rowpos[ch][j]);

			if(fseek(psd,rowpos[i][j],SEEK_SET) == -1){
				alwayswarn("# error seeking to %ld\n",rowpos[i][j]);
				memset(inrow,0,rb); // zero out the row
			}else{

				if(chcomp[i] == RAWDATA){ /* uncompressed row */
					n = fread(inrow,1,rb,psd);
					if(n != rb){
						warn("error reading row data (raw) @ %ld",rowpos[i][j]);
						memset(inrow+n,0,rb-n); // zero out the rest of the row
					}
				}
				else if(chcomp[i] == RLECOMP){ /* RLE compressed row */
					n = rowpos[i][j+1] - rowpos[i][j];
					if(n > 2*rb){
						n = 2*rb; // sanity check
						warn("bad RLE count %5d @ channel %2d, row %5d",n,i,j);
					}
					rlebytes = fread(rledata,1,n,psd);
					if(rlebytes < n){
						warn("error reading row data (RLE) @ %ld",rowpos[i][j]);
						memset(inrow,0,rb); // zero it out, will probably unpack short
					}
					unpackbits(inrow,rledata,rb,rlebytes);
				}else // assume it is bad
					memset(inrow,0,rb);

			}
			if(fwrite(inrow, 1, rb, png) != rb){
				alwayswarn("# error writing raw data, aborting\n");
				goto done;
			}
		}

	}

done:
	fclose(png);
	free(rowbuf);
	free(rledata);
	free(inrow);

	fseek(psd,savepos,SEEK_SET); 
	VERBOSE(">>> restoring filepos= %ld\n",savepos);
}

