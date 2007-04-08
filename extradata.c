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

/* 'Extra data' handling. *Work in progress*
 * 
 * There's guesswork and trial-and-error in here,
 * due to many errors and omissions in Adobe's documentation on this (PS6 SDK).
 * It's amazing that they would try to describe a hierarchical format
 * as a flat list of fields. Reminds me of Jasc's PSP format docs, too.
 * One assumes they don't really encourage people to try and USE the info.
 */

struct dictentry{
	char *key,*tag,*desc;
	void (*func)(FILE *f, FILE *xmlf, int printxml, struct dictentry *dict);
};

#define FIXEDPT(x) ((x)/65536.)

void ed_typetool(FILE *f, FILE *xmlfile, int printxml, struct dictentry *dict){
	int i, j, v = get2B(f), mark, type, script, facemark,
		autokern, charcount, selstart, selend, linecount, orient, align, style;
	double size, tracking, kerning, leading, baseshift, scaling, hplace, vplace;
	static char *coeff[] = {"XX","XY","YX","YY","TX","TY"}; // from CS doc

	if(printxml){
		fprintf(xmlfile, "\n\t\t\t<VERSION>%d</VERSION>\n", v);

		// read transform (6 doubles)
		fputs("\t\t\t<TRANSFORM>", xmlfile);
		for(i = 0; i < 6; ++i)
			fprintf(xmlfile, " <%s>%g</%s>", coeff[i], getdoubleB(f), coeff[i]);
		fputs(" </TRANSFORM>\n", xmlfile);
		
		// read font information
		v = get2B(f);
		fprintf(xmlfile, "\t\t\t<FONTINFOVERSION>%d</FONTINFOVERSION>\n", v);
		if(v <= 6){
			for(i = get2B(f); i--;){
				mark = get2B(f);
				type = get4B(f);
				fprintf(xmlfile, "\t\t\t<FACE MARK='%d' TYPE='%d' FONTNAME='%s'", mark, type, getpstr(f));
				fprintf(xmlfile, " FONTFAMILY='%s'", getpstr(f));
				fprintf(xmlfile, " FONTSTYLE='%s'", getpstr(f));
				script = get2B(f);
				fprintf(xmlfile, " SCRIPT='%d'>\n", script);
				
				// doc is unclear, but this may work:
				fputs("\t\t\t\t<DESIGNVECTOR>", xmlfile);
				for(j = get4B(f); j--;)
					fprintf(xmlfile, " <AXIS>%ld</AXIS>", get4B(f));
				fputs(" </DESIGNVECTOR>\n", xmlfile);

				fprintf(xmlfile, "\t\t\t</FACE>\n");
			}

			j = get2B(f);
			for(; j--;){
				mark = get2B(f);
				facemark = get2B(f);
				size = FIXEDPT(get4B(f)); // of course, this representation is undocumented
				tracking = FIXEDPT(get4B(f));
				kerning = FIXEDPT(get4B(f));
				leading = FIXEDPT(get4B(f));
				baseshift = FIXEDPT(get4B(f));
				autokern = fgetc(f);
				fprintf(xmlfile, "\t\t\t<STYLE MARK='%d' FACEMARK='%d' SIZE='%g' TRACKING='%g' KERNING='%g' LEADING='%g' BASESHIFT='%g' AUTOKERN='%d'",
						mark, facemark, size, tracking, kerning, leading, baseshift, autokern);
				if(v <= 5)
					fprintf(xmlfile, " EXTRA='%d'", fgetc(f));
				fprintf(xmlfile, " ROTATE='%d' />\n", fgetc(f));
			}

			type = get2B(f);
			scaling = FIXEDPT(get4B(f));
			charcount = get4B(f);
			hplace = FIXEDPT(get4B(f));
			vplace = FIXEDPT(get4B(f));
			selstart = get4B(f);
			selend = get4B(f);
			linecount = get2B(f);
			fprintf(xmlfile, "\t\t\t<TEXT TYPE='%d' SCALING='%g' CHARCOUNT='%d' HPLACEMENT='%g' VPLACEMENT='%g' SELSTART='%d' SELEND='%d'>\n",
					type, scaling, charcount, hplace, vplace, selstart, selend);
			for(i = linecount; i--;){
				char *buf;
				charcount = get4B(f);
				buf = malloc(charcount+1);
				orient = get2B(f);
				align = get2B(f);
				fprintf(xmlfile, "\t\t\t\t<LINE ORIENTATION='%d' ALIGNMENT='%d'>\n", orient, align);
				for(j = 0; j < charcount; ++j){
					wchar_t wc = buf[j] = get2B(f); // FIXME: this is not the right way to get ASCII
					style = get2B(f);
					fprintf(xmlfile, "\t\t\t\t\t<UNICODE STYLE='%d'>%04x</UNICODE>", style, wc); // FIXME
					//if(isprint(wc)) fprintf(xmlfile, " <!--%c-->", wc);
					fputc('\n', xmlfile);
				}
				buf[j] = 0;
				fputs("\t\t\t\t\t<ASCII>", xmlfile);
				fputsxml(buf, xmlfile);
				fputs("</ASCII>\n\t\t\t\t</LINE>\n", xmlfile);
				free(buf);
			}
			fputs("\t\t\t</TEXT>\n", xmlfile);
		}else
			fputs("\t\t\t<!-- don't know how to parse this version -->\n", xmlfile);
		fputs("\t\t", xmlfile);
	}else
		UNQUIET("    (Type tool, version = %d)\n", v);
}

