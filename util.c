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
#include <errno.h>

#include "psdparse.h"

#define WARNLIMIT 10

#ifdef HAVE_ICONV_H
	iconv_t ic;
#endif

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

void fputcxml(unsigned c, FILE *f){
	// see: http://www.w3.org/TR/REC-xml/#charsets
	// http://triptico.com/docs/unicode.html
	// http://www.unicode.org/unicode/faq/utf_bom.html
	// http://asis.epfl.ch/GNU.MISC/recode-3.6/recode_5.html
	// http://www.terena.org/activities/multiling/unicode/utf16.html
	switch(c){
	case '<':  fputs("&lt;", f); break;
	case '>':  fputs("&gt;", f); break;
	case '&':  fputs("&amp;", f); break;
	case '\'': fputs("&apos;", f); break;
	case '\"': fputs("&quot;", f); break;
	case 0x09:
	case 0x0a:
	case 0x0d:
		fputc(c, f);
		break;
	default:
		if(c >= 0x20){
			if(c < 0x7f) // ASCII printable
				fputc(c, f);
			else{
				warn("Unicode: %#x", c);
#ifdef HAVE_ICONV_H
				size_t inb, outb;
				const char *inbuf;
				char *outbuf, in[2], out[8];

				//iconv(ic, NULL, &inb, NULL, &outb); // reset iconv state
				// set up input as 2-byte BigEndian
				in[0] = c >> 8;
				in[1] = c;
				inbuf = in;
				inb = 2;
				outbuf = out;
				outb = sizeof(out);

				if(ic != (iconv_t)-1){
					if(iconv(ic, &inbuf, &inb, &outbuf, &outb) != (size_t)-1)
						fwrite(out, 1, sizeof(out)-outb, f);
					else
						alwayswarn("iconv() failed, errno=%u\n", errno);
				}
#endif
				// there is really no fallback if iconv isn't available
			}
		}else
			warn("char %#x not valid in XML; ignored", c);
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
	union {
		double d;
		unsigned char c[8];
	} u, urev;

	if(fread(u.c, 1, 8, f) == 8){
		if(platform_is_LittleEndian()){
			urev.c[0] = u.c[7];
			urev.c[1] = u.c[6];
			urev.c[2] = u.c[5];
			urev.c[3] = u.c[4];
			urev.c[4] = u.c[3];
			urev.c[5] = u.c[2];
			urev.c[6] = u.c[1];
			urev.c[7] = u.c[0];
			return urev.d;
		}
		else
			return u.d;
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

char indir[PATH_MAX];

void openfiles(char *psdpath, struct psd_header *h)
{
	char *ext, fname[PATH_MAX], *dirsuffix;

	strcpy(indir, psdpath);
	ext = strrchr(indir, '.');
	dirsuffix = h->depth < 32 ? "_png" : "_raw";
	ext ? strcpy(ext, dirsuffix) : strcat(indir, dirsuffix);

	if(writelist){
		setupfile(fname, pngdir, "list", ".txt");
		listfile = fopen(fname, "w");
	}

	// see: http://hsivonen.iki.fi/producing-xml/
	if(xmlout){
		quiet = writexml = 1;
		verbose = 0;
		xml = stdout;
	}else if(writexml){
		setupfile(fname, pngdir, "psd", ".xml");
		xml = fopen(fname, "w");
	}
	if(xml){
#ifdef HAVE_ICONV_H
		// I'm guessing that Adobe's unicode is utf-16 and not ucs-2; need to check this
		ic = iconv_open("UTF-8", "UTF-16BE");
		if(ic == (iconv_t)-1)
			alwayswarn("iconv_open(): failed, errno = %d\n", errno);
#endif
		fputs("<?xml version='1.0' encoding='UTF-8'?>\n", xml);
		fputs("<!-- generated by psdparse version " VERSION " -->\n", xml);
	}
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
