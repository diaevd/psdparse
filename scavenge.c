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

#include <stdio.h>
#include <stdlib.h>

#ifdef HAVE_ZLIB_H
	#include "zlib.h"
#endif

#include "psdparse.h"

extern struct dictentry bmdict[];

extern int scavenge, scavenge_rle;

// memory-mapped analogues of the routines in util.c

// Read a 4-byte signed binary value in BigEndian format.
// Assumes sizeof(long) == 4 (and two's complement CPU :)
long peek4B(unsigned char *p){
	return (p[0]<<24) | (p[1]<<16) | (p[2]<<8) | p[3];
}

// Read a 8-byte signed binary value in BigEndian format.
// Assumes sizeof(long) == 4
int64_t peek8B(unsigned char *p){
	int64_t msl = (unsigned long)peek4B(p);
	return (msl << 32) | (unsigned long)peek4B(p+4);
}

// Read a 2-byte signed binary value in BigEndian format.
// Meant to work even where sizeof(short) > 2
int peek2B(unsigned char *p){
	unsigned n = (p[0]<<8) | p[1];
	return n < 0x8000 ? n : n - 0x10000;
}

// Read a 2-byte unsigned binary value in BigEndian format.
unsigned peek2Bu(unsigned char *p){
	return (p[0]<<8) | p[1];
}

unsigned scan(unsigned char *addr, size_t len, struct psd_header *h)
{
	unsigned char *p = addr, *q;
	size_t i;
	int j, k;
	unsigned n = 0, ps_ptr_bytes = 2 << h->version;
	struct dictentry *de;
	struct layer_info *li = h->linfo;

	for(i = 0; i < len;)
	{
		if(KEYMATCH((char*)p+i, "8BIM")){
			i += 4;

			// found possible layer signature
			// check next 4 bytes for a known blend mode
			for(de = bmdict; de->key; ++de)
				if(!memcmp(de->key, (char*)p+i, 4)){
					// found a possible layer blendmode signature
					// try to guess number of channels
					for(j = 1; j < 64; ++j){
						q = p + i - 4 - j*(ps_ptr_bytes + 2) - 2;
						if(peek2B(q) == j){
							long t = peek4B(q-16), l = peek4B(q-12), b = peek4B(q-8), r = peek4B(q-4);
							// sanity check bounding box
							if(b >= t && r >= l){
								// sanity check channel ids
								for(k = 0; k < j; ++k){
									int chid = peek2B(q + 2 + k*(ps_ptr_bytes + 2));
									if(chid < -2 || chid >= j)
										break; // smells bad, give up
								}
								if(k == j){
									// channel ids were ok. could still be a valid guess...
									++n;
									if(li){
										li->filepos = q - p - 16;
										++li;
									}
									else
										VERBOSE("scavenge @ %8d : key: %c%c%c%c  could be %d channel layer: t = %ld, l = %ld, b = %ld, r = %ld\n",
											   q - p - 16,
											   de->key[0], de->key[1], de->key[2], de->key[3],
											   j,
											   t, l, b, r);
									break;
								}
							}
						}
					}

					break;
				}
		}else
			++i;
	}
	return n;
}

int is_resource(unsigned char *addr, size_t len, size_t offset)
{
	int namelen;
	long size;

	if(KEYMATCH(addr + offset, "8BIM")){
		offset += 4; // type
		offset += 2; // id
		namelen = addr[offset];
		offset += PAD2(1+namelen);
		size = peek4B(addr+offset);
		offset += 4;
		offset += PAD2(size); // skip resource block data
		return offset; // should be offset of next resource
	}
	return 0;
}

// if no valid layer signature was found, then look for an empty layer/mask info block
// which would imply only merged data is in the file (no layers)

void scan_merged(unsigned char *addr, size_t len, struct psd_header *h)
{
	size_t i, j = 0;
	unsigned ps_ptr_bytes = 2 << h->version;

	h->lmistart = h->lmilen = 0;

	// first search for a valid image resource block (these precede layer/mask info)
	for(i = 0; i < len;)
	{
		j = is_resource(addr, len, i);
		if(j && j < len-4){
			VERBOSE("scavenge: possible resource id=%d @ %lu\n", peek2B(addr+i+4), (unsigned long)i);
			i = j; // found an apparently valid resource; skip over it

			// is it followed by another resource?
			if(memcmp("8BIM", addr+j, 4))
				break; // no - stop looking
		}else
			++i;
	}

	if(!j)
		alwayswarn("Did not find any plausible image resources; probably cannot locate merged image data.\n");

	for(i = j; i < len; ++i)
	{
		psd_bytes_t lmilen = h->version == 2 ? peek8B(addr+i) : peek4B(addr+i),
				  layerlen = h->version == 2 ? peek8B(addr+i+ps_ptr_bytes) : peek4B(addr+i+ps_ptr_bytes);;
		if(lmilen > 0 && (i+lmilen) < len && layerlen == 0)
		{
			// sanity check compression type
			int comptype = peek2Bu(addr + i + lmilen);
			if(comptype == 0 || comptype == 1)
			{
				h->lmistart = i+ps_ptr_bytes;
				h->lmilen = lmilen;
				VERBOSE("scavenge: possible empty LMI @ %lld\n", h->lmistart);
				UNQUIET(
"May be able to recover merged image if you can provide correct values\n\
for --mergedrows, --mergedcols, --mergedchan, --depth and --mode.\n");
				break; // take first one
			}
		}
	}
}

