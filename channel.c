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
	case RLECOMP: /* RLE data */
		pos = chan->rowpos[row];
		seekres = fseeko(psd, pos, SEEK_SET);
		if(seekres != -1){
			n = chan->rowpos[row+1] - chan->rowpos[row]; // get RLE byte count
			if(n > 2*chan->rowbytes){
				n = 2*chan->rowbytes; // sanity check
				warn("bad RLE byte count %5ld @ channel %2d, row %5ld", n, chan->id, row);
			}
			rlebytes = fread(rlebuf, 1, n, psd);
			if(rlebytes < n)
				warn("RLE row data short @ " LL_L("%lld","%ld"), pos);
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
	else if(n < chan->rowbytes){
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
	int comp, ch, dumpit, samplebytes = h->depth > 8 ? h->depth/8 : 0;
	psd_bytes_t pos, rb;
	unsigned char *rowbuf;
	psd_pixels_t count, last, *rlebuf;
	long n, j;

	chan->filepos = ftello(f) + 2;

	if(li){
		VERBOSE(">>> channel id = %d @ " LL_L("%7lld\n","%7ld\n"), chan->id, ftello(f));

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
		VERBOSE(">>> merged image data @ " LL_L("%7lld\n","%7ld\n"), ftello(f));
		chan->rows = h->rows;
		chan->cols = h->cols;
	}

	// Compute image row bytes
	rb = ((long)chan->cols*h->depth + 7)/8;

	// Read compression type
	comp = get2Bu(f);

	if(comp > ZIPPREDICT){
		alwayswarn("## bad compression type %d\n", comp);
		if(li){ // make a guess based on channel byte count
			comp = chan->length == chan->rows*rb ? RAWDATA : RLECOMP;
			alwayswarn("## guessing: %s\n", comptype[comp]);
		}else{
			comp = -1;
		}
	}else
		VERBOSE("    compression = %d (%s)\n", comp, comptype[comp]);
	VERBOSE("    uncompressed size " LL_L("%lld","%ld") " bytes"
			" (row bytes = " LL_L("%lld","%ld") ")\n", channels*chan->rows*rb, rb);

	// copy this info to all channels
	for(ch = 0; ch < channels; ++ch){
		if(!li)
			chan[ch].id = ch;
		chan[ch].rowbytes = rb;
		chan[ch].comptype = comp;
		chan[ch].rows = chan->rows;
		chan[ch].cols = chan->cols;
	}

	if(!chan->rows || comp == -1){
		//VERBOSE("## skipping channel\n");
		return;
	}

	// Prepare compressed data for later access:

	// For RLE, we read the row count array and compute file positions.
	// For ZIP, read and decompress whole channel.
	switch(comp){
	case RAWDATA:
		// skip channel's image data
		pos = chan->filepos + chan->rowbytes*chan->rows;
		fseeko(f, pos, SEEK_SET);
		break;
	case RLECOMP:
		/* image data starts after RLE counts */
		pos = chan->filepos + ((channels*chan->rows) << h->version);

		// assume that all channels have same row count and rowbytes
		// - this is safe, only merged data will have channels > 1
		rlebuf = checkmalloc(channels*chan->rows*sizeof(psd_pixels_t));

		/* accumulate RLE counts, to make array of row start positions */
		for(ch = 0; ch < channels; ++ch){
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
		}
		// skip channel's compressed image data
		fseeko(f, pos, SEEK_SET);
		break;
	case ZIPNOPREDICT:
	case ZIPPREDICT:
		if(li){
			unsigned char *zipdata = checkmalloc(chan->length);
			count = fread(zipdata, 1, chan->length, f);
			if(count < chan->length)
				warn("ZIP data short: wanted %d bytes, got %d", chan->length, count);
			chan->unzipdata = checkmalloc(chan->rows*chan->rowbytes);
			if(comp == ZIPNOPREDICT)
				psd_unzip_without_prediction(zipdata, count, chan->unzipdata, chan->rows*chan->rowbytes);
			else
				psd_unzip_with_prediction(zipdata, count, chan->unzipdata, chan->rows*chan->rowbytes, chan->cols, h->depth);
			free(zipdata);
		}else
			warn("ZIP data outside a layer");
		break;
	}
#if 0
	for(ch = 0; ch < channels; ++ch){
		rowbuf = checkmalloc(chan[ch].rowbytes*2); /* slop for worst case RLE overhead (usually (rb/127+1) ) */
		VERBOSE("\n    channel %d (@ " LL_L("%7lld):\n","%7ld):\n"),
				ch, (psd_bytes_t)ftello(f));

		for(j = 0; j < chan[ch].rows; ++j){
			if(chan[ch].rows > 3*CONTEXTROWS){
				if(j == chan[ch].rows-CONTEXTROWS)
					VERBOSE("    ...%ld rows not shown...\n", chan[ch].rows-2*CONTEXTROWS);
				dumpit = j < CONTEXTROWS || j >= chan[ch].rows-CONTEXTROWS;
			}else
				dumpit = 1;

			switch(comp){
			case RLECOMP:
				n = chan[ch].rowpos[j+1] - chan[ch].rowpos[j];
				//VERBOSE("rle count[%5d] = %5d\n",j,n);
				if(n < 0 || n > 2*chan[ch].rowbytes){
					warn("bad RLE count %5ld @ row %5ld",n,j);
					n = 2*chan[ch].rowbytes;
				}
				if(fseeko(f, chan[ch].rowpos[j], SEEK_SET) == -1)
					warn("can't seek to RLE data @ %d", chan[ch].rowpos[j]);
				else if((psd_pixels_t)fread(rowbuf, 1, n, f) == n){
					if(dumpit){
						VERBOSE("   %5ld: <%5ld> ", j, n);
						dumprow(rowbuf, n, samplebytes);
					}
				}else
					warn("couldn't read RLE row!");
				break;
			case RAWDATA:
				pos = chan[ch].filepos + j*chan[ch].rowbytes;
				if(fseeko(f, pos, SEEK_SET) == -1)
					warn("can't seek to raw data @ %d", pos);
				else if((psd_pixels_t)fread(rowbuf, 1, chan[ch].rowbytes, f) == chan[ch].rowbytes){
					if(dumpit){
						VERBOSE("   %5ld: ", j);
						dumprow(rowbuf, chan[ch].rowbytes, samplebytes);
					}
				}else
					warn("couldn't read raw row!");
				break;
			case ZIPNOPREDICT:
			case ZIPPREDICT:
				VERBOSE("   %5ld: (unzip) ",j);
				dumprow(chan[ch].unzipdata + j*chan[ch].rowbytes, chan[ch].rowbytes, samplebytes);
				break;
			}
		} // for rows

		free(rowbuf);
	} // for channels
#endif
}
