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

#include "psdparse.h"

#include "png.h"

static void writeimage(psd_file_t psd, char *dir, char *name,
					   struct layer_info *li,
					   struct channel_info *chan,
					   int channels, long rows, long cols,
					   struct psd_header *h, int color_type)
{
	FILE *outfile;

	if(writepng){
		if(h->depth == 32){
			if((outfile = rawsetupwrite(psd, dir, name, cols, rows, channels, color_type, li, h)))
				rawwriteimage(outfile, psd, li, chan, channels, h);
		}else{
			if((outfile = pngsetupwrite(psd, dir, name, cols, rows, channels, color_type, li, h)))
				pngwriteimage(outfile, psd, li, chan, channels, h);
		}
	}
}

static void writechannels(psd_file_t f, char *dir, char *name,
						  struct layer_info *li,
						  struct channel_info *chan,
						  int channels, struct psd_header *h)
{
	char pngname[FILENAME_MAX];
	int ch;

	for(ch = 0; ch < channels; ++ch){
		// build PNG file name
		strcpy(pngname, name);

		if(chan[ch].id == LMASK_CHAN_ID){
			if(xml){
				fprintf(xml, "\t\t<LAYERMASK TOP='%ld' LEFT='%ld' BOTTOM='%ld' RIGHT='%ld' ROWS='%ld' COLUMNS='%ld' DEFAULTCOLOR='%d'>\n",
						li->mask.top, li->mask.left, li->mask.bottom, li->mask.right, li->mask.rows, li->mask.cols, li->mask.default_colour);
				if(li->mask.flags & 1) fputs("\t\t\t<POSITIONRELATIVE />\n", xml);
				if(li->mask.flags & 2) fputs("\t\t\t<DISABLED />\n", xml);
				if(li->mask.flags & 4) fputs("\t\t\t<INVERT />\n", xml);
			}
			strcat(pngname, ".lmask");
		}else if(chan[ch].id == TRANS_CHAN_ID){
			if(xml) fputs("\t\t<TRANSPARENCY>\n", xml);
			strcat(pngname, li ? ".trans" : ".alpha");
		}else{
			if(xml)
				fprintf(xml, "\t\t<CHANNEL ID='%d'>\n", chan[ch].id);
			if(chan[ch].id < (int)strlen(channelsuffixes[h->mode])) // can identify channel by letter
				sprintf(pngname+strlen(pngname), ".%c", channelsuffixes[h->mode][chan[ch].id]);
			else // give up and use a number
				sprintf(pngname+strlen(pngname), ".%d", chan[ch].id);
		}

		if(chan[ch].comptype == -1)
			alwayswarn("## not writing \"%s\", bad channel compression type\n", pngname);
		else
			writeimage(f, dir, pngname, li, chan + ch, 1,
					   chan[ch].rows, chan[ch].cols, h, PNG_COLOR_TYPE_GRAY);

		if(chan[ch].id == LMASK_CHAN_ID){
			if(xml) fputs("\t\t</LAYERMASK>\n", xml);
		}else if(chan[ch].id == TRANS_CHAN_ID){
			if(xml) fputs("\t\t</TRANSPARENCY>\n", xml);
		}else{
			if(xml) fputs("\t\t</CHANNEL>\n", xml);
		}
	}
}

void doimage(psd_file_t f, struct layer_info *li, char *name, struct psd_header *h)
{
	// map channel count to a suitable PNG mode (when scavenging and actual mode is not known)
	static int png_mode[] = {0, PNG_COLOR_TYPE_GRAY, PNG_COLOR_TYPE_GRAY_ALPHA,
								PNG_COLOR_TYPE_RGB,  PNG_COLOR_TYPE_RGB_ALPHA};
	int ch, pngchan, color_type, channels = li ? li->channels : h->channels;
	psd_bytes_t image_data_end;

	pngchan = color_type = 0;
	switch(h->mode){
	default: // multichannel, cmyk, lab etc
		split = 1;
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
	case SCAVENGE_MODE:
		pngchan = channels;
		color_type = png_mode[pngchan];
		break;
	}

	if(li){
		// Process layer

		for(ch = 0; ch < channels; ++ch){
			VERBOSE("  channel %d:\n", ch);
			dochannel(f, li, li->chan + ch, 1/*count*/, h);
		}

		image_data_end = ftello(f);

		if(writepng){
			nwarns = 0;
			if(pngchan && !split){
				writeimage(f, pngdir, name, li, li->chan,
						   h->depth == 32 ? channels : pngchan,
						   li->bottom - li->top, li->right - li->left,
						   h, color_type);

				if(h->depth < 32){
					// spit out any 'extra' channels (e.g. layer transparency)
					for(ch = 0; ch < channels; ++ch)
						if(li->chan[ch].id < -1 || li->chan[ch].id >= pngchan)
							writechannels(f, pngdir, name, li, li->chan + ch, 1, h);
				}
			}else{
				UNQUIET("# writing layer as split channels...\n");
				writechannels(f, pngdir, name, li, li->chan, channels, h);
			}
		}
	}else{
		struct channel_info *merged_chans = checkmalloc(channels*sizeof(struct channel_info));

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

		VERBOSE("\n  merged channels:\n");
		dochannel(f, NULL, merged_chans, channels, h);

		image_data_end = ftello(f);

		if(xml)
			fprintf(xml, "\t<COMPOSITE CHANNELS='%d' HEIGHT='%ld' WIDTH='%ld'>\n",
					channels, h->rows, h->cols);

		nwarns = 0;
		ch = 0;
		if(pngchan && !split){
			writeimage(f, pngdir, name, NULL, merged_chans,
					   h->depth == 32 ? channels : pngchan,
					   h->rows, h->cols, h, color_type);
			ch += pngchan;
		}
		if(writepng && ch < channels){
			if(split){
				UNQUIET("# writing %s image as split channels...\n", mode_names[h->mode]);
			}else{
				UNQUIET("# writing %d extra channels...\n", channels - ch);
			}

			writechannels(f, pngdir, name, NULL, merged_chans + ch, channels - ch, h);
		}

		if(xml) fputs("\t</COMPOSITE>\n", xml);

		free(merged_chans);
	}

	// caller may expect this file position
	fseeko(f, image_data_end, SEEK_SET);
}
