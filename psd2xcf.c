/*
    This file is part of "psdparse"
    Copyright (C) 2004-2010 Toby Thain, toby@telegraphics.com.au

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

#include "psdparse.h"
#include "xcf.h"
#include "version.h"

/* This program outputs a GIMP XCF file converted from PSD layer data.
 *
 * build:
 *     make psd2xcf -f Makefile.unix
 */

// These flags control psdparse behaviour.
// You WILL get text output unless you set quiet = 1 !
int verbose = 0, quiet = 0, rsrc = 1, resdump = 0, extra = 0,
	makedirs = 0, numbered = 0, help = 0, split = 0, xmlout = 0,
	writepng = 0, writelist = 0, writexml = 0, unicode_filenames = 1,
	use_merged = 0;
long hres, vres; // set by doresources()
char *pngdir;
off_t xcf_merged_pos; // updated by doimage() if merged image is processed

static FILE *xcf;
static int xcf_compr = 1; // RLE

void usage(char *prog, int status){
	fprintf(stderr, "usage: %s [options] psdfile...\n\
  -h, --help         show this help\n\
  -V, --version      show version\n\
  -v, --verbose      print more information\n\
  -q, --quiet        work silently\n\
  -c, --rle          RLE compression (default)\n\
  -u, --raw          no compression\n\
  -m, --merged       include merged (composite) image, if available\n", prog);
	exit(status);
}

int main(int argc, char *argv[]){
	static struct option longopts[] = {
		{"help",       no_argument, &help, 1},
		{"version",    no_argument, NULL, 'V'},
		{"verbose",    no_argument, &verbose, 1},
		{"quiet",      no_argument, &quiet, 1},
		{"rle",        no_argument, &xcf_compr, 1},
		{"raw",        no_argument, &xcf_compr, 0},
		{"merged",     no_argument, &use_merged, 1},
		{NULL,0,NULL,0}
	};
	FILE *f;
	struct psd_header h;
	int arg, i, indexptr, opt;
	off_t xcf_layers_pos;

	while( (opt = getopt_long(argc, argv, "hVvqcum", longopts, &indexptr)) != -1 )
		switch(opt){
		case 0: break; // long option
		case 'h': help = 1; break;
		case 'V':
			printf("psd2xcf version " VERSION_STR
				   "\nCopyright (C) 2004-2010 Toby Thain <toby@telegraphics.com.au>\n");
			return EXIT_SUCCESS;
		case 'v': verbose = 1; break;
		case 'q': quiet = 1; break;
		case 'c': xcf_compr = 1; break;
		case 'u': xcf_compr = 0; break;
		case 'm': use_merged = 1; break;
		default:  usage(argv[0], EXIT_FAILURE);
		}

	if(optind >= argc)
		usage(argv[0], EXIT_FAILURE);
	else if(help)
		usage(argv[0], EXIT_SUCCESS);

	for(arg = optind; arg < argc; ++arg){
		if( (f = fopen(argv[arg], "rb")) ){
			h.version = h.nlayers = 0;
			h.layerdatapos = 0;

			if(dopsd(f, argv[arg], &h)){
				if(h.nlayers == 0){
					alwayswarn("# File has no layers. Using merged image.\n");
					use_merged = 1;
				}
				if( (xcf = xcf_open(argv[arg], &h)) ){
					// xcf_open() has written the XCF header.

					// properties...
					xcf_prop_compression(xcf, xcf_compr);
					// image resolution in pixels per cm
					xcf_prop_resolution(xcf, FIXEDPT(hres)/2.54, FIXEDPT(vres)/2.54);
					if(h.mode == ModeIndexedColor) // copy palette from psd to xcf
						xcf_prop_colormap(xcf, f, &h);
					xcf_prop_end(xcf);

					// layer pointers... write dummies now, fixup later.
					// Ignore zero-sized layers.
					xcf_layers_pos = ftello(xcf);

					if(use_merged)
						put4xcf(xcf, 0); // slot for merged image layer

					for(i = h.nlayers; i--;)
						if(h.linfo[i].right > h.linfo[i].left
						&& h.linfo[i].bottom > h.linfo[i].top)
							put4xcf(xcf, 0);
					put4xcf(xcf, 0); // end of layer pointers

					// channel pointers here... is this the background image??
					put4xcf(xcf, 0); // end of channel pointers

					// process the layers in 'image data' section.
					// this will, in turn, call doimage() for each layer.
					fseeko(f, h.layerdatapos, SEEK_SET);
					processlayers(f, &h);

					if(use_merged){
						// position file after 'layer & mask info', i.e. at the
						// beginning of the merged image data.
						fseeko(f, h.lmistart + h.lmilen, SEEK_SET);

						// process merged (composite) image data
						xcf_merged_pos = 0;
						doimage(f, NULL, NULL, &h);
					}

					// Update the layer pointers. We do this in reverse
					// of the PSD order, since XCF stores layers top to bottom.
					fseeko(xcf, xcf_layers_pos, SEEK_SET);
					UNQUIET("xcf layer offset fixup (top to bottom):\n");

					if(use_merged)
						put4xcf(xcf, xcf_merged_pos);

					for(i = h.nlayers-1; i >= 0; --i){
						if(h.linfo[i].right > h.linfo[i].left
						   && h.linfo[i].bottom > h.linfo[i].top)
						{
							UNQUIET("  layer %3d xcf @ %7lld  \"%s\"\n",
									i, h.linfo[i].xcf_pos, h.linfo[i].unicode_name);
							put4xcf(xcf, h.linfo[i].xcf_pos);
						}
						else{
							UNQUIET("  layer %3d       skipped  \"%s\"\n",
									i, h.linfo[i].unicode_name);
						}
					}

					fclose(xcf);
				}
				else{
					fatal("could not open xcf file for writing\n");
				}
			}else{
				fprintf(stderr, "Not a PSD or PSB file.\n");
			}

			fclose(f);
		}else{
			fprintf(stderr, "Could not open: %s\n", argv[arg]);
		}
	}

	return EXIT_SUCCESS;
}

