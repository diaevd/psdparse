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

extern void ed_versdesc(psd_file_t f, int level, int printxml, struct dictentry *parent);

static void ir_resolution(psd_file_t f, int level, int len, struct dictentry *parent){
	double hresd, vresd;
	extern long hres, vres; // fixed point values
	
	hresd = FIXEDPT(hres = get4B(f));
	get2B(f);
	get2B(f);
	vresd = FIXEDPT(vres = get4B(f));
	fprintf(xml, " <HRES>%g</HRES> <VRES>%g</VRES> ", hresd, vresd);
	UNQUIET("    Resolution %g x %g pixels per inch\n", hresd, vresd);
}

#define BYTESPERLINE 16

static void ir_dump(psd_file_t f, int level, int len, struct dictentry *parent){
	unsigned char row[BYTESPERLINE];
	int i, n;

	for(; len; len -= n){
		n = len < BYTESPERLINE ? len : BYTESPERLINE;
		fread(row, 1, n, f);
		for(i = 0; i < n; ++i)
			VERBOSE("%02x ", row[i]);
		putchar('\n');
	}
}

static void ir_raw(psd_file_t f, int level, int len, struct dictentry *parent){
	while(len--)
		fputc(fgetc(f), xml);
}

static void ir_pstring(psd_file_t f, int level, int len, struct dictentry *parent){
	fputsxml(getpstr(f), xml);
}

static void ir_pstrings(psd_file_t f, int level, int len, struct dictentry *parent){
	char *s;

	for(; len > 1; len -= strlen(s)+1){        // this loop will do the wrong thing
		fprintf(xml, "%s<NAME>", tabs(level)); // if any string contains NUL byte
		s = getpstr(f);
		fputsxml(s, xml);
		fputs("</NAME>\n", xml);
		VERBOSE("    %s\n", s);
	}
}

static void ir_1byte(psd_file_t f, int level, int len, struct dictentry *parent){
	fprintf(xml, "%d", fgetc(f));
}

static void ir_2byte(psd_file_t f, int level, int len, struct dictentry *parent){
	fprintf(xml, "%d", get2B(f));
}

static void ir_4byte(psd_file_t f, int level, int len, struct dictentry *parent){
	fprintf(xml, "%ld", get4B(f));
}

static void ir_digest(psd_file_t f, int level, int len, struct dictentry *parent){
	while(len--)
		fprintf(xml, "%02x", fgetc(f));
}

static void ir_pixelaspect(psd_file_t f, int level, int len, struct dictentry *parent){
	int v = get4B(f);
	double ratio = getdoubleB(f);
	fprintf(xml, " <VERSION>%d</VERSION> <RATIO>%g</RATIO> ", v, ratio);
	UNQUIET("    (Version = %d, Ratio = %g)\n", v, ratio);
}

static void ir_unicodestr(psd_file_t f, int level, int len, struct dictentry *parent){
	long count = get4B(f);
	while(count--)
		fputcxml(get2Bu(f), xml);
}

static void ir_gridguides(psd_file_t f, int level, int len, struct dictentry *parent){
	const char *indent = tabs(level);
	long v = get4B(f), gv = get4B(f), gh = get4B(f);
	long i, n = get4B(f);
	fprintf(xml, "%s<VERSION>%ld</VERSION>\n", indent, v);
	// Note that these quantities are "document coordinates".
	// This is not documented, but appears to mean fixed point with 5 fraction bits,
	// so we divide by 32 to obtain pixel position.
	fprintf(xml, "%s<GRIDCYCLE> <V>%g</V> <H>%g</H> </GRIDCYCLE>\n", indent, gv/32., gh/32.);
	fprintf(xml, "%s<GUIDES>\n", indent);
	for(i = n; i--;){
		long ord = get4B(f);
		char c = fgetc(f) ? 'H' : 'V';
		fprintf(xml, "%s\t<%cGUIDE>%g</%cGUIDE>\n", indent, c, ord/32., c);
	}
	fprintf(xml, "%s</GUIDES>\n", indent);
}

