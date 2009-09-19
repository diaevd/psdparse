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
#include <fcntl.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

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
		if(!memcmp("8BIM", (char*)p+i, 4)){
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
										VERBOSE("@ %8d : key: %c%c%c%c  could be %d channel layer: t = %ld, l = %ld, b = %ld, r = %ld\n",
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

	if(!memcmp("8BIM", addr + offset, 4)){
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
	size_t i, j = 0, ps_ptr_bytes = 2 << h->version;

	h->lmistart = h->lmilen = 0;

	// first search for a valid image resource block (these precede layer/mask info)
	for(i = 0; i < len;)
	{
		j = is_resource(addr, len, i);
		if(j && j < len-4){
			VERBOSE("possible resource id=%d @ %lu\n", peek2B(addr+i+4), i);
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
				VERBOSE("possible empty LMI @ %lld\n", h->lmistart);
				UNQUIET(
"May be able to recover merged image if you can provide correct values\n\
for --mergedrows, --mergedcols, --mergedchan, --depth and --mode.\n");
				break; // take first one
			}
		}
	}
}

void scan_channels(unsigned char *addr, size_t len, struct psd_header *h)
{
	int i, n, c, rows, cols, rb, totalrle, countbytes = 2*h->version, count;
	struct layer_info *li = h->linfo;
	size_t lastpos = h->layerdatapos, pos, p;

	for(i = 0; i < h->nlayers; ++i)
	{
		li[i].chpos = 0;
		rows = li[i].bottom - li[i].top;
		cols = li[i].right - li[i].left;
		rb = ((long)cols*h->depth + 7)/8;
		if(rows && cols)
		{
			// scan forward for compression type value
			for(pos = lastpos; pos < len-2; pos += 2)
			{
				p = pos;
				for(c = li[i].channels; c-- && p < len-2;)
				{
					if(peek2Bu(addr+p) == 1)
					{
						//UNQUIET("possible rle start pos for layer %d channel %d: %lu ch=%d rows=%d\n",
						//		i, c, pos, li[i].channels, rows);
						p += 2;
						totalrle = 0;
						for(n = rows; n-- && p < len-countbytes; p += countbytes){ // assume PSD for now
							count = h->version == 1 ? peek2Bu(addr+p) : peek4B(addr+p);
							if(count < 2)
								goto mismatch; // bad RLE count
							else
								totalrle += count;
						}
						p += totalrle;
					}
					else if(peek2Bu(addr+p) == 0)
					{
						//UNQUIET("possible raw start pos for layer %d channel %d: %lu ch=%d rows=%d\n",
						//		i, c, pos, li[i].channels, rows);
						p += 2+rows*rb;
					}
					else
						goto mismatch;
				}
				if(c == -1)
				{
					// found likely match for RLE counts location
					UNQUIET("Likely start pos for layer %d: %lu  end=%lu\n", i, pos, p);
					li[i].chpos = pos;
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

int scavenge_psd(int fd, struct psd_header *h)
{
	void *addr;
	struct stat sb;

	if(fstat(fd, &sb) == 0 && (sb.st_mode & S_IFMT) == S_IFREG)
	{
		if( (addr = map_file(fd, sb.st_size)) )
		{
			if(scavenge){
				h->linfo = NULL;
				h->nlayers = scan(addr, sb.st_size, h);
				if(h->nlayers){
					if( (h->linfo = checkmalloc(h->nlayers*sizeof(struct layer_info))) )
						scan(addr, sb.st_size, h);
				}
				else
					scan_merged(addr, sb.st_size, h);
	
				if(h->nlayers){
					UNQUIET("possible layers (PS%c): %d\n", h->version == 2 ? 'B' : 'D', h->nlayers);
				}else
					alwayswarn("Did not find any plausible layer signatures.");
				//printf("possible layers (PSB): %d\n", scan(addr, sb.st_size, 1));
			}
			
			if(scavenge_rle)
				scan_channels(addr, sb.st_size, h);

			unmap_file(addr, sb.st_size);
			return h->nlayers;
		}
		else
			fprintf(stderr, "mmap() failed: %d\n", errno);

		unmap_file(addr, sb.st_size); // needed for Windows cleanup, will do nothing on UNIX
	}

	return 0;
}
