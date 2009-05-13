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
#include <sys/mman.h>
#include <sys/stat.h>

#include "psdparse.h"

int verbose = 0, quiet = 0, makedirs = 0;

char dirsep[] = {'/',0};
FILE *xml = NULL;

extern struct dictentry bmdict[];

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

unsigned scan(unsigned char *addr, size_t len, int psb_flag)
{
	unsigned char *p = addr, *q;
	size_t i;
	unsigned j, n = 0, ps_ptr_bytes = psb_flag ? 8 : 4;
	struct dictentry *de;

	for(i = 0; i < len;)
	{
		if(!strncmp("8BIM", (char*)p+i, 4)){
			i += 4;

			// found possible layer signature
			// check next 4 bytes for a known blend mode
			for(de = bmdict; de->key; ++de)
				if(!strncmp(de->key, (char*)p+i, 4)){
					// found a possible layer blendmode signature
					// try to guess number of channels
					for(j = 1; j < 64; ++j){
						q = p + i - 4 - j*(ps_ptr_bytes + 2) - 2; // for PSD. for PSB, j*8 !
						if(peek2Bu(q) == j){
							long t = peek4B(q-16), l = peek4B(q-12), b = peek4B(q-8), r = peek4B(q-4);
							if(b > t && r > l){
								++n;
								printf("@ %8d : key: %c%c%c%c  could be %d channels: t = %lu, l = %lu, b = %lu, r = %lu\n",
									   q - p - 16,
									   de->key[0], de->key[1], de->key[2], de->key[3],
									   j,
									   t, l, b, r);
								break;
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

int scavenge(char *path)
{
	int fd, status = 0;
	void *addr;
	struct stat sb;

	if( (fd = open(path, O_RDONLY, 0)) != -1 )
	{
		if(fstat(fd, &sb) == 0 && (sb.st_mode & S_IFMT) == S_IFREG)
		{
			addr = mmap(NULL, sb.st_size, PROT_READ, MAP_FILE, fd, 0);
			if(addr != MAP_FAILED)
			{
				printf("possible layers (PSD): %d\n", scan(addr, sb.st_size, 0));
				//printf("possible layers (PSB): %d\n", scan(addr, sb.st_size, 1));

				munmap(addr, sb.st_size);
				status = 1;
			}
			else
				fputs("mmap() failed", stderr);
		}
		close(fd);
	}
	return status;
}

int main(int argc, char *argv[])
{
	return argc == 2 && scavenge(argv[1]) ? EXIT_SUCCESS : EXIT_FAILURE;
}
