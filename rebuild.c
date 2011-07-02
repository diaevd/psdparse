/*
    This file is part of "psdparse"
    Copyright (C) 2004-2011 Toby Thain, toby@telegraphics.com.au

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

FILE *rebuilt_psd;

void writeheader(psd_file_t out_psd, int version, struct psd_header *h){
	fwrite("8BPS", 1, 4, out_psd);
	put2B(out_psd, version);
	put4B(out_psd, PAD_BYTE);
	put2B(out_psd, PAD_BYTE);
	put2B(out_psd, h->channels);
	put4B(out_psd, h->rows);
	put4B(out_psd, h->cols);
	put2B(out_psd, h->depth);
	put2B(out_psd, h->mode);
}

psd_bytes_t writepsdchannels(
		FILE *out_psd,
		int version,
		psd_file_t psd,
		int chindex,
		struct channel_info *ch,
		int chancount,
		struct psd_header *h)
{
	psd_pixels_t j, k, total_rows = chancount * ch->rows;
	psd_bytes_t *rowcounts;
	unsigned char *compbuf, *inrow, *rlebuf, *p;
	int i, comp;
	psd_bytes_t chansize, compsize;
	extern const char *comptype[];

	rlebuf    = checkmalloc(ch->rowbytes*2);
	inrow     = checkmalloc(ch->rowbytes);
	compbuf   = checkmalloc(PACKBITSWORST(ch->rowbytes)*total_rows);
	rowcounts = checkmalloc(sizeof(psd_bytes_t)*total_rows);

	// compress the channel(s) to decide between raw & RLE adaptively
	p = compbuf;
	compsize = 0;
	for(i = k = 0; i < chancount; ++i){
		for(j = 0; j < ch[i].rows; ++j, ++k){
			/* get row data */
			readunpackrow(psd, ch+i, j, inrow, rlebuf);
			rowcounts[k] = packbits(inrow, p, ch[i].rowbytes);
			compsize += rowcounts[k];
			p += rowcounts[k];
			//printf("packed ch %u row %u [%u] (rb %u) to %u bytes\n",
			//		 i, (unsigned)j, (unsigned)k, (unsigned)ch[i].rowbytes, (unsigned)rowcounts[k]);
		}
	}
	// allow for row counts:
	chansize = PSDBSIZE(version)*total_rows + compsize;

	if(chansize < total_rows*ch->rowbytes){
		// There was a saving using RLE, so use compressed data.

		put2B(out_psd, comp = RLECOMP); // compression type: raw image data
		chansize += 2;
		for(j = 0; j < total_rows; ++j){
			if(version == 1){
				if(rowcounts[j] > UINT16_MAX)
					fatal("## row count out of range for PSD (v1) format. Try without --rebuildpsd.\n");
				put2B(out_psd, rowcounts[j]);
			}else{
				put4B(out_psd, rowcounts[j]);
			}
		}

		if((psd_pixels_t)fwrite(compbuf, 1, compsize, out_psd) != compsize){
			alwayswarn("# error writing psd channel (RLE), aborting\n");
			return 0;
		}

		free(compbuf);
		free(rowcounts);
	}else{
		// There was no saving using RLE, so don't compress.

		put2B(out_psd, comp = RAWDATA); // compression type
		chansize = 2 + total_rows*ch->rowbytes;
		for(i = 0; i < chancount; ++i){
			for(j = 0; j < ch[i].rows; ++j){
				/* get row data */
				readunpackrow(psd, ch+i, j, inrow, rlebuf);

				/* write an uncompressed row */
				if((psd_pixels_t)fwrite(inrow, 1, ch[i].rowbytes, out_psd) != ch[i].rowbytes){
					alwayswarn("# error writing psd channel (raw), aborting\n");
					return 0;
				}
			}
		}
	}

	if(chancount > 1){
		VERBOSE("# %d merged channels: %u bytes (%s)\n", chancount, (unsigned)chansize, comptype[comp]);
	}else{
		VERBOSE("#   channel %d: %u bytes (%s)\n", chindex, (unsigned)chansize, comptype[comp]);
	}

	free(rlebuf);
	free(inrow);

	return chansize;
}

