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

#include "psdparse.h"

#include "png.h"

static void writeimage(psd_file_t psd, char *dir, char *name, int chcomp[], 
					   struct layer_info *li, psd_bytes_t **rowpos,
					   int startchan, int channels, long rows, long cols,
					   struct psd_header *h, int color_type)
{
	psd_bytes_t savepos = ftello(psd);
	FILE *outfile;

	if(h->depth == 32){
		if((outfile = rawsetupwrite(psd, dir, name, cols, rows, channels, color_type, li, h)))
			rawwriteimage(outfile, psd, chcomp, li, rowpos, startchan, channels, rows, cols, h);
	}else{
		if((outfile = pngsetupwrite(psd, dir, name, cols, rows, channels, color_type, li, h)))
			pngwriteimage(outfile, psd, chcomp, li, rowpos, startchan, channels, rows, cols, h);
	}

	fseeko(psd, savepos, SEEK_SET);
	VERBOSE(">>> restoring filepos= " LL_L("%lld\n","%ld\n"), savepos);
}

static void writechannels(psd_file_t f, char *dir, char *name, int chcomp[], 
						  struct layer_info *li, psd_bytes_t **rowpos, int startchan, 
						  int channels, long rows, long cols, struct psd_header *h)
{
	char pngname[FILENAME_MAX];
	int i, ch;

	for(i = 0; i < channels; ++i){
		// build PNG file name
		strcpy(pngname, name);
		ch = li ? li->chid[startchan + i] : startchan + i;
		if(ch == -2){
			if(xml){
				fprintf(xml, "\t\t<LAYERMASK TOP='%ld' LEFT='%ld' BOTTOM='%ld' RIGHT='%ld' ROWS='%ld' COLUMNS='%ld' DEFAULTCOLOR='%d'>\n",
						li->mask.top, li->mask.left, li->mask.bottom, li->mask.right, li->mask.rows, li->mask.cols, li->mask.default_colour);
				if(li->mask.flags & 1) fputs("\t\t\t<POSITIONRELATIVE />\n", xml);
				if(li->mask.flags & 2) fputs("\t\t\t<DISABLED />\n", xml);
				if(li->mask.flags & 4) fputs("\t\t\t<INVERT />\n", xml);
			}
			strcat(pngname, ".lmask");
			// layer mask channel is a special case, gets its own dimensions
			rows = li->mask.rows;
			cols = li->mask.cols;
		}else if(ch == -1){
			if(xml) fputs("\t\t<TRANSPARENCY>\n", xml);
			strcat(pngname, li ? ".trans" : ".alpha");
		}else if(ch < (int)strlen(channelsuffixes[h->mode])) // can identify channel by letter
			sprintf(pngname+strlen(pngname), ".%c", channelsuffixes[h->mode][ch]);
		else // give up and use a number
			sprintf(pngname+strlen(pngname), ".%d", ch);
			
		if(chcomp[i] == -1)
			alwayswarn("## not writing \"%s\", bad channel compression type\n", pngname);
		else
			writeimage(f, dir, pngname, chcomp, li, rowpos, startchan+i, 1, rows, cols, h, PNG_COLOR_TYPE_GRAY);

		if(ch == -2){
			if(xml) fputs("\t\t</LAYERMASK>\n", xml);
		}else if(ch == -1){
			if(xml) fputs("\t\t</TRANSPARENCY>\n", xml);
		}
	}
}

void doimage(psd_file_t f, struct layer_info *li, char *name,
			 int channels, psd_pixels_t rows, psd_pixels_t cols, struct psd_header *h)
{
	int ch, comp, startchan, pngchan, color_type, *chcomp;
	psd_bytes_t **rowpos;

	chcomp = checkmalloc(channels*sizeof(int));
	rowpos = checkmalloc(channels*sizeof(psd_bytes_t*));
	
	for(ch = 0; ch < channels; ++ch){
		// is it a layer mask? if so, use special case row count
		psd_pixels_t chrows = li && li->chid[ch] == -2 ? li->mask.rows : rows;
		rowpos[ch] = checkmalloc((chrows+1)*sizeof(psd_bytes_t));
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
		if( li ? li->chindex[-1] != -1 : channels > 1 && h->mergedalpha ){
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
		if( li ? li->chindex[-1] != -1 : channels > 3 && h->mergedalpha ){
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
		
		comp = dochannel(f, NULL, 0/*no index*/, channels, rows, cols, h->depth, rowpos, h);
		for(ch = 0; ch < channels; ++ch) 
			chcomp[ch] = comp; /* merged channels share same compression type */
		
		if(xml)
			fprintf(xml, "\t<COMPOSITE CHANNELS='%d' HEIGHT='%ld' WIDTH='%ld'>\n",
					channels, rows, cols);
		if(writepng){
			nwarns = 0;
			startchan = 0;
			if(pngchan && !split){
				writeimage(f, pngdir, name, chcomp, NULL, rowpos, 0,
						   h->depth == 32 ? channels : pngchan, rows, cols, h, color_type);
				startchan += pngchan;
			}
			if(startchan < channels){
				if(!pngchan)
					UNQUIET("# writing %s image as split channels...\n", mode_names[h->mode]);
				writechannels(f, pngdir, name, chcomp, NULL, rowpos, 
							  startchan, channels-startchan, rows, cols, h);
			}
		}
		if(xml) fputs("\t</COMPOSITE>\n", xml);
	}else{
		// Process layer:
		// for each channel, store its row pointers sequentially 
		// in the rowpos[] array, and its compression type in chcomp[] array
		// (pngwriteimage() will take care of interleaving this data for libpng)
		for(ch = 0; ch < channels; ++ch){
			VERBOSE("  channel %d:\n", ch);
			chcomp[ch] = dochannel(f, li, ch, 1/*count*/, rows, cols, h->depth, rowpos+ch, h);
		}
		if(writepng){
			nwarns = 0;
			if(pngchan && !split){
				writeimage(f, pngdir, name, chcomp, li, rowpos, 0,
						   h->depth == 32 ? channels : pngchan, rows, cols, h, color_type);

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
