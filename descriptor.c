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

#ifdef HAVE_ICONV_H
	extern iconv_t ic;
#endif

static void desc_class(psd_file_t f, int level, int printxml, struct dictentry *parent);
static void desc_reference(psd_file_t f, int level, int printxml, struct dictentry *parent);
static void desc_list(psd_file_t f, int level, int printxml, struct dictentry *parent);
static void desc_double(psd_file_t f, int level, int printxml, struct dictentry *parent);
static void desc_unitfloat(psd_file_t f, int level, int printxml, struct dictentry *parent);
static void desc_unicodestr(psd_file_t f, int level, int printxml, struct dictentry *parent);
static void desc_enumerated(psd_file_t f, int level, int printxml, struct dictentry *parent);
static void desc_integer(psd_file_t f, int level, int printxml, struct dictentry *parent);
static void desc_boolean(psd_file_t f, int level, int printxml, struct dictentry *parent);
static void desc_alias(psd_file_t f, int level, int printxml, struct dictentry *parent);

static void ascii_string(psd_file_t f, long count){
	fputs(" <STRING>", xml);
	while(count--)
		fputcxml(fgetc(f), xml);
	fputs("</STRING>", xml);
}

// TODO: re-encode the embedded Unicode text strings as UTF-8;
//       perhaps parse out keys from PDF and emit some corresponding XML structure.

static void desc_pdf(psd_file_t f, int level, int printxml, struct dictentry *parent){
	long count = get4B(f);
	unsigned char *buf = malloc(count), *p, *q, *strbuf, c;

	if(buf){
		size_t inb = fread(buf, 1, count, f), cnt, n;

		for(p = buf, n = inb; n;){
			c = *p++;
			--n;
			switch(c){
			case '(':
				// check parsed string length
				q = p;
				cnt = pdf_string(&q, NULL, n);

				// parse string into new buffer, and step past in source buffer
				strbuf = malloc(cnt);
				q = p;
				pdf_string(&p, strbuf, n);
				n -= p - q;

				//fprintf(stderr, "pdf string: %lu (", cnt);
				//fwrite(strbuf, 1, cnt, stderr);
				//fputs(")\n", stderr);

#ifdef HAVE_ICONV_H
				fprintf(xml, "%s<STRING>", tabs(level));

				if(cnt >= 2 && strbuf[0] == 0xfe && strbuf[1] == 0xff){
					size_t inb, outb;
					const char *inbuf;
					char *outbuf, *utfbuf;

					iconv(ic, NULL, &inb, NULL, &outb); // reset iconv state

					// skip over the meaningless BOM
					inbuf = (char*)strbuf + 2;
					inb = cnt - 2;
					outb = 4*(cnt/2); // sloppy overestimate of buffer needed
					if( (utfbuf = malloc(outb)) ){
						outbuf = utfbuf;
						if(ic != (iconv_t)-1){
							if(iconv(ic, &inbuf, &inb, &outbuf, &outb) != (size_t)-1)
								fwrite(utfbuf, 1, outbuf-utfbuf, xml);
							else
								alwayswarn("iconv() failed, errno=%u\n", errno);
						}
						free(utfbuf);
					}
				}else
					fwrite(strbuf, 1, cnt, xml); // not UTF; should be PDFDocEncoded
				fputs("</STRING>\n", xml);
#endif
				free(strbuf);
				break;
			case '<':
				if(n && *p == '<'){
					//fputs("dict: <<\n", stderr);
					++p;
					--n;
				}
				else{
					// TODO: hex string
					while(n && *p != '>')
						++p;
				}
				break;
			case '>':
				if(n && *p == '>'){
					//fputs("      >>\n", stderr);
					++p;
					--n;
				}
				break;
			case '/':
				// check parsed name length
				q = p;
				cnt = pdf_name(&q, NULL, n);

				// parse name into new buffer, and step past in source buffer
				strbuf = malloc(cnt);
				q = p;
				pdf_name(&p, strbuf, n);
				n -= p - q;

				//fprintf(stderr, "pdf name: %lu /", cnt);
				//fwrite(strbuf, 1, cnt, stderr);
				//fputs("\n", stderr);

				// name should be treated as UTF-8
				/*
				fprintf(xml, "%s<NAME>", tabs(level));
				fwrite(strbuf, 1, cnt, xml);
				fputs("</NAME>\n", xml);
				*/
				free(strbuf);
				break;
			}
		}

		fprintf(xml, "%s<RAW><![CDATA[", tabs(level));
		fwrite(buf, 1, inb, xml);
		fputs("]]></RAW>\n", xml);

		free(buf);
	}
}

static void stringorid(psd_file_t f, int level, char *tag){
	long count = get4B(f);
	fprintf(xml, "%s<%s>", tabs(level), tag);
	if(count)
		ascii_string(f, count);
	else{
		fputs(" <ID>", xml);
		fputsxml(getkey(f), xml);
		fputs("</ID>", xml);
	}
	fprintf(xml, " </%s>\n", tag);
}

static void ref_property(psd_file_t f, int level, int printxml, struct dictentry *parent){
	desc_class(f, level, printxml, parent);
	stringorid(f, level, "KEY");
}

static void ref_enumref(psd_file_t f, int level, int printxml, struct dictentry *parent){
	desc_class(f, level, printxml, parent);
	desc_enumerated(f, level, printxml, parent);
}

static void ref_offset(psd_file_t f, int level, int printxml, struct dictentry *parent){
	desc_class(f, level, printxml, parent);
	desc_integer(f, level, printxml, parent);
}

