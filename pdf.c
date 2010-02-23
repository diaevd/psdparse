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

#define MAX_NAMES 32
#define MAX_DICTS 32 // dict/array nesting limit

int is_pdf_white(char c){
	return c == '\000' || c == '\011' || c == '\012'
		|| c == '\014' || c == '\015' || c == '\040';
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

size_t pdf_string(char **p, char *outbuf, size_t n){
	int paren = 1;
	size_t cnt;

	for(cnt = 0; n;){
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
size_t pdf_hexstring(char **p, char *outbuf, size_t n){
	size_t cnt, flag;
	unsigned acc;

	for(cnt = acc = flag = 0; n;){
		char c = *(*p)++;
		--n;
		if(c == '>'){ // or should this be pdf_delim() ?
			// check for partial byte
			if(flag){
				if(outbuf)
					*outbuf++ = acc;
				++cnt;
			}
			break;
		}else if(!is_pdf_white(c)){ // N.B. DOES NOT CHECK for valid hex digits!
			acc |= hexdigit(c);
			if(flag){
				// both nibbles loaded; emit character
				if(outbuf)
					*outbuf++ = acc;
				++cnt;
				flag = acc = 0;
			}else{
				acc <<= 4;
				flag = 1; // high nibble loaded
			}
		}
	}
	return cnt;
}

// parameters analogous to pdf_string()'s
size_t pdf_name(char **p, char *outbuf, size_t n){
	size_t cnt;

	for(cnt = 0; n;){
		char c = *(*p);
		if(is_pdf_white(c) || is_pdf_delim(c))
			break;
		else if(c == '#' && n >= 3){
			// process #XX hex escape
			c = (hexdigit((*p)[1]) << 4) | hexdigit((*p)[2]);
			*p += 3;
			n -= 3;
		}else{
			// consume character
			++(*p);
			--n;
		}
		if(outbuf)
			*outbuf++ = c;
		++cnt;
	}
	return cnt;
}

static char *name_stack[MAX_NAMES];
static unsigned name_tos, in_array;

void push_name(const char *indent, char *tag){
	if(name_tos == MAX_NAMES)
		fatal("name stack overflow");
	name_stack[name_tos++] = tag;

	fprintf(xml, "%s<%s>", indent, tag);
}

void pop_name(const char *indent){
	if(name_tos){
		fprintf(xml, "%s</%s>\n", indent, name_stack[--name_tos]);
		free(name_stack[name_tos]);
	}
	else
		warn_msg("pop_name(): underflow");
}

void begin_value(const char *indent){
	if(in_array)
		fprintf(xml, "%s<e>", indent);
}

void end_value(){
	if(in_array)
		fputs("</e>\n", xml);
	else
		pop_name("");
}

// Write a string representation to XML. Either convert to UTF-8
// from the UTF-16BE if flagged as such by a BOM prefix,
// or just write the literal string bytes without transliteration.

/*
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

void stringxml(const char *indent, unsigned char *strbuf, size_t cnt){
	begin_value(indent);

	if(cnt >= 2 && strbuf[0] == 0xfe && strbuf[1] == 0xff){
#ifdef HAVE_ICONV_H
		size_t inb, outb;
		char *inbuf, *outbuf, *utf8;

		iconv(ic, NULL, &inb, NULL, &outb); // reset iconv state

		outb = 6*(cnt/2); // sloppy overestimate of buffer (FIXME)
		if( (utf8 = malloc(outb)) ){
			// skip the meaningless BOM
			inbuf = (char*)strbuf + 2;
			inb = cnt - 2;
			outbuf = utf8;
			if(ic != (iconv_t)-1){
				if(iconv(ic, &inbuf, &inb, &outbuf, &outb) != (size_t)-1){
					fputs("<![CDATA[", xml);
					fwrite(utf8, 1, outbuf-utf8, xml);
					fputs("]]>", xml);
				}else
					alwayswarn("stringxml(): iconv() failed, errno=%u\n", errno);
			}
			free(utf8);
		}
#endif
	}else
		fputsxml((char*)strbuf, xml); // not UTF; should be PDFDocEncoded

	end_value();
}

// Implements a "ghetto" PDF syntax parser - just the minimum needed
// to translate Photoshop's embedded type tool data into XML.

// PostScript implements a single heterogenous stack; we don't try
// to emulate proper behaviour here but rather keep a 'stack' of names
// only in order to generate correct closing tags,
// and remember whether the 'current' object is a dictionary or array.

// I've arbitrarily chosen to represent 'anonymous' dictionaries
// <d></d> and array elements by <e></e>

static void pdf_data(char *buf, size_t n, int level){
	char *p, *q, *strbuf, c;
	size_t cnt;
	unsigned is_array[MAX_DICTS], dict_tos = 0;

	name_tos = in_array = 0;
	for(p = buf; n;){
		c = *p++;
		--n;
		switch(c){
		case '(':
			// check parsed string length
			q = p;
			cnt = pdf_string(&q, NULL, n);

			// parse string into new buffer, and step past in source buffer
			strbuf = malloc(cnt+1);
			q = p;
			pdf_string(&p, strbuf, n);
			n -= p - q;
			strbuf[cnt] = 0;

			stringxml(tabs(level), (unsigned char*)strbuf, cnt);

			free(strbuf);
			break;

		case '<':
			if(n && *p == '<'){
				++p;
				--n;
				// if beginning a dictionary inside an array, there is
				// no name to use.
				if(in_array)
					push_name(tabs(level), strdup("d"));
		case '[':
				if(name_tos){ // only if a name has opened an element
					++level;
					fputc('\n', xml);
				}
				if(dict_tos == MAX_DICTS)
					fatal("dict stack overflow");
				is_array[dict_tos++] = in_array = c == '[';
			}
			else{ // hex string literal
				q = p;
				cnt = pdf_hexstring(&q, NULL, n);

				strbuf = malloc(cnt+1);
				q = p;
				pdf_hexstring(&p, strbuf, n);
				n -= p - q;
				strbuf[cnt] = 0;

				stringxml(tabs(level), (unsigned char*)strbuf, cnt);

				free(strbuf);
			}
			break;

		case '>':
			if(n && *p == '>'){
				++p;
				--n;
		case ']':
				if(dict_tos)
					--dict_tos;
				else
					warn_msg("dict stack underflow");
				in_array = dict_tos && is_array[dict_tos-1];
				pop_name(tabs(--level));
			}
			break;

		case '/':
			// check parsed name length
			q = p;
			cnt = pdf_name(&q, NULL, n);

			// parse name into new buffer, and step past in source buffer
			strbuf = malloc(cnt+1);
			q = p;
			pdf_name(&p, strbuf, n);
			strbuf[cnt] = 0;
			n -= p - q;

			// if a name occurs inside an array, emit a single empty element
			if(in_array)
				fprintf(xml, "%s<%s/>\n", tabs(level), strbuf);
			else // otherwise it's a dictionary key; open an element
				push_name(tabs(level), (char*)strbuf);
			break;

		case '%': // skip comment
			while(n && *p != '\012' && *p != '\015'){
				++p;
				--n;
			}
			break;

		default:
			if(!is_pdf_white(c)){
				// probably numeric or boolean literal
				begin_value(tabs(level));

				// copy characters until whitespace or delimiter
				fputcxml(c, xml);
				while(n && !is_pdf_white(*p) && !is_pdf_delim(*p)){
					fputcxml(*p++, xml);
					--n;
				}

				end_value();
			}
			break;
		}
	}

	// close any open elements (should not happen)
	while(name_tos){
		warn_msg("unclosed element %s", name_stack[name_tos-1]);
		pop_name(tabs(--level));
	}
}

void desc_pdf(psd_file_t f, int level, int printxml, struct dictentry *parent){
	long count = get4B(f);
	char *buf = malloc(count);

	if(buf){
		/* The raw PDF data is not valid UTF-8 and may break XML parse.
		fprintf(xml, "%s<RAW><![CDATA[", tabs(level));
		fwrite(buf, 1, count, xml);
		fputs("]]></RAW>\n", xml); */

		pdf_data(buf, fread(buf, 1, count, f), level);

		free(buf);
	}
}
