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

#define CONTEXTROWS 3
#define DUMPCOLS 50

void dumprow(unsigned char *b, long n, int group){
	long k, m = group ? group*((DUMPCOLS/2)/(group+1)) : DUMPCOLS/2;
	if(m > n)
		m = n;
	for(k = 0; k < m; ++k){
		if(group && !(k % group)) VERBOSE(" ");
		VERBOSE("%02x",b[k]);
	}
	if(n > m)
		VERBOSE(" ...%ld more", group ? (n-m)/group : n-m);
	VERBOSE("\n");
}

void readunpackrow(psd_file_t psd,        // input file handle
				   struct channel_info *chan, // channel info
				   psd_pixels_t row,      // row index
				   unsigned char *inrow,  // dest buffer for the uncompressed row (rb bytes)
				   unsigned char *rlebuf) // temporary buffer for compressed data, 2 x rb in size
{
	psd_pixels_t n = 0, rlebytes;
	psd_bytes_t pos;
	int seekres = 0;

	switch(chan->comptype){
	case RAWDATA: /* uncompressed */
		pos = chan->filepos + chan->rowbytes*row;
		seekres = fseeko(psd, pos, SEEK_SET);
		if(seekres != -1)
			n = fread(inrow, 1, chan->rowbytes, psd);
		break;
	case RLECOMP:
		pos = chan->rowpos[row];
		seekres = fseeko(psd, pos, SEEK_SET);
		if(seekres != -1){
			rlebytes = fread(rlebuf, 1, chan->rowpos[row+1] - pos, psd);
			n = unpackbits(inrow, rlebuf, chan->rowbytes, rlebytes);
		}
		break;
	case ZIPNOPREDICT:
	case ZIPPREDICT:
		memcpy(inrow, chan->unzipdata + chan->rowbytes*row, chan->rowbytes);
		return;
	}
	// if we don't recognise the compression type, skip the row
	// FIXME: or would it be better to use the last valid type seen?

	if(seekres == -1)
		alwayswarn("# can't seek to " LL_L("%lld\n","%ld\n"), pos);

	if(n < chan->rowbytes){
		warn("row data short (wanted %d, got %d bytes)", chan->rowbytes, n);
		// zero out unwritten part of row
		memset(inrow + n, 0, chan->rowbytes - n);
	}
}

void dochannel(psd_file_t f,
			   struct layer_info *li,
			   struct channel_info *chan, // array of channel info
			   int channels, // how many channels are to be processed (>1 only for merged data)
			   struct psd_header *h)
{
	static char *comptype[] = {"raw", "RLE", "ZIP without prediction", "ZIP with prediction"};
	int comp, ch;
	psd_bytes_t chpos, pos, rb;
	unsigned char *zipdata;
	psd_pixels_t count, last, j;

	chpos = ftello(f);

	if(li){
		VERBOSE(">>> channel id = %d @ " LL_L("%7lld, %lld","%7ld, %ld") " bytes\n",
				chan->id, chpos, chan->length);

		// If this is a layer mask, the pixel size is a special case
		if(chan->id == -2){
			chan->rows = li->mask.rows;
			chan->cols = li->mask.cols;
			VERBOSE("# layer mask (%4ld,%4ld,%4ld,%4ld) (%4ld rows x %4ld cols)\n",
					li->mask.top,li->mask.left,li->mask.bottom,li->mask.right,chan->rows,chan->cols);
		}else{
			// channel has dimensions of the layer
			chan->rows = li->bottom - li->top;
			chan->cols = li->right - li->left;
		}
	}else{
		// merged image, has dimensions of PSD
		VERBOSE(">>> merged image data @ " LL_L("%7lld\n","%7ld\n"), chpos);
		chan->rows = h->rows;
		chan->cols = h->cols;
	}

	// Compute image row bytes
	rb = ((long)chan->cols*h->depth + 7)/8;

	// Read compression type
	comp = get2Bu(f);

	VERBOSE("    compression = %d (%s)\n", comp, comptype[comp]);
	VERBOSE("    uncompressed size " LL_L("%lld","%ld") " bytes"
			" (row bytes = " LL_L("%lld","%ld") ")\n", channels*chan->rows*rb, rb);

	// Prepare compressed data for later access:

	pos = chpos + 2;

	// skip rle counts, leave pos pointing to first compressed image row
	if(comp == RLECOMP)
		pos += (channels*chan->rows) << h->version;

	for(ch = 0; ch < channels; ++ch){
		if(!li)
			chan[ch].id = ch;
		chan[ch].rowbytes = rb;
		chan[ch].comptype = comp;
		chan[ch].rows = chan->rows;
		chan[ch].cols = chan->cols;
		chan[ch].filepos = pos;

		if(!chan->rows)
			continue;

		// For RLE, we read the row count array and compute file positions.
		// For ZIP, read and decompress whole channel.
		switch(comp){
		case RAWDATA:
			pos += chan->rowbytes*chan->rows;
			break;

		case RLECOMP:
			/* accumulate RLE counts, to make array of row start positions */
			chan[ch].rowpos = checkmalloc((chan[ch].rows+1)*sizeof(psd_bytes_t));
			last = chan[ch].rowbytes;
			for(j = 0; j < chan[ch].rows && !feof(f); ++j){
				count = h->version==1 ? get2Bu(f) : (unsigned long)get4B(f);

				if(count > 2*chan[ch].rowbytes)  // this would be impossible
					count = last; // make a guess, to help recover
				last = count;

				chan[ch].rowpos[j] = pos;
				pos += count;
			}
			if(j < chan[ch].rows)
				fatal("# couldn't read RLE counts");
			chan[ch].rowpos[j] = pos; /* = end of last row */
			break;

		case ZIPNOPREDICT:
		case ZIPPREDICT:
			if(li){
				pos += chan->length - 2;

				zipdata = checkmalloc(chan->length);
				count = fread(zipdata, 1, chan->length - 2, f);
				if(count < chan->length - 2)
					alwayswarn("ZIP data short: wanted %d bytes, got %d", chan->length, count);

				chan->unzipdata = checkmalloc(chan->rows*chan->rowbytes);
				if(comp == ZIPNOPREDICT)
					psd_unzip_without_prediction(zipdata, count, chan->unzipdata,
												 chan->rows*chan->rowbytes);
				else
					psd_unzip_with_prediction(zipdata, count, chan->unzipdata,
											  chan->rows*chan->rowbytes,
											  chan->cols, h->depth);

				free(zipdata);
			}else
				alwayswarn("## can't process ZIP outside layer");
			break;
		default:
			VERBOSE("## bad compression type - skipping channel\n");
			if(li)
				fseeko(f, chan->length - 2, SEEK_CUR);
			break;
		}
	}

	if(li && pos != chpos + chan->length)
		alwayswarn("# channel data is %ld bytes, but length = %ld\n",
				   pos - chpos, chan->length);

	// the file pointer must be left at the end of the channel's data
	fseeko(f, pos, SEEK_SET);
}
