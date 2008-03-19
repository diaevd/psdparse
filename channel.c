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
				   int chcomp[],          // vector of compression types used for each channel
				   psd_bytes_t **rowpos,  // file offsets to rows, indexed by [channel] and [row]
				   int ch,                // channel to access
				   psd_pixels_t row,      // row index
				   psd_bytes_t rb,        // bytes per uncompressed row
				   unsigned char *inrow,  // dest buffer for the uncompressed row (rb bytes)
				   unsigned char *outrow) // temporary buffer for compressed data, 2 x rb in size
{
	psd_pixels_t n = 0, rlebytes;

	if(fseeko(psd, rowpos[ch][row], SEEK_SET) == -1){
		alwayswarn("# can't seek to " LL_L("%lld\n","%ld\n"), rowpos[ch][row]);
	}else{
		if(chcomp[ch] == RAWDATA){
			/* uncompressed */
			n = fread(inrow, 1, rb, psd);
			if(n < rb)
				warn("can't read row data (raw) @ " LL_L("%lld","%ld"), rowpos[ch][row]);
		}
		else if(chcomp[ch] == RLECOMP){
			/* RLE data */
			n = rowpos[ch][row+1] - rowpos[ch][row]; // get RLE byte count
			if(n > 2*rb){
				n = 2*rb; // sanity check
				warn("bad RLE byte count %5ld @ channel %2d, row %5ld", n, ch, row);
			}
			rlebytes = fread(outrow, 1, n, psd);
			if(rlebytes < n)
				warn("can't read row data (RLE) @ " LL_L("%lld","%ld"), rowpos[ch][row]);
			n = unpackbits(inrow, outrow, rb, rlebytes);
		}
		// if we don't recognise the compression type, skip the row
		// FIXME: or would it be better to use the last valid type seen?
	}
	
	if(n < rb)
		memset(inrow+n, 0, rb-n); // zero out whatever part of row wasn't written
}

// if rowpos is NULL, we're just skipping through this channel,
// not collecting row start positions.

