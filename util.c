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
	fputs(s, stderr);
#ifdef PSDPARSE_PLUGIN
	// FIXME: show user a message
	// caller must be prepared for this function to return, of course
	pl_fatal(s);
#else
	exit(EXIT_FAILURE);
#endif
}

int nwarns = 0;

void warn(char *fmt, ...){
	char s[0x200];
	va_list v;

	if(nwarns == WARNLIMIT) fputs("#   (further warnings suppressed)\n", stderr);
	++nwarns;
	if(nwarns <= WARNLIMIT){
		va_start(v, fmt);
		vsnprintf(s, 0x200, fmt, v);
		va_end(v);
		fflush(stdout);
		fprintf(stderr, "#   warning: %s\n", s);
	}
}

void alwayswarn(char *fmt, ...){
	char s[0x200];
	va_list v;

	va_start(v, fmt);
	vsnprintf(s, 0x200, fmt, v);
	va_end(v);
	fflush(stdout);
	fputs(s, stderr);
}

void *checkmalloc(long n){
	void *p = malloc(n);
	if(p) return p;
	else fatal("can't get memory");
	return NULL;
}

// escape XML special characters to entities
// see: http://www.w3.org/TR/xml/#sec-predefined-ent

void fputcxml(wchar_t c, FILE *f){
	switch(c){
	case '<':  fputs("&lt;", f); break;
	case '>':  fputs("&gt;", f); break;
	case '&':  fputs("&amp;", f); break;
	case '\'': fputs("&apos;", f); break;
	case '\"': fputs("&quot;", f); break;
	default:
#ifdef HAVE_NEWLOCALE
		if(utf_locale)
			fputwc_l(c, f, utf_locale);
		else
#endif
		if(c < 0x80 && isprint(c)) // ASCII printable
			fputc(c, f);
		else
			fprintf(f, "&#x%04x;", c);
	}
}

void fputsxml(char *str, FILE *f){
	char *p = str;
	while(*p)
		fputcxml(*p++, f);
}

// fetch Pascal string (length byte followed by text)
// N.B. This returns a pointer to the string as a C string (no length
//      byte, and terminated by NUL).
char *getpstr(psd_file_t f){
	static char pstr[0x100];
	int len = fgetc(f);
	if(len != EOF){
		fread(pstr, 1, len, f);
		pstr[len] = 0;
	}else
		pstr[0] = 0;
	return pstr;
}

// Pascal string, aligned to 2 byte
char *getpstr2(psd_file_t f){
	char *p = getpstr(f);
	if(!(p[0] & 1))
		fgetc(f); // skip padding
	return p;
}

char *getkey(psd_file_t f){
	static char k[5];
	if(fread(k, 1, 4, f) == 4)
		k[4] = 0;
	else
		k[0] = 0; // or return NULL?
	return k;
}

static int platform_is_LittleEndian(){
	union{ int a; char b; } u;
	u.a = 1;
	return u.b;
}

double getdoubleB(psd_file_t f){
	unsigned char raw[8], rev[8];

	if(fread(raw, 1, 8, f) == 8){
		if(platform_is_LittleEndian()){
			rev[0] = raw[7];
			rev[1] = raw[6];
			rev[2] = raw[5];
			rev[3] = raw[4];
			rev[4] = raw[3];
			rev[5] = raw[2];
			rev[6] = raw[1];
			rev[7] = raw[0];
			return *(double*)rev;
		}else
			return *(double*)raw;
	}
	return 0;
}

// Read a 4-byte signed binary value in BigEndian format.
// Assumes sizeof(long) == 4 (and two's complement CPU :)
long get4B(psd_file_t f){
	long n = fgetc(f)<<24;
	n |= fgetc(f)<<16;
	n |= fgetc(f)<<8;
	return n | fgetc(f);
}

// Read a 8-byte signed binary value in BigEndian format.
// Assumes sizeof(long) == 4
int64_t get8B(psd_file_t f){
	int64_t msl = (unsigned long)get4B(f);
	return (msl << 32) | (unsigned long)get4B(f);
}

// Read a 2-byte signed binary value in BigEndian format. 
// Meant to work even where sizeof(short) > 2
int get2B(psd_file_t f){
	unsigned n = fgetc(f)<<8;
	n |= fgetc(f);
	return n < 0x8000 ? n : n - 0x10000;
}

// Read a 2-byte unsigned binary value in BigEndian format. 
unsigned get2Bu(psd_file_t f){
	unsigned n = fgetc(f)<<8;
	return n |= fgetc(f);
}

// return pointer to a string of n tabs
const char *tabs(int n){
	static const char forty[] = "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";
	return forty + (sizeof(forty) - 1 - n);
}

// construct the destination filename, and create enclosing directories
// as needed (and if requested).

void setupfile(char *dstname, char *dir, char *name, char *suffix){
	char *last, d[PATH_MAX];

	MKDIR(dir, 0755);

	if(strchr(name, DIRSEP)){
		if(!makedirs)
			alwayswarn("# warning: replaced %c's in filename (use --makedirs if you want subdirectories)\n", DIRSEP);
		for(last = name; (last = strchr(last+1, '/')); )
			if(makedirs){
				last[0] = 0;
				strcpy(d, dir);
				strcat(d, dirsep);
				strcat(d, name);
				if(!MKDIR(d, 0755)) VERBOSE("# made subdirectory \"%s\"\n", d);
				last[0] = DIRSEP;
			}else 
				last[0] = '_';
	}

	strcpy(dstname, dir);
	strcat(dstname, dirsep);
	strcat(dstname, name);
	strcat(dstname, suffix);
}
