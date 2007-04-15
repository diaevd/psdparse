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

extern int verbose, quiet, rsrc, extra, makedirs, numbered, mergedalpha,
		   help, split, nwarns, writepng, writelist, writexml;
extern char *pngdir;

char indir[PATH_MAX], dirsep[] = {DIRSEP,0};
FILE *listfile = NULL, *xml = NULL;

void skipblock(psd_file_t f, char *desc){
	psd_bytes_t n = get4B(f); // correct for PSB???
	if(n){
		fseeko(f, n, SEEK_CUR);
		VERBOSE("  ...skipped %s (" LL_L("%lld","%ld") " bytes)\n", desc, n);
	}else
		VERBOSE("  (%s is empty)\n",desc);
}

static void writechannels(psd_file_t f, char *dir, char *name, int chcomp[], 
						  struct layer_info *li, psd_bytes_t **rowpos, int startchan, 
						  int channels, long rows, long cols, struct psd_header *h)
{
	FILE *png;
	char pngname[FILENAME_MAX];
	int i,ch;

	for(i = 0; i < channels; ++i){
		// build PNG file name
		strcpy(pngname,name);
		ch = li ? li->chid[startchan + i] : startchan + i;
		if(ch == -2){
			if(xml){
				fprintf(xml,"\t\t<LAYERMASK TOP='%ld' LEFT='%ld' BOTTOM='%ld' RIGHT='%ld' ROWS='%ld' COLUMNS='%ld' DEFAULTCOLOR='%d'>\n",
						li->mask.top, li->mask.left, li->mask.bottom, li->mask.right, li->mask.rows, li->mask.cols, li->mask.default_colour);
				if(li->mask.flags & 1) fprintf(xml,"\t\t\t<POSITIONRELATIVE />\n");
				if(li->mask.flags & 2) fprintf(xml,"\t\t\t<DISABLED />\n");
				if(li->mask.flags & 4) fprintf(xml,"\t\t\t<INVERT />\n");
			}
			strcat(pngname,".lmask");
			// layer mask channel is a special case, gets its own dimensions
			rows = li->mask.rows;
			cols = li->mask.cols;
		}else if(ch == -1){
			if(xml) fputs("\t\t<TRANSPARENCY>\n",xml);
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
			if(xml) fputs("\t\t</LAYERMASK>\n",xml);
		}else if(ch == -1){
			if(xml) fputs("\t\t</TRANSPARENCY>\n",xml);
		}
	}
}