psd_bytes_t writelayerinfo(psd_file_t psd, psd_file_t out_psd,
						   int version, struct psd_header *h)
{
	int i, j, namelen, mask_size, extralen;
	psd_bytes_t size;
	struct layer_info *li;

	put2B(out_psd, h->mergedalpha ? -h->nlayers : h->nlayers);
	size = 2;
	for(i = 0, li = h->linfo; i < h->nlayers; ++i, ++li){
		put4B(out_psd, li->top);
		put4B(out_psd, li->left);
		put4B(out_psd, li->bottom);
		put4B(out_psd, li->right);
		put2B(out_psd, li->channels);
		size += 18;
		for(j = 0; j < li->channels; ++j){
			put2B(out_psd, li->chan[j].id);
			putpsdbytes(out_psd, version, li->chan[j].length_rebuild);
			size += 2 + PSDBSIZE(version);
		}
		fwrite(li->blend.sig, 1, 4, out_psd);
		fwrite(li->blend.key, 1, 4, out_psd);
		fputc(li->blend.opacity, out_psd);
		fputc(li->blend.clipping, out_psd);
		fputc(li->blend.flags, out_psd);
		fputc(PAD_BYTE, out_psd);
		size += 12;

		// layer's 'extra data' section ================================

		namelen = strlen(li->name);
		mask_size = li->mask.size >= 36 ? 36 : (li->mask.size >= 20 ? 20 : 0);

		extralen = 4 + mask_size + 4 + PAD4(namelen+1);
		put4B(out_psd, extralen);
		size += 4 + extralen;

		// layer mask data ---------------------------------------------
		put4B(out_psd, mask_size);
		if(mask_size >= 20){
			put4B(out_psd, li->mask.top);
			put4B(out_psd, li->mask.left);
			put4B(out_psd, li->mask.bottom);
			put4B(out_psd, li->mask.right);
			fputc(li->mask.default_colour, out_psd);
			fputc(li->mask.flags, out_psd);
			mask_size -= 18;
			if(mask_size >= 36){
				fputc(li->mask.real_flags, out_psd);
				fputc(li->mask.real_default_colour, out_psd);
				put4B(out_psd, li->mask.real_top);
				put4B(out_psd, li->mask.real_left);
				put4B(out_psd, li->mask.real_bottom);
				put4B(out_psd, li->mask.real_right);
				mask_size -= 18;
			}
			while(mask_size--)
				fputc(PAD_BYTE, out_psd);
		}

		// layer blending ranges ---------------------------------------
		put4B(out_psd, 0); // empty

		// layer name --------------------------------------------------
		fputc(namelen, out_psd);
		fwrite(li->name, 1, PAD4(namelen+1)-1, out_psd);

		// no 'additional info'

		// End of layer records section ================================
	}

	return size;
}

psd_bytes_t copy_block(psd_file_t psd, psd_file_t out_psd, psd_bytes_t pos){
	char *tempbuf;
	psd_bytes_t n, cnt;

	fseeko(psd, pos, SEEK_SET);
	n = get4B(psd); // TODO: sanity check this byte count
	tempbuf = malloc(n);
	fread(tempbuf, 1, n, psd);
	put4B(out_psd, n);
	cnt = fwrite(tempbuf, 1, n, out_psd);
	free(tempbuf);
	if(cnt != n)
		alwayswarn("# copy_block(): only wrote %d of %d bytes\n", cnt, n);
	return 4 + cnt;
}

void rebuild_psd(psd_file_t psd, int version, struct psd_header *h){
	psd_bytes_t lmipos, lmilen, layerlen, checklen;
	int i, j;
	struct layer_info *li;

	// File header
	writeheader(rebuilt_psd, version, h);

	// copy color mode data --------------------------------------------
	copy_block(psd, rebuilt_psd, h->colormodepos);

	// TODO: image resources -------------------------------------------
	put4B(rebuilt_psd, 0); // empty for now

	// Layer and mask information ======================================
	lmipos = ftello(rebuilt_psd);
	putpsdbytes(rebuilt_psd, version, 0); // dummy lmi length
	lmilen = PSDBSIZE(version);

	// Layer info ------------------------------------------------------
	putpsdbytes(rebuilt_psd, version, 0); // dummy layer info length
	layerlen = PSDBSIZE(version);
	layerlen += checklen = writelayerinfo(psd, rebuilt_psd, version, h);

	VERBOSE("# rebuilt layer info: %u bytes\n", (unsigned)layerlen);

	// Image data ======================================================
	for(i = 0, li = h->linfo; i < h->nlayers; ++i, ++li){
		VERBOSE("# rebuilt layer %d:\n", i);

		for(j = 0; j < li->channels; ++j)
			layerlen += li->chan[j].length_rebuild =
					writepsdchannels(rebuilt_psd, version, psd, j, li->chan + j, 1, h);
	}

	// Even alignment --------------------------------------------------
	if(layerlen & 1){
		++layerlen;
		fputc(0, rebuilt_psd);
	}

	// Global layer mask info ==========================================
	//layerlen += copy_block(psd, rebuilt_psd, h->global_lmi_pos);
	put4B(rebuilt_psd, 0); // empty for now
	layerlen += 4;

	// Merged image data ===============================================
	writepsdchannels(rebuilt_psd, version, psd, 0, h->merged_chans, h->channels, h);

	// Rebuild finished!

	// overwrite layer & mask information with fixed-up sizes
	fseeko(rebuilt_psd, lmipos, SEEK_SET);
	putpsdbytes(rebuilt_psd, version, lmilen + layerlen); // do fixup
	putpsdbytes(rebuilt_psd, version, layerlen); // do fixup
	if(writelayerinfo(psd, rebuilt_psd, version, h) != checklen)
		alwayswarn("# oops, this shouldn't happen " __FILE__ " @ %d\n", __LINE__);
}
