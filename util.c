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

#include <stdarg.h>
#include <ctype.h>

#include "psdparse.h"

#define WARNLIMIT 10

void fatal(char *s){
	fflush(stdout);
	fputs(s,stderr);
	exit(EXIT_FAILURE);
}

int nwarns = 0;

void warn(char *fmt,...){
	char s[0x200];
	va_list v;

	if(nwarns == WARNLIMIT) fputs("#   (further warnings suppressed)\n",stderr);
	++nwarns;
	if(nwarns <= WARNLIMIT){
		va_start(v,fmt);
		vsnprintf(s,0x200,fmt,v);
		va_end(v);
		fflush(stdout);
		fprintf(stderr,"#   warning: %s\n",s);
	}
}

void alwayswarn(char *fmt,...){
	char s[0x200];
	va_list v;

	va_start(v,fmt);
	vsnprintf(s,0x200,fmt,v);
	va_end(v);
	fflush(stdout);
	fputs(s,stderr);
}

void *checkmalloc(long n){
	void *p = malloc(n);
	if(p) return p;
	else fatal("can't get memory");
	return NULL;
}

// escape XML special characters to entities
// see: http://www.w3.org/TR/xml/#sec-predefined-ent

void fputcxml(char c,FILE *f){
	switch(c){
	case '<':  fputs("&lt;",f); break;
	case '>':  fputs("&gt;",f); break;
	case '&':  fputs("&amp;",f); break;
	case '\'': fputs("&apos;",f); break;
	case '\"': fputs("&quot;",f); break;
	default:
		if(isascii(c))
			fputc(c,f);
		else
			fprintf(f,"&#%d;",c & 0xff);
	}
}

void fputsxml(char *str,FILE *f){
	char *p = str;
	while(*p)
		fputcxml(*p++, f);
}

// fetch Pascal string (length byte followed by text)
char *getpstr(FILE *f){
	static char pstr[0x100];
	int len = fgetc(f) & 0xff;
	fread(pstr, 1, len, f);
	pstr[len] = 0;
	return pstr;
}

// Pascal string, aligned to 2 byte
char *getpstr2(FILE *f){
	static char pstr[0x100];
	int len = fgetc(f) & 0xff;
	fread(pstr, 1, len + !(len & 1), f); // if length is even, read an extra byte
	pstr[len] = 0;
	return pstr;
}

static int platform_is_LittleEndian(){
	union{ int a; char b; } u;
	u.a = 1;
	return u.b;
}

double getdoubleB(FILE *f){
	unsigned char be[8],le[8];;

	if(fread(be, 1, 8, f) == 8){
		if(platform_is_LittleEndian()){
			le[0] = be[7];
			le[1] = be[6];
			le[2] = be[5];
			le[3] = be[4];
			le[4] = be[3];
			le[5] = be[2];
			le[6] = be[1];
			le[7] = be[0];
			return *(double*)le;
		}else
			return *(double*)be;
	}
	return 0;
}

// Read a 4-byte signed binary value in BigEndian format.
// Assumes sizeof(long) == 4 (and two's complement CPU :)
long get4B(FILE *f){
	long n = fgetc(f)<<24;
	n |= fgetc(f)<<16;
	n |= fgetc(f)<<8;
	return n | fgetc(f);
}

// Read a 2-byte signed binary value in BigEndian format. 
// Meant to work even where sizeof(short) > 2
int get2B(FILE *f){
	unsigned n = fgetc(f)<<8;
	n |= fgetc(f);
	return n < 0x8000 ? n : n - 0x10000;
}

// Read a 2-byte unsigned binary value in BigEndian format. 
unsigned get2Bu(FILE *f){
	unsigned n = fgetc(f)<<8;
	return n |= fgetc(f);
}