void doimage(psd_file_t f, struct layer_info *li, char *name,
			 int channels, psd_pixels_t rows, psd_pixels_t cols, struct psd_header *h)
{
	FILE *png;
	int ch, comp, startchan, pngchan, color_type,
		*chcomp = checkmalloc(sizeof(int)*channels);
	psd_bytes_t **rowpos = checkmalloc(sizeof(psd_bytes_t*)*channels);

	for(ch = 0; ch < channels; ++ch){
		// is it a layer mask? if so, use special case row count
		long chrows = li && li->chid[ch] == -2 ? li->mask.rows : rows;
		rowpos[ch] = checkmalloc(sizeof(psd_bytes_t)*(chrows+1));
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
		
		comp = dochannel(f,NULL,0/*no index*/,channels,rows,cols,h->depth,rowpos,h);
		for(ch = 0; ch < channels; ++ch) 
			chcomp[ch] = comp; /* merged channels share same compression type */
		
		if(xml)
			fprintf(xml,"\t<COMPOSITE CHANNELS='%d' HEIGHT='%ld' WIDTH='%ld'>\n",
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
		if(xml) fputs("\t</COMPOSITE>\n",xml);
	}else{
		// Process layer:
		// for each channel, store its row pointers sequentially 
		// in the rowpos[] array, and its compression type in chcomp[] array
		// (pngwriteimage() will take care of interleaving this data for libpng)
		for(ch = 0; ch < channels; ++ch){
			VERBOSE("  channel %d:\n",ch);
			chcomp[ch] = dochannel(f,li,ch,1/*count*/,rows,cols,h->depth,rowpos+ch,h);
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

/**
 * Process the "layer and mask information" section. The file must be
 * positioned at the beginning of this section when dolayermaskinfo()
 * is called.
 * 
 * The function sets h->nlayers to the number of layers in the PSD,
 * and also populates the h->linfo[] array.
 */

void dolayermaskinfo(psd_file_t f, struct psd_header *h){
	psd_bytes_t layerlen, chlen, extralen, extrastart;
	int i, j, chid, namelen;
	char *chidstr, tmp[10];

	if( (h->lmilen = GETPSDBYTES(f)) ){
		h->lmistart = ftello(f);

		// process layer info section
		if( (layerlen = GETPSDBYTES(f)) ){
			// layers structure
			h->nlayers = get2B(f);
			if(h->nlayers < 0){
				h->nlayers = - h->nlayers;
				VERBOSE("  (first alpha is transparency for merged image)\n");
				mergedalpha = 1;
			}
			UNQUIET("\n%d layers:\n",h->nlayers);
			
			if( h->nlayers*(18+6*h->channels) > layerlen ){ // sanity check
				alwayswarn("### unlikely number of layers, giving up.\n");
				return;
			}
			
			h->linfo = checkmalloc(h->nlayers*sizeof(struct layer_info));

			// load h->linfo[] array with each layer's info

			for(i = 0; i < h->nlayers; ++i){
				// process layer record
				h->linfo[i].top = get4B(f);
				h->linfo[i].left = get4B(f);
				h->linfo[i].bottom = get4B(f);
				h->linfo[i].right = get4B(f);
				h->linfo[i].channels = get2Bu(f);
				
				VERBOSE("\n");
				UNQUIET("  layer %d: (%4ld,%4ld,%4ld,%4ld), %d channels (%4ld rows x %4ld cols)\n",
						i, h->linfo[i].top, h->linfo[i].left, h->linfo[i].bottom, h->linfo[i].right, h->linfo[i].channels,
						h->linfo[i].bottom-h->linfo[i].top, h->linfo[i].right-h->linfo[i].left);

				if( h->linfo[i].bottom < h->linfo[i].top || h->linfo[i].right < h->linfo[i].left
				 || h->linfo[i].channels > 64 ) // sanity check
				{
					alwayswarn("### something's not right about that, trying to skip layer.\n");
					fseeko(f, 6*h->linfo[i].channels+12, SEEK_CUR);
					skipblock(f,"layer info: extra data");
				}else{

					h->linfo[i].chlengths = checkmalloc(h->linfo[i].channels*sizeof(psd_bytes_t));
					h->linfo[i].chid = checkmalloc(h->linfo[i].channels*sizeof(int));
					h->linfo[i].chindex = checkmalloc((h->linfo[i].channels+2)*sizeof(int));
					h->linfo[i].chindex += 2; // so we can index array from [-2] (hackish)
					
					for(j = -2; j < h->linfo[i].channels; ++j)
						h->linfo[i].chindex[j] = -1;
		
					// fetch info on each of the layer's channels
					
					for(j = 0; j < h->linfo[i].channels; ++j){
						chid = h->linfo[i].chid[j] = get2B(f);
						chlen = h->linfo[i].chlengths[j] = GETPSDBYTES(f);
						
						if(chid >= -2 && chid < h->linfo[i].channels)
							h->linfo[i].chindex[chid] = j;
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
						VERBOSE("    channel %2d: " LL_L("%7lld","%7ld") " bytes, id=%2d %s\n",
								j, chlen, chid, chidstr);
					}

					fread(h->linfo[i].blend.sig,1,4,f);
					fread(h->linfo[i].blend.key,1,4,f);
					h->linfo[i].blend.opacity = fgetc(f);
					h->linfo[i].blend.clipping = fgetc(f);
					h->linfo[i].blend.flags = fgetc(f);
					fgetc(f); // padding
					layerblendmode(f, 0, 0, &h->linfo[i].blend);

					// process layer's 'extra data' section

					extralen = get4B(f);
					extrastart = ftello(f);
					VERBOSE("  (extra data: " LL_L("%lld","%ld") " bytes @ "
							LL_L("%lld","%ld") ")\n", extralen, extrastart);

					// fetch layer mask data
					if( (h->linfo[i].mask.size = get4B(f)) ){
						h->linfo[i].mask.top = get4B(f);
						h->linfo[i].mask.left = get4B(f);
						h->linfo[i].mask.bottom = get4B(f);
						h->linfo[i].mask.right = get4B(f);
						h->linfo[i].mask.default_colour = fgetc(f);
						h->linfo[i].mask.flags = fgetc(f);
						fseeko(f, h->linfo[i].mask.size-18, SEEK_CUR); // skip remainder
						h->linfo[i].mask.rows = h->linfo[i].mask.bottom - h->linfo[i].mask.top;
						h->linfo[i].mask.cols = h->linfo[i].mask.right - h->linfo[i].mask.left;
					}else
						VERBOSE("  (no layer mask)\n");
			
					skipblock(f,"layer blending ranges");
					
					// layer name
					h->linfo[i].nameno = malloc(16);
					sprintf(h->linfo[i].nameno,"layer%d",i+1);
					namelen = fgetc(f);
					h->linfo[i].name = checkmalloc(PAD4(namelen+1));
					fread(h->linfo[i].name,1,PAD4(namelen+1)-1,f);
					h->linfo[i].name[namelen] = 0;
					if(namelen){
						UNQUIET("    name: \"%s\"\n",h->linfo[i].name);
						if(h->linfo[i].name[0] == '.')
							h->linfo[i].name[0] = '_';
					}
					
					// process layer's 'additional info'
					
					h->linfo[i].additionalpos = ftello(f);
					h->linfo[i].additionallen = extrastart + extralen - h->linfo[i].additionalpos;
					if(extra)
						doadditional(f, 0, h->linfo[i].additionallen, 0);
			
					// leave file positioned at 'image data' section
					fseeko(f, extrastart+extralen, SEEK_SET);
				}
			} // for layers
      
		}else VERBOSE("  (layer info section is empty)\n");
		
	}else VERBOSE("  (layer & mask info section is empty)\n");
}

/**
 * Loop over all layers described by layer info section,
 * spit out a line in asset list if requested, and call
 * doimage() to process its image data.
 */
	
void processlayers(psd_file_t f, struct psd_header *h){
	int i;
	psd_bytes_t savepos;

	if(listfile) fputs("assetlist = {\n",listfile);
		
	for(i = 0; i < h->nlayers; ++i){
		long pixw = h->linfo[i].right - h->linfo[i].left,
			 pixh = h->linfo[i].bottom - h->linfo[i].top;

		VERBOSE("\n  layer %d (\"%s\"):\n",i,h->linfo[i].name);
	  
		if(listfile && pixw && pixh){
			if(numbered)
				fprintf(listfile,"\t\"%s\" = { pos={%4ld,%4ld}, size={%4ld,%4ld} }, -- %s\n",
						h->linfo[i].nameno, h->linfo[i].left, h->linfo[i].top, pixw, pixh, h->linfo[i].name);
			else
				fprintf(listfile,"\t\"%s\" = { pos={%4ld,%4ld}, size={%4ld,%4ld} },\n",
						h->linfo[i].name, h->linfo[i].left, h->linfo[i].top, pixw, pixh);
		}
		if(xml){
			fputs("\t<LAYER NAME='",xml);
			fputsxml(h->linfo[i].name,xml);
			fprintf(xml,"' TOP='%ld' LEFT='%ld' BOTTOM='%ld' RIGHT='%ld' WIDTH='%ld' HEIGHT='%ld'>\n",
					h->linfo[i].top, h->linfo[i].left, h->linfo[i].bottom, h->linfo[i].right, pixw, pixh);
		}

		if(xml) layerblendmode(f, 2, 1, &h->linfo[i].blend);

		doimage(f, h->linfo+i, numbered ? h->linfo[i].nameno : h->linfo[i].name,
				h->linfo[i].channels, pixh, pixw, h);

		if(extra){
			// Process 'additional data' (non-image layer data,
			// such as adjustments, effects, type tool).
			savepos = ftello(f);
			fseeko(f, h->linfo[i].additionalpos, SEEK_SET);
			doadditional(f, 2, h->linfo[i].additionallen, 1);
			fseeko(f, savepos, SEEK_SET); // restore file position
		}
		if(xml) fputs("\t</LAYER>\n\n",xml);
	}

	if(listfile) fputs("}\n",listfile);
}

int dopsd(psd_file_t f, char *psdpath){
	struct psd_header h;
	int result = 0;
	char *base,*ext,fname[PATH_MAX],*dirsuffix;
	psd_bytes_t k;
	
	// file header
	fread(h.sig,1,4,f);
	h.version = get2Bu(f);
	get4B(f); get2B(f); // reserved[6];
	h.channels = get2Bu(f);
	h.rows = get4B(f);
	h.cols = get4B(f);
	h.depth = get2Bu(f);
	h.mode = get2Bu(f);

	if(!feof(f) && !memcmp(h.sig,"8BPS",4)){
		if(h.version == 1
#ifdef PSBSUPPORT
				   || h.version == 2
#endif
		){
			strcpy(indir,psdpath);
			ext = strrchr(indir,'.');
			dirsuffix = h.depth < 32 ? "_png" : "_raw";
			ext ? strcpy(ext,dirsuffix) : strcat(indir,dirsuffix);

			if(writelist){
				setupfile(fname,pngdir,"list",".txt");
				listfile = fopen(fname,"w");
			}
			if(writexml){
				setupfile(fname,pngdir,"psd",".xml");
				if( (xml = fopen(fname,"w")) )
					fputs("<?xml version=\"1.0\"?>\n",xml);
			}

			if(listfile) fprintf(listfile,"-- PSD file: %s\n",psdpath);
			if(xml){
				fputs("<PSD FILE='",xml);
				fputsxml(psdpath,xml);
				fprintf(xml,"' VERSION='%d' CHANNELS='%d' ROWS='%ld' COLUMNS='%ld' DEPTH='%d' MODE='%d'",
						h.version,h.channels,h.rows,h.cols,h.depth,h.mode);
				if(h.mode >= 0 && h.mode < 16)
					fprintf(xml, " MODENAME='%s'", mode_names[h.mode]);
				fputs(">\n", xml);
			}
			UNQUIET("  PS%c (version %d), %d channels, %ld rows x %ld cols, %d bit %s\n",
					h.version == 1 ? 'D' : 'B', h.version, h.channels, h.rows, h.cols, h.depth,
					h.mode >= 0 && h.mode < 16 ? mode_names[h.mode] : "???");
			
			if(h.channels <= 0 || h.channels > 64 || h.rows <= 0 || 
				 h.cols <= 0 || h.depth < 0 || h.depth > 32 || h.mode < 0)
				alwayswarn("### something isn't right about that header, giving up now.\n");
			else{
				h.colormodepos = ftello(f);
				skipblock(f,"color mode data");

				if(rsrc)
					doimageresources(f);
				else
					skipblock(f,"image resources");

				dolayermaskinfo(f,&h);
				processlayers(f,&h);
				
				// process global layer mask info section
				skipblock(f,"global layer mask info");
		
				// global 'extra data' (not really documented)
				k = h.lmistart + h.lmilen - ftello(f);
				if(extra)
					doadditional(f, 1, k, 1);
				fseeko(f, h.lmistart + h.lmilen, SEEK_SET);

				// merged image data
				base = strrchr(psdpath,DIRSEP);
				doimage(f,NULL,base ? base+1 : psdpath,h.channels,h.rows,h.cols,&h);

				UNQUIET("  done.\n\n");
				result = 1;
			}
			if(xml) fputs("</PSD>\n",xml);
		}else
			alwayswarn("# \"%s\": version %d not supported\n", psdpath, h.version);
	}else
		alwayswarn("# \"%s\": couldn't read header, or is not a PSD/PSB\n", psdpath);

	if(listfile) fclose(listfile);
	if(xml) fclose(xml);
	return result;
}
