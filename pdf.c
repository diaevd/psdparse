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

/* PDF data. This has embedded literal strings in UTF-16, e.g.:

00000820  74 6f 72 0a 09 09 3c 3c  0a 09 09 09 2f 54 65 78  |tor...<<..../Tex|
00000830  74 20 28 fe ff ac f5 ac  1c 00 20 d3 ec d1 a0 c5  |t (....... .....|
00000840  68 bc 94 00 20 d5 04 b8  5c 5c ad f8 b7 a8 c7 78  |h... ...\\.....x|
00000850  00 20 b9 e5 c2 a4 d3 98  c7 74 d3 7c c7 58 00 20  |. .......t.|.X. |
00000860  d3 ec d1 a0 c0 f5 00 20  d3 0c c7 7c 00 20 cd 9c  |....... ...|. ..|
00000870  b8 25 c7 44 00 20 c7 04  d5 5c 5c 00 2c 00 20 00  |.%.D. ...\\.,. .|
00000880  50 00 53 00 44 d3 0c c7  7c c7 58 00 20 b0 b4 bd  |P.S.D...|.X. ...|
00000890  80 00 20 ad 6c c8 70 00  20 bd 84 c1 1d d3 0c c7  |.. .l.p. .......|
000008a0  7c c7 85 b2 c8 b2 e4 00  2e 00 0d 00 0d 00 0d 29  ||..............)|

From PDF Reference 1.7, 3.8.1 Text String Type

The text string type is used for character strings that are encoded in either PDFDocEncoding
or the UTF-16BE Unicode character encoding scheme. PDFDocEncoding
can encode all of the ISO Latin 1 character set and is documented in Appendix D. ...

For text strings encoded in Unicode, the first two bytes must be 254 followed by 255.
These two bytes represent the Unicode byte order marker, U+FEFF,
indicating that the string is encoded in the UTF-16BE (big-endian) encoding scheme ...

Note: Applications that process PDF files containing Unicode text strings
should be prepared to handle supplementary characters;
that is, characters requiring more than two bytes to represent.
*/

int is_pdf_white(char c){
	return c == '\000' || c == '\011' || c == '\012' || c == '\014' || c == '\015' || c == '\040';
}

int is_pdf_delim(char c){
	return c == '(' || c == ')' || c == '<' || c == '>'
		|| c == '[' || c == ']' || c == '{' || c == '}'
		|| c == '/' || c == '%';
}

// p      : pointer to first character following opening ( of string
// outbuf : destination buffer for parsed string. pass NULL to count but not store
// n      : count of characters available in input buffer
// returns number of characters in parsed string
// updates the source pointer to the first character after the string

size_t pdf_string(unsigned char **p, unsigned char *outbuf, size_t n){
	int paren = 1;
	size_t cnt = 0;
	while(n){
		char c = *(*p)++;
		--n;
		switch(c){
		case '(':
			++paren;
			break;
		case ')':
			if(!(--paren))
				return cnt; // it was the closing paren
			break;
		case '\\':
			if(!n)
				return cnt; // ran out of data
			--n;
			switch(*(*p)++){
			case 'n': c = 012; break; // LF
			case 'r': c = 015; break; // CR
			case 't': c = 011; break; // HT
			case 'b': c = 010; break; // BS
			case 'f': c = 014; break; // FF
			case '(':
			case ')':
			case '\\': c = (*p)[-1]; break;
			default:
				// octal escape?
				if(n >= 2 && isdigit((*p)[-1]) && isdigit((*p)[0]) && isdigit((*p)[1])){
					c = (((*p)[-1]-'0') << 6) | (((*p)[0]-'0') << 3) | ((*p)[1]-'0');
					*p += 2;
					n -= 2;
				}else
					continue;
			}
		}
		if(outbuf)
			*outbuf++ = c;
		++cnt;
	}
	return cnt;
}

// parameters analogous to pdf_string()'s
size_t pdf_name(unsigned char **p, unsigned char *outbuf, size_t n){
	size_t cnt = 0;
	while(n){
		char c = *(*p)++;
		--n;
		if(c == '#' && n >= 2){
			c = (hexdigit((*p)[0]) << 4) | hexdigit((*p)[1]);
			*p += 2;
			n -= 2;
		}else if(is_pdf_white(c) || is_pdf_delim(c))
			break;
		if(outbuf)
			*outbuf++ = c;
		++cnt;
	}
	return cnt;
}