int dochannel(psd_file_t f, struct layer_info *li, int idx, int channels,
			  psd_pixels_t rows, psd_pixels_t cols, int depth,
			  psd_bytes_t **rowpos, struct psd_header *h)
{
	static char *comptype[] = {"raw", "RLE", "ZIP without prediction", "ZIP with prediction"};
	int comp, ch, dumpit, samplebytes = depth > 8 ? depth/8 : 0;
	psd_bytes_t pos, chpos = ftello(f);
	psd_bytes_t rb, chlen = 0;
	unsigned char *rowbuf;
	psd_pixels_t k, count, last, *rlebuf = NULL;
	long n, j;

	if(li){
		chlen = li->chlengths[idx];
		VERBOSE(">>> dochannel %d/%d filepos=" LL_L("%7lld bytes=%7lld\n","%7ld bytes=%7ld\n"),
				idx, channels, chpos, chlen);

		if(chlen < 2){
			alwayswarn("## channel too short (", LL_L("%lld","%ld") " bytes)\n", chlen);
			if(chlen > 0)
				fseeko(f, chlen, SEEK_CUR); // skip it anyway, but not backwards
			return -1;
		}

		if(li->chid[idx] == -2){
			rows = li->mask.rows;
			cols = li->mask.cols;
			VERBOSE("# layer mask (%4ld,%4ld,%4ld,%4ld) (%4ld rows x %4ld cols)\n",
					li->mask.top,li->mask.left,li->mask.bottom,li->mask.right,rows,cols);
		}
	}else
		VERBOSE(">>> dochannel %d/%d filepos=" LL_L("%7lld\n","%7ld\n"),
				idx, channels, chpos);

	rb = ((long)cols*depth + 7)/8;

	comp = get2Bu(f);
	chlen -= 2;
	if(comp > RLECOMP){
		alwayswarn("## bad compression type %d\n", comp);
		if(li){ // make a guess based on channel byte count
			comp = chlen == rows*rb ? RAWDATA : RLECOMP;
			alwayswarn("## guessing: %s\n", comptype[comp]);
		}else{
			alwayswarn("## skipping channel (" LL_L("%lld","%ld") " bytes)\n", chlen);
			fseeko(f, chlen, SEEK_CUR);
			return -1;
		}
	}else
		VERBOSE("    compression = %d (%s)\n", comp, comptype[comp]);
	VERBOSE("    uncompressed size " LL_L("%lld","%ld") " bytes"
			" (row bytes = " LL_L("%lld","%ld") ")\n", channels*rows*rb, rb);

	rowbuf = checkmalloc(rb*2); /* slop for worst case RLE overhead (usually (rb/127+1) ) */
	pos = chpos+2;

	if(comp == RLECOMP){
		long rlecounts = (channels*rows) << h->version;
		if(li && chlen < rlecounts)
			alwayswarn("## channel too short for RLE row counts (need %ld bytes, have "
					   LL_L("%lld","%ld") " bytes)\n", rlecounts, chlen);

		pos += rlecounts; /* image data starts after RLE counts */
		rlebuf = checkmalloc(channels*rows*sizeof(psd_pixels_t));
		/* accumulate RLE counts, to make array of row start positions */
		for(ch = k = 0; ch < channels; ++ch){
			last = rb;
			for(j = 0; j < rows && !feof(f); ++j, ++k){
				count = h->version==1 ? get2Bu(f) : (unsigned long)get4B(f); // PSD/PSB
				if(count > 2*rb)  // this would be impossible
					count = last; // make a guess, to help recover
				rlebuf[k] = last = count;
				//printf("rowpos[%d][%3ld]=%6lld\n",ch,j,pos);
				if(rowpos) rowpos[ch][j] = pos;
				pos += count;
			}
			if(rowpos) rowpos[ch][j] = pos; /* = end of last row */
			if(j < rows) fatal("# couldn't read RLE counts");
		}
	}else if(rowpos){
		/* make array of row start positions (uncompressed; each row is rb bytes) */
		for(ch = 0; ch < channels; ++ch){
			for(j = 0; j < rows; ++j){
				rowpos[ch][j] = pos;
				pos += rb;
			}
			rowpos[ch][j] = pos; /* = end of last row */
		}
	}

	for(ch = k = 0; ch < channels; ++ch){
		
		//if(channels>1)
		VERBOSE("\n    channel %d (@ " LL_L("%7lld):\n","%7ld):\n"), ch, (psd_bytes_t)ftello(f));

		for(j = 0; j < rows; ++j){
			if(rows > 3*CONTEXTROWS){
				if(j == rows-CONTEXTROWS) 
					VERBOSE("    ...%ld rows not shown...\n", rows-2*CONTEXTROWS);
				dumpit = j < CONTEXTROWS || j >= rows-CONTEXTROWS;
			}else 
				dumpit = 1;

			if(comp == RLECOMP){
				n = rlebuf[k++];
				//VERBOSE("rle count[%5d] = %5d\n",j,n);
				if(n < 0 || n > 2*rb){
					warn("bad RLE count %5ld @ row %5ld",n,j);
					n = 2*rb;
				}
				if((psd_pixels_t)fread(rowbuf,1,n,f) == n){
					if(dumpit){
						VERBOSE("   %5ld: <%5ld> ",j,n);
						dumprow(rowbuf,n,samplebytes);
					}
				}else{
					memset(rowbuf,0,n);
					warn("couldn't read RLE row!");
				}
			}
			else if(comp == RAWDATA){
				if((psd_pixels_t)fread(rowbuf,1,rb,f) == rb){
					if(dumpit){
						VERBOSE("   %5ld: ",j);
						dumprow(rowbuf,rb,samplebytes);
					}
				}else{
					memset(rowbuf,0,rb);
					warn("couldn't read raw row!");
				}
			}
		} // for rows

	} // for channels
	
	if(li && ftello(f) != (chpos+2+chlen)){
		warn("currentpos = " LL_L("%lld, should be %lld !",
			 "%ld, should be %ld !"), ftello(f), chpos+2+chlen);
		fseeko(f, chpos+2+chlen, SEEK_SET);
	}

	if(comp == RLECOMP) free(rlebuf);
	free(rowbuf);

	return comp;
}