static void desc_class(psd_file_t f, int level, int printxml, struct dictentry *parent){
	desc_unicodestr(f, level, printxml, parent);
	stringorid(f, level, "CLASS");
}

static void desc_reference(psd_file_t f, int level, int printxml, struct dictentry *parent){
	static struct dictentry refdict[] = {
		// all functions must be present, for this to parse correctly
		{0, "prop", "PROPERTY", "Property", ref_property},
		{0, "Clss", "CLASS", "Class", desc_class},
		{0, "Enmr", "ENUMREF", "Enumerated Reference", ref_enumref},
		{0, "rele", "-OFFSET", "Offset", ref_offset}, // '-' prefix means keep tag value on one line
		{0, "Idnt", "IDENTIFIER", "Identifier", NULL}, // doc is missing?!
		{0, "indx", "INDEX", "Index", NULL}, // doc is missing?!
		{0, "name", "NAME", "Name", NULL}, // doc is missing?!
		{0, NULL, NULL, NULL, NULL}
	};
	long count = get4B(f);
	while(count-- && findbykey(f, level, refdict, getkey(f), printxml, 0))
		;
}

struct dictentry *item(psd_file_t f, int level){
	static struct dictentry itemdict[] = {
		{0, "obj ", "REFERENCE", "Reference", desc_reference},
		{0, "Objc", "DESCRIPTOR", "Descriptor", descriptor}, // doc missing?!
		{0, "list", "LIST", "List", desc_list}, // not documented?
		{0, "VlLs", "LIST", "List", desc_list},
		{0, "doub", "-DOUBLE", "Double", desc_double}, // '-' prefix means keep tag value on one line
		{0, "UntF", "-UNITFLOAT", "Unit float", desc_unitfloat},
		{0, "TEXT", "-STRING", "String", desc_unicodestr},
		{0, "enum", "ENUMERATED", "Enumerated", desc_enumerated}, // Enmr? (see v6 rel2)
		{0, "long", "-INTEGER", "Integer", desc_integer},
		{0, "bool", "-BOOLEAN", "Boolean", desc_boolean},
		{0, "GlbO", "GLOBALOBJECT", "GlobalObject same as Descriptor", descriptor},
		{0, "type", "CLASS", "Class", desc_class},  // doc missing?! - Clss? (see v6 rel2)
		{0, "GlbC", "CLASS", "Class", desc_class}, // doc missing?!
		{0, "alis", "-ALIAS", "Alias", desc_alias},
		{0, "tdta", "ENGINEDATA", "Engine Data", desc_pdf}, // undocumented, apparently PDF syntax data
		{0, NULL, NULL, NULL, NULL}
	};
	char *k;
	struct dictentry *p;

	stringorid(f, level, "KEY");
	p = findbykey(f, level, itemdict, k = getkey(f), 1, 0);

	if(!p){
		fprintf(stderr, "### item(): unknown key '%s'; file offset %#lx\n",
				k, (unsigned long)ftell(f));
		exit(1);
	}
	return p;
}

static void desc_list(psd_file_t f, int level, int printxml, struct dictentry *parent){
	long count = get4B(f);
	while(count--)
		item(f, level);
}

void descriptor(psd_file_t f, int level, int printxml, struct dictentry *parent){
	long count;

	desc_class(f, level, printxml, parent);
	count = get4B(f);
	fprintf(xml, "%s<!--count:%ld-->\n", tabs(level), count);
	while(count--)
		item(f, level);
}

static void desc_double(psd_file_t f, int level, int printxml, struct dictentry *parent){
	fprintf(xml, "%g", getdoubleB(f));
};

static void desc_unitfloat(psd_file_t f, int level, int printxml, struct dictentry *parent){
	static struct dictentry ufdict[] = {
		{0, "#Ang", "-ANGLE", "angle: base degrees", desc_double},
		{0, "#Rsl", "-DENSITY", "density: base per inch", desc_double},
		{0, "#Rlt", "-DISTANCE", "distance: base 72ppi", desc_double},
		{0, "#Nne", "-NONE", "none: coerced", desc_double},
		{0, "#Prc", "-PERCENT", "percent: tagged unit value", desc_double},
		{0, "#Pxl", "-PIXELS", "pixels: tagged unit value", desc_double},
		{0, NULL, NULL, NULL, NULL}
	};

	findbykey(f, level, ufdict, getkey(f), 1, 0); // FIXME: check for NULL return
}

static void desc_unicodestr(psd_file_t f, int level, int printxml, struct dictentry *parent){
	long count = get4B(f);
	fprintf(xml, "%s<UNICODE>", parent->tag[0] == '-' ? " " : tabs(level));
	while(count--)
		fputcxml(get2Bu(f), xml);
	fprintf(xml, "</UNICODE>%c", parent->tag[0] == '-' ? ' ' : '\n');
}

static void desc_enumerated(psd_file_t f, int level, int printxml, struct dictentry *parent){
	stringorid(f, level, "TYPE");
	stringorid(f, level, "ENUM");
}

static void desc_integer(psd_file_t f, int level, int printxml, struct dictentry *parent){
	fprintf(xml, " <INTEGER>%ld</INTEGER> ", get4B(f));
}

static void desc_boolean(psd_file_t f, int level, int printxml, struct dictentry *parent){
	fprintf(xml, " <BOOLEAN>%d</BOOLEAN> ", fgetc(f));
}

static void desc_alias(psd_file_t f, int level, int printxml, struct dictentry *parent){
	psd_bytes_t count = get4B(f);
	fprintf(xml, " <!-- %lu bytes alias data --> ", (unsigned long)count);
	fseeko(f, count, SEEK_CUR); // skip over
}
