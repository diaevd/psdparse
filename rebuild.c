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

psd_bytes_t writelayerinfo(psd_file_t out_psd, int version, struct psd_header *h){
	unsigned i, j, namelen;
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

		namelen = strlen(li->name);

		put4B(out_psd, 4 + 4 + PAD4(namelen+1)); // size of 'extra data'
			put4B(out_psd, 0); // TODO: layer mask
		put4B(out_psd, 0); // empty 'layer blending ranges'
		fputc(namelen, out_psd);
		fwrite(li->name, 1, PAD4(namelen+1)-1, out_psd);
		size += PAD4(namelen+1);
		// no 'additional info'
	}

	return size;
}

void rebuild_psd(psd_file_t psd, int version, struct psd_header *h){
	psd_bytes_t lmipos, layerlen;

	writeheader(rebuilt_psd, version, h);
	put4B(rebuilt_psd, 0); // TODO: empty color mode data
	put4B(rebuilt_psd, 0); // TODO: empty image resources
	lmipos = ftello(rebuilt_psd);
	putpsdbytes(rebuilt_psd, version, 0); // dummy lmi section length
	putpsdbytes(rebuilt_psd, version, 0); // dummy layer section length
	layerlen = writelayerinfo(rebuilt_psd, version, h);
	fseeko(rebuilt_psd, lmipos, SEEK_SET);
	putpsdbytes(rebuilt_psd, version, layerlen + PSDBSIZE(version)); // do fixup
	putpsdbytes(rebuilt_psd, version, layerlen); // do fixup
}
