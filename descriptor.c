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
	long i;

	fputs(" <STRING>", xml);
	for(i = count; i--;)
		fputcxml(fgetc(f), xml);
	fputs("</STRING>", xml);
}

static void desc_raw(psd_file_t f, int level, int printxml, struct dictentry *parent){
	ir_raw(f, level, get4B(f), parent);
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
		{0, "tdta", "ENGINEDATA", "Engine Data", desc_raw}, // undocumented
		{0, NULL, NULL, NULL, NULL}
	};
	char *k;
	struct dictentry *p;

	fprintf(xml, "%s<ITEM>\n", tabs(level));
	stringorid(f, level+1, "KEY");
	p = findbykey(f, level+1, itemdict, k = getkey(f), 1, 0);
	fprintf(xml, "%s</ITEM>\n", tabs(level));
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

// FIXME: this does not work yet. Trial and error needed, docs aren't good enough.

void descriptor(psd_file_t f, int level, int printxml, struct dictentry *parent){
	long count;

	desc_class(f, level, printxml, parent);
	count = get4B(f);
	fprintf(xml, "%s<!--count:%ld-->\n", tabs(level), count);
	while(count--)
		item(f, level+1);
	fprintf(xml, "%s</DESCRIPTOR>\n", tabs(level));
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