/* This function is a callback from processlayers(). It needs the
 * input file positioned at the beginning of the image data
 * for the layer, or merged (flattened) image data, whichever is being
 * processed. */

void doimage(psd_file_t f, struct layer_info *li, char *name, struct psd_header *h)
{
	int ch;
	psd_bytes_t image_data_end;

	/* li points to layer information. If it is NULL, then
	 * the merged image is being being processed, not a layer. */

	if(li){
		// Process layer, described by struct layer_info pointed to by li

		// The following members of this struct may be useful:
		//   top, left, bottom, right       - position/size of layer in document
		//     (the layer may lie partly or wholly outside the document bounds,
		//      as defined by PSD header)
		//   channels                       - channel count
		//   struct channel_info *chan      - array of channel_info
		//   struct blend_mode_info blend   - blending information
		//   struct layer_mask_info mask    - layer mask info
		//   char *name                     - layer name

		UNQUIET("layer \"%s\"\n", li->name);
		for(ch = 0; ch < li->channels; ++ch){
			// dochannel() initialises the li->chan[ch] struct, including:
			//   id                    - channel id
			//   comptype              - channel's compression type
			//   rows, cols, rowbytes  - set by dochannel()
			//   length                - channel byte count in file
			// how to find image data, depending on compression type:
			//   rawpos                - file offset of RAW channel data (AFTER compression type)
			//   rowpos                - row data file positions (RLE ONLY)
			//   unzipdata             - uncompressed data (ZIP ONLY)

			dochannel(f, li, li->chan + ch, 1, h);
			UNQUIET("  channel %d  id=%2d  %4ld rows x %4ld cols  %6lld bytes\n",
				   ch, li->chan[ch].id, li->chan[ch].rows, li->chan[ch].cols,
				   li->chan[ch].length);
		}

		image_data_end = ftello(f);
		VERBOSE("## layer image data end @ %lld\n", image_data_end);

		// xcf_layer() does alter the input PSD file position!
		li->xcf_pos = li->right > li->left && li->bottom > li->top
							? xcf_layer(xcf, f, li, xcf_compr)
							: 0;

		// caller may be assuming this position
		fseeko(f, image_data_end, SEEK_SET);
	}
	else{
		// The merged image has the size, mode, depth, and channel count
		// given by the main PSD header (h).

		struct channel_info *merged_chans = checkmalloc(h->channels*sizeof(struct channel_info));
		struct layer_info mli;

		// The 'merged' or 'composite' image is where the flattened image is stored
		// when 'Maximise Compatibility' is used.
		// It consists of:
		// - the alpha channel for merged image (if mergedalpha is TRUE)
		// - the merged image (1 or 3 channels)
		// - any remaining alpha or spot channels.

		UNQUIET("\nmerged channels:\n");

		dochannel(f, NULL, merged_chans, h->channels, h);

		for(ch = 0; ch < h->channels; ++ch){
			UNQUIET("  channel %d  id=%2d  %4ld rows x %4ld cols\n",
				   ch, merged_chans[ch].id, merged_chans[ch].rows, merged_chans[ch].cols);
		}

		mli.top = mli.left = 0;
		mli.bottom = h->rows;
		mli.right = h->cols;
		mli.channels = h->channels;

		mli.chan = merged_chans;
		mli.chindex = checkmalloc((h->channels+2)*sizeof(int));
		mli.chindex += 2;

		// map channel ids to channel indexes
		for(ch = -2; ch < h->channels; ++ch)
			mli.chindex[ch] = -1;
		for(ch = 0; ch < h->channels; ++ch){
			int chid = merged_chans[ch].id;
			if(chid >= -2 && chid < h->channels){
				mli.chindex[chid] = ch;
			}
			else{
				warn_msg("unexpected channel id %d in merged image", chid);
			}
		}

		memcpy(mli.blend.key, "norm", 4); // normal blend mode
		mli.blend.opacity = 255;
		mli.blend.clipping = 0;
		mli.blend.flags = h->nlayers ? 2 : 0; // if other layers, then hide merged image
		mli.mask.size = 0; // no layer mask
		mli.name = mli. unicode_name = "Photoshop merged image";

		xcf_merged_pos = xcf_layer(xcf, f, &mli, xcf_compr);

		free(merged_chans);
	}
}