size_t try_inflate(unsigned char *src_buf, size_t src_len,
				   unsigned char *dst_buf, size_t dst_len)
{
#ifdef HAVE_ZLIB_H
	z_stream stream;
	int state;

	memset(&stream, 0, sizeof(z_stream));
	stream.data_type = Z_BINARY;

	stream.next_in = src_buf;
	stream.avail_in = src_len;
	stream.next_out = dst_buf;
	stream.avail_out = dst_len;

	if(inflateInit(&stream) == Z_OK) {
		do {
			state = inflate(&stream, Z_FINISH);
			//fprintf(stderr,"inflate: state=%d next_in=%p avail_in=%d next_out=%p avail_out=%d\n",
			//		state, stream.next_in, stream.avail_in, stream.next_out, stream.avail_out);
			if(state == Z_STREAM_END)
				return stream.next_in - src_buf;
		}  while (state == Z_OK && stream.avail_out > 0);
	}
#endif
	return 0;
}

void scan_channels(unsigned char *addr, size_t len, struct psd_header *h)
{
	int i, n, c, rows, cols, rb, totalrle, comp, nextcomp, countbytes = 2*h->version;
	struct layer_info *li = h->linfo;
	size_t lastpos = h->layerdatapos, pos, p, count, bufsize;
	unsigned char *buf;

	UNQUIET("scan_channels(): starting @ %lu\n", (unsigned long)lastpos);

	for(i = 0; i < h->nlayers; ++i)
	{
		UNQUIET("scan_channels(): layer %d, channels: %d\n", i, li[i].channels);

		li[i].chpos = 0;
		rows = li[i].bottom - li[i].top;
		cols = li[i].right  - li[i].left;
		if(rows && cols)
		{
			rb = ((long)cols*h->depth + 7)/8;

			// scan forward for compression type value
			for(pos = lastpos; pos < len-2; pos += 2)
			{
				p = pos;
				for(c = 0; c < li[i].channels && p < len-2; ++c)
				{
					comp = peek2Bu(addr+p);
					p += 2;
					switch(comp)
					{
					case RAWDATA:
						nextcomp = peek2Bu(addr + p + rows*rb);
						if(nextcomp >= RAWDATA && nextcomp <= ZIPPREDICT){
							p += li[c].chan[c].length = rows*rb;
							//VERBOSE("layer %d channel %d: possible RAW data @ %lu\n", i, c, p);
						}else
							goto mismatch;
						break;
					case RLECOMP:
						totalrle = 0;
						for(n = rows; n-- && p < len-countbytes; p += countbytes){ // assume PSD for now
							count = h->version == 1 ? peek2Bu(addr+p) : (size_t)peek4B(addr+p);
							if(count >= 2){
								totalrle += count;
								//VERBOSE("layer %d channel %d: possible RLE data @ %lu\n", i, c, p);
							}else
								goto mismatch; // bad RLE count
						}
						p += li[c].chan[c].length = totalrle;
						break;
					case ZIPNOPREDICT:
					case ZIPPREDICT:
						//fprintf(stderr,"ZIP comp type seen... rows=%d rb=%d\n",rows,rb);
						bufsize = rows*rb;
						buf = checkmalloc(bufsize);
						count = try_inflate(addr+p, len-p, buf, bufsize);
						free(buf);
						if(count){
							//fprintf(stderr,"ZIP OK @ %d! count=%d\n",p,count);
							//li[i].chan[c].length = count;
							p += li[c].chan[c].length = count;
							break;
						}
					default:
						goto mismatch;
					}

					li[c].chan[c].comptype = comp;
					li[c].chan[c].id = c;
				}

				if(c == li[i].channels)
				{
					// found likely match for RLE counts location
					UNQUIET("scan_channels(): layer %d may be @ %7lu\n", i, (unsigned long)pos);
					li[i].chpos = pos;
					li[i].chan = checkmalloc(li[i].channels*sizeof(struct channel_info));
					lastpos = p; // step past it

					goto next_layer;
				}
mismatch:
				;
			}
		}
next_layer:
		;
	}
}

int scavenge_psd(void *addr, size_t st_size, struct psd_header *h)
{
	if(scavenge){
		h->linfo = NULL;
		h->nlayers = scan(addr, st_size, h);
		if(h->nlayers){
			if( (h->linfo = checkmalloc(h->nlayers*sizeof(struct layer_info))) )
				scan(addr, st_size, h);
		}
		else
			scan_merged(addr, st_size, h);

		if(h->nlayers){
			UNQUIET("scavenge: possible layers (PS%c): %d\n", h->version == 2 ? 'B' : 'D', h->nlayers);
		}else
			alwayswarn("Did not find any plausible layer signatures (flattened file?)\n");
		//printf("possible layers (PSB): %d\n", scan(addr, sb.st_size, 1));
	}

	return h->nlayers;
}