// id, key, tag, desc, func
static struct dictentry rdesc[] = {
	{1000, NULL, NULL, "PS2.0 mode data", NULL},
	{1001, NULL, NULL, "Macintosh print record", NULL},
	{1003, NULL, NULL, "PS2.0 indexed color table", NULL},
	{1005, NULL, "-RESOLUTION", "ResolutionInfo", ir_resolution},
	{1006, NULL, "ALPHA", "Alpha names", ir_pstrings},
	{1007, NULL, NULL, "DisplayInfo", NULL},
	{1008, NULL, "-CAPTION", "Caption", ir_pstring},
	{1009, NULL, NULL, "Border information", NULL},
	{1010, NULL, NULL, "Background color", NULL},
	{1011, NULL, NULL, "Print flags", NULL},
	{1012, NULL, NULL, "Grayscale/multichannel halftoning info", NULL},
	{1013, NULL, NULL, "Color halftoning info", NULL},
	{1014, NULL, NULL, "Duotone halftoning info", NULL},
	{1015, NULL, NULL, "Grayscale/multichannel transfer function", NULL},
	{1016, NULL, NULL, "Color transfer functions", NULL},
	{1017, NULL, NULL, "Duotone transfer functions", NULL},
	{1018, NULL, NULL, "Duotone image info", NULL},
	{1019, NULL, NULL, "B&W values for the dot range", NULL},
	{1021, NULL, NULL, "EPS options", NULL},
	{1022, NULL, NULL, "Quick Mask info", NULL},
	{1024, NULL, "-TARGETLAYER", "Layer state info", ir_2byte},
	{1025, NULL, NULL, "Working path", NULL},
	{1026, NULL, NULL, "Layers group info", NULL},
	{1028, NULL, NULL, "IPTC-NAA record (File Info)", NULL},
	{1029, NULL, NULL, "Image mode for raw format files", NULL},
	{1030, NULL, NULL, "JPEG quality", NULL},
	// v4.0
	{1032, NULL, "GRIDGUIDES", "Grid and guides info", ir_gridguides},
	{1033, NULL, NULL, "Thumbnail resource", NULL},
	{1034, NULL, "-COPYRIGHTFLAG", "Copyright flag", ir_1byte},
	{1035, NULL, "-URL", "URL", ir_raw}, // incorrectly documented as Pascal string
	// v5.0
	{1036, NULL, NULL, "Thumbnail resource (5.0)", NULL},
	{1037, NULL, "-GLOBALANGLE", "Global Angle", ir_4byte},
	{1038, NULL, NULL, "Color samplers resource", NULL},
	{1039, NULL, "ICC34", "ICC Profile", ir_icc34profile},
	{1040, NULL, "-WATERMARK", "Watermark", ir_1byte},
	{1041, NULL, "-ICCUNTAGGED", "ICC Untagged Profile", ir_1byte},
	{1042, NULL, "-EFFECTSVISIBLE", "Effects visible", ir_1byte},
	{1043, NULL, NULL, "Spot Halftone", NULL},
	{1044, NULL, "-DOCUMENTIDSEED", "Document specific IDs", ir_4byte},
	{1045, NULL, NULL, "Unicode Alpha Names", NULL},
	// v6.0
	{1046, NULL, "-COLORTABLECOUNT", "Indexed Color Table Count", ir_2byte},
	{1047, NULL, "-TRANSPARENTINDEX", "Transparent Index", ir_2byte},
	{1049, NULL, "-GLOBALALTITUDE", "Global Altitude", ir_4byte},
	{1050, NULL, NULL, "Slices", NULL},
	{1051, NULL, "-WORKFLOWURL", "Workflow URL", ir_unicodestr},
	{1052, NULL, NULL, "Jump To XPEP", NULL},
	{1053, NULL, NULL, "Alpha Identifiers", NULL},
	{1054, NULL, NULL, "URL List", NULL},
	{1057, NULL, NULL, "Version Info", NULL},
	// v7.0 - from CS doc
	{1058, NULL, NULL, "EXIF data 1", NULL},
	{1059, NULL, NULL, "EXIF data 3", NULL},
	{1060, NULL, "XMP", "XMP metadata", ir_raw},
	{1061, NULL, "-CAPTIONDIGEST", "Caption digest (RSA MD5)", ir_digest},
	{1062, NULL, NULL, "Print scale", NULL},
	// CS
	{1064, NULL, "-PIXELASPECTRATIO", "Pixel aspect ratio", ir_pixelaspect},
	{1065, NULL, "LAYERCOMPS", "Layer comps", NULL /*ed_versdesc*/},
	{1066, NULL, NULL, "Alternate duotone colors", NULL},
	{1067, NULL, NULL, "Alternate spot colors", NULL},
	
	{2999, NULL, "-CLIPPINGPATH", "Name of clipping path", ir_pstring},
	{10000,NULL, NULL, "Print flags info", NULL},
	{-1, NULL, NULL, NULL, NULL}
};

static struct dictentry *findbyid(int id){
	static struct dictentry path = {0, NULL, "PATH", "Path", NULL};
	struct dictentry *d;

	/* dumb linear lookup of description string from resource id */
	/* assumes array ends with a NULL desc pointer */
	if(id >= 2000 && id < 2999)
		return &path; // special case
	for(d = rdesc; d->desc; ++d)
		if(d->id == id)
			return d;
	return NULL;
}

static long doirb(psd_file_t f){
	static struct dictentry resource = {0, NULL, "RESOURCE", "dummy", NULL};
	char type[4], name[0x100];
	int id, namelen;
	long size;
	struct dictentry *d;

	fread(type, 1, 4, f);
	id = get2B(f);
	namelen = fgetc(f);
	fread(name, 1, PAD2(1+namelen)-1, f);
	name[namelen] = 0;
	size = get4B(f);

	UNQUIET("  resource '%c%c%c%c' (%5d,\"%s\"):%5ld bytes",
			type[0],type[1],type[2],type[3], id, name, size);
	if( (d = findbyid(id)) ) 
		UNQUIET(" [%s]", d->desc);
	UNQUIET("\n");

	if(xml && d && d->tag){
		fprintf(xml, "\t<RESOURCE TYPE='%c%c%c%c' ID='%d'",
				type[0],type[1],type[2],type[3], id);
		if(namelen) fprintf(xml, " NAME='%s'", name);
		if(d->func){
			fputs(">\n", xml);
			entertag(f, 2, size, &resource, d); // HACK: abuse 'printxml' parameter
			fputs("\t</RESOURCE>\n\n", xml);
		}else{
			fputs(" />\n", xml);
		}
	}
	fseeko(f, PAD2(size), SEEK_CUR); // skip resource block data

	return 4+2+PAD2(1+namelen)+4+PAD2(size); /* returns total bytes in block */
}

void doimageresources(psd_file_t f){
	long len = get4B(f);
	VERBOSE("\nImage resources (%ld bytes):\n", len);
	while(len > 0)
		len -= doirb(f);
	if(len != 0) warn("image resources overran expected size by %d bytes\n", -len);
}