void ed_unicodename(FILE *f, FILE *xmlfile, int printxml, struct dictentry *dict){
	unsigned long len = get4B(f); // character count, not byte count

	if(len > 0 && len < 1024){ // sanity check
		if(printxml) // FIXME: what's the right way to represent a Unicode string in XML? UTF-8?
			while(len--)
				fprintf(xmlfile,"%04x",get2B(f));
		else if(!quiet){
			fputs("    (Unicode name = '", stdout);
			while(len--)
				fputwc(get2B(f), stdout); // FIXME: not working
			fputs("')\n", stdout);
		}
	}
}

void ed_4byte(FILE *f, FILE *xmlfile, int printxml, struct dictentry *dict){
	unsigned long id = get4B(f);
	if(printxml)
		fprintf(xmlfile, "%lu", id);
	else
		UNQUIET("    (%s = %lu)\n", dict->desc, id);
}

void ed_annotation(FILE *f, FILE *xmlfile, int printxml, struct dictentry *dict){
	int i, j, major = get2B(f), minor = get2B(f), len, open, flags;
	char type[4], key[4];
	long datalen, len2;

	if(printxml){
		fprintf(xmlfile, "\n\t\t\t<VERSION MAJOR='%d' MINOR='%d' />\n", major, minor);
		for(i = get4B(f); i--;){
			len = get4B(f);
			fread(type, 1, 4, f);
			if(!memcmp(type, "txtA", 4))
				fprintf(xmlfile, "\t\t\t<TEXT");
			else if(!memcmp(type, "sndA", 4))
				fprintf(xmlfile, "\t\t\t<SOUND");
			else
				fprintf(xmlfile, "\t\t\t<UNKNOWN");
			open = fgetc(f);
			flags = fgetc(f);
			//optblocks = get2B(f);
			//icont = get4B(f);  iconl = get4B(f);  iconb = get4B(f);  iconr = get4B(f);
			//popupt = get4B(f); popupl = get4B(f); popupb = get4B(f); popupr = get4B(f);
			fseek(f, 2+16+16+10, SEEK_CUR); // skip this mundane stuff
			fprintf(xmlfile, " OPEN='%d' FLAGS='%d' AUTHOR='", open, flags);
			fputsxml(getpstr2(f), xmlfile);
			fputs("' NAME='", xmlfile);
			fputsxml(getpstr2(f), xmlfile);
			fputs("' MODDATE='", xmlfile);
			fputsxml(getpstr2(f), xmlfile);
			fputc('\'', xmlfile);

			len2 = get4B(f); // remaining bytes in annotation, from this field inclusive
			fread(key, 1, 4, f);
			datalen = get4B(f); //printf(" key=%c%c%c%c datalen=%d\n", key[0],key[1],key[2],key[3],datalen);
			if(!memcmp(key, "txtC", 4)){
				// Once again, the doc lets us down:
				// - it says "ASCII or Unicode," but doesn't say how each is distinguished;
				// - one might think it has something to do with the mysterious four bytes
				//   stuck to the beginning of the data.
				char *buf = malloc(datalen/2+1);
				fputs(">\n\t\t\t\t<UNICODE>", xmlfile);
				for(j = 0; j < datalen/2; ++j){
					wchar_t wc = buf[j] = get2B(f); // FIXME: this is not the right way to get ASCII
					fprintf(xmlfile, "%04x", wc);
				}
				buf[j] = 0;
				fputs("</UNICODE>\n\t\t\t\t<ASCII>", xmlfile);
				fputsxml(buf, xmlfile);
				fputs("</ASCII>\n\t\t\t</TEXT>\n", xmlfile);
				len2 -= datalen; // we consumed this much from the file
				free(buf);
			}else if(!memcmp(key, "sndM", 4)){
				// Perhaps the 'length' field is actually a sampling rate?
				// Documentation says something different, natch.
				fprintf(xmlfile, " RATE='%ld' BYTES='%ld' />\n", datalen, len2-12);
			}else
				fputs(" /> <!-- don't know -->\n", xmlfile);

			fseek(f, PAD4(len2-12), SEEK_CUR); // skip whatever's left of this annotation's data
		}
		fputs("\t\t", xmlfile);
	}else
		UNQUIET("    (Annotation, version = %d.%d)\n", major, minor);
}

