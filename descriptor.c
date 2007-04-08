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

#include <wchar.h>

#include "psdparse.h"

void desc_class(FILE *f, int printxml, struct dictentry *dict);
void desc_reference(FILE *f, int printxml, struct dictentry *dict);
void desc_list(FILE *f, int printxml, struct dictentry *dict);
void desc_double(FILE *f, int printxml, struct dictentry *dict);
void desc_unitfloat(FILE *f, int printxml, struct dictentry *dict);
void desc_unicodestr(FILE *f, int printxml, struct dictentry *dict);
void desc_enumerated(FILE *f, int printxml, struct dictentry *dict);
void desc_integer(FILE *f, int printxml, struct dictentry *dict);
void desc_boolean(FILE *f, int printxml, struct dictentry *dict);
void desc_alias(FILE *f, int printxml, struct dictentry *dict);

void stringorid(FILE *f, char *tag){
	long count = get4B(f);
	fprintf(xmlfile, "\t\t\t\t<%s>", tag);
	if(count){
		fprintf(xmlfile, "\t\t\t\t\t<STRING>");
		while(count--)
			fputcxml(fgetc(f), xmlfile);
		fprintf(xmlfile, "</STRING>\n");
	}else
		fprintf(xmlfile, "\t\t\t\t\t<ID>%ld</ID>\n", get4B(f));
	fprintf(xmlfile, "\t\t\t\t</%s>", tag);
}

void ref_property(FILE *f, int printxml, struct dictentry *dict){
	desc_class(f, printxml, dict);
	stringorid(f, "KEY");
}

void ref_enumref(FILE *f, int printxml, struct dictentry *dict){
	desc_class(f, printxml, dict);
	desc_enumerated(f, printxml, dict);
}

void ref_offset(FILE *f, int printxml, struct dictentry *dict){
	desc_class(f, printxml, dict);
	desc_integer(f, printxml, dict);
}

void desc_class(FILE *f, int printxml, struct dictentry *dict){
	desc_unicodestr(f, printxml, dict);
	stringorid(f, "CLASS");
}

void desc_reference(FILE *f, int printxml, struct dictentry *dict){
	static struct dictentry refdict[] = {
		// all functions must be present, for this to parse correctly
		{0, "prop", "PROPERTY", "Property", ref_property},
		{0, "Clss", "CLASS", "Class", desc_class},
		{0, "Enmr", "ENUMREF", "Enumerated Reference", ref_enumref},
		{0, "rele", "OFFSET", "Offset", ref_offset},
		{0, "Idnt", "IDENTIFIER", "Identifier", NULL}, // doc is missing?!
		{0, "indx", "INDEX", "Index", NULL}, // doc is missing?!
		{0, "name", "NAME", "Name", NULL}, // doc is missing?!
		{0, NULL, NULL, NULL, NULL}
	};
	long count = get4B(f);
	while(count--){
		char key[4];
		fread(key, 1, 4, f);
		findbykey(f, refdict, key, printxml); // FIXME: if this returns NULL, we got problems
	}
}

void keytype(FILE *f){
	static struct dictentry descdict[] = {
		{0, "obj ", "REFERENCE", "Reference", desc_reference},
		{0, "Objc", "DESCRIPTOR", "Descriptor", NULL}, // doc missing?!
		{0, "VlLs", "LIST", "List", desc_list},
		{0, "doub", "DOUBLE", "Double", desc_double},
		{0, "UntF", "UNITFLOAT", "Unit float", desc_unitfloat},
		{0, "TEXT", "STRING", "String", desc_unicodestr},
		{0, "enum", "ENUMERATED", "Enumerated", desc_enumerated}, // Enmr? (see v6 rel2)
		{0, "long", "INTEGER", "Integer", desc_integer},
		{0, "bool", "BOOLEAN", "Boolean", desc_boolean},
		{0, "GlbO", "GLOBALOBJECT", "GlobalObject same as Descriptor", NULL},
		{0, "type", "CLASS", "Class", NULL},  // doc missing?! - Clss? (see v6 rel2)
		{0, "GlbC", "CLASS", "Class", NULL}, // doc missing?!
		{0, "alis", "ALIAS", "Alias", desc_alias},
		{0, NULL, NULL, NULL, NULL}
	};
	char key[4];

	fread(key, 1, 4, f);
	findbykey(f, descdict, key, 1);
}

void desc_list(FILE *f, int printxml, struct dictentry *dict){
	long count = get4B(f);
	while(count--)
		keytype(f);
}

void descriptor(FILE *f, int printxml, struct dictentry *dict){
	long j, count;

	desc_class(f, printxml, dict);
	count = get4B(f);
	while(count--){
		j = get4B(f);
		if(j){
			fputs("\t\t\t\t<ITEM KEY='", xmlfile);
			while(j--)
				fputcxml(fgetc(f), xmlfile);
			fputs("' />\n", xmlfile);
		}else
			keytype(f);
	}
}

void desc_double(FILE *f, int printxml, struct dictentry *dict){
	fprintf(xmlfile, "%g", getdoubleB(f));
};

void desc_unitfloat(FILE *f, int printxml, struct dictentry *dict){
	static struct dictentry ufdict[] = {
		{0, "#Ang", "ANGLE", "angle: base degrees", desc_double},
		{0, "#Rsl", "DENSITY", "density: base per inch", desc_double},
		{0, "#Rlt", "DISTANCE", "distance: base 72ppi", desc_double},
		{0, "#Nne", "NONE", "none: coerced", desc_double},
		{0, "#Prc", "PERCENT", "percent: tagged unit value", desc_double},
		{0, "#Pxl", "PIXELS", "pixels: tagged unit value", desc_double},
		{0, NULL, NULL, NULL, NULL}
	};
	char key[4];
	
	fread(key, 1, 4, f);
	findbykey(f, ufdict, key, 1); // FIXME: check for NULL return
}

void desc_unicodestr(FILE *f, int printxml, struct dictentry *dict){
	long count = get4B(f);
	if(count){
		fputs("\n\t\t\t\t<UNICODE>", xmlfile);
		while(count--)
			fprintf(xmlfile, "%04x", get2B(f));
		fputs("</UNICODE>\n", xmlfile);
	}
}

void desc_enumerated(FILE *f, int printxml, struct dictentry *dict){
	stringorid(f, "TYPE");
	stringorid(f, "ENUM");
}

void desc_integer(FILE *f, int printxml, struct dictentry *dict){
	fprintf(xmlfile, " <VALUE>%ld</VALUE> ", get4B(f));
}

void desc_boolean(FILE *f, int printxml, struct dictentry *dict){
	fprintf(xmlfile, " <VALUE>%d</VALUE> ", fgetc(f));
}

void desc_alias(FILE *f, int printxml, struct dictentry *dict){
	long count = get4B(f);
	fprintf(xmlfile, " <!-- %ld bytes alias data --> ", count);
	fseek(f, count, SEEK_CUR); // skip over
}

// CS doc
void ed_descriptor(FILE *f, int printxml, struct dictentry *dict){
	if(printxml)
		descriptor(f, printxml, dict); // TODO: pass flag to extract value data
}