void ed_1byte(FILE *f, FILE *xmlfile, int printxml, struct dictentry *dict){
	int k = fgetc(f);
	if(printxml)
		fprintf(xmlfile, "%d", k);
	else
		UNQUIET("    (%s = %d)\n", dict->desc, k);
}

void ed_referencepoint(FILE *f, FILE *xmlfile, int printxml, struct dictentry *dict){
	double x,y;

	x = getdoubleB(f);
	y = getdoubleB(f);
	if(printxml)
		fprintf(xmlfile, " <X>%g</X> <Y>%g</Y> ", x, y);
	else
		UNQUIET("    (Reference point X=%g Y=%g)\n", x, y);
}

void doextradata(FILE *f, long length, int printxml){
	struct extra_data extra;
	static struct dictentry extradict[] = {
		// v4.0
		{"levl", "LEVELS", "Levels", NULL},
		{"curv", "CURVES", "Curves", NULL},
		{"brit", "BRIGHTNESSCONTRAST", "Brightness/contrast", NULL},
		{"blnc", "COLORBALANCE", "Color balance", NULL},
		{"hue ", "HUESATURATION4", "Old Hue/saturation, Photoshop 4.0", NULL},
		{"hue2", "HUESATURATION5", "New Hue/saturation, Photoshop 5.0", NULL},
		{"selc", "SELECTIVECOLOR", "Selective color", NULL},
		{"thrs", "THRESHOLD", "Threshold", NULL},
		{"nvrt", "INVERT", "Invert", NULL},
		{"post", "POSTERIZE", "Posterize", NULL},
		// v5.0
		{"lrFX", "EFFECT", "Effects layer", NULL},
		{"tySh", "TYPETOOL5", "Type tool (5.0)", ed_typetool},
		{"TySh", "TYPETOOL6", "Type tool (6.0)", ed_typetool}, // from CS doc
		{"luni", "UNICODENAME", "Unicode layer name", ed_unicodename},
		{"lyid", "LAYERID", "Layer ID", ed_4byte},
		// v6.0
		{"lfx2", "OBJECTEFFECT", "Object based effects layer", NULL},
		{"Patt", "PATTERN", "Pattern", NULL},
		{"Anno", "ANNOTATION", "Annotation", ed_annotation},
		{"clbl", "BLENDCLIPPING", "Blend clipping", ed_1byte},
		{"infx", "BLENDINTERIOR", "Blend interior", ed_1byte},
		{"knko", "KNOCKOUT", "Knockout", ed_1byte},
		{"lspf", "PROTECTED", "Protected", ed_4byte},
		{"lclr", "SHEETCOLOR", "Sheet color", NULL},
		{"fxrp", "REFERENCEPOINT", "Reference point", ed_referencepoint},
		{"grdm", "GRADIENT", "Gradient", NULL},
		{"ffxi", "FOREIGNEFFECTID", "Foreign effect ID", ed_4byte}, // CS doc
		{"lnsr", "LAYERNAMESOURCE", "Layer name source", ed_4byte}, // CS doc
		// v7.0
		{"lyvr", "LAYERVERSION", "Layer version", ed_4byte}, // CS doc
		{"tsly", "TRANSPARENCYSHAPES", "Transparency shapes layer", ed_1byte}, // CS doc
		{"lmgm", "LAYERMASKASGLOBALMASK", "Layer mask as global mask", ed_1byte}, // CS doc
		{"vmgm", "VECTORMASKASGLOBALMASK", "Vector mask as global mask", ed_1byte}, // CS doc
		// CS
		{"lsct", "SECTION", "Section divider", ed_4byte},
		{NULL, NULL, NULL, NULL}
	};
	struct dictentry *d;

	while(length > 0){
		fread(extra.sig, 1, 4, f);
		fread(extra.key, 1, 4, f);
		extra.length = get4B(f);
		length -= 12 + extra.length;
		if(!memcmp(extra.sig, "8BIM", 4)){
			VERBOSE("    extra data: sig='%c%c%c%c' key='%c%c%c%c' length=%5lu\n",
					extra.sig[0],extra.sig[1],extra.sig[2],extra.sig[3],
					extra.key[0],extra.key[1],extra.key[2],extra.key[3],
					extra.length);
			for(d = extradict; d->key; ++d)
				if(!memcmp(extra.key, d->key, 4)){
					if(d->func){
						long savepos = ftell(f);
						if(printxml) fprintf(xmlfile, "\t\t<%s>", d->tag);
						d->func(f, xmlfile, printxml, d);
						if(printxml) fprintf(xmlfile, "</%s>\n", d->tag);
						fseek(f, savepos, SEEK_SET);
					}else{
						// there is no function to parse this block
						if(printxml)
							fprintf(xmlfile, "\t\t<%s /> <!-- not parsed -->\n", d->tag);
						else
							UNQUIET("    (%s data)\n", d->desc);
					}
					break;
				}
			
			fseek(f, extra.length, SEEK_CUR);
		}else{
			warn("bad signature in layer's extra data, skipping the rest");
			break;
		}
	}
}
