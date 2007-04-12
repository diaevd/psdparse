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

extern void ed_versdesc(FILE *f, int level, int printxml, struct dictentry *parent);

void ir_resolution(FILE *f, int level, int len, struct dictentry *parent){
	double hres, vres;
	
	hres = FIXEDPT(get4B(f));
	get2B(f);
	get2B(f);
	vres = FIXEDPT(get4B(f));
	fprintf(xml, " <HRES>%g</HRES> <VRES>%g</VRES> ", hres, vres);
	UNQUIET("    Resolution %g x %g pixels per inch\n", hres, vres);
}

void ir_pstring(FILE *f, int level, int len, struct dictentry *parent){
	fputsxml(getpstr(f), xml);
}

void ir_1byte(FILE *f, int level, int len, struct dictentry *parent){
	fprintf(xml, "%d", fgetc(f));
}

void ir_2byte(FILE *f, int level, int len, struct dictentry *parent){
	fprintf(xml, "%d", get2B(f));
}

void ir_4byte(FILE *f, int level, int len, struct dictentry *parent){
	fprintf(xml, "%ld", get4B(f));
}

void ir_digest(FILE *f, int level, int len, struct dictentry *parent){
	while(len--)
		fprintf(xml, "%02x", fgetc(f));
}

void ir_pixelaspect(FILE *f, int level, int len, struct dictentry *parent){
	int v = get4B(f);
	double ratio = getdoubleB(f);
	fprintf(xml, " <VERSION>%d</VERSION> <RATIO>%g</RATIO> ", v, ratio);
	UNQUIET("    (Version = %d, Ratio = %g)\n", v, ratio);
}

void ir_unicodestr(FILE *f, int level, int len, struct dictentry *parent){
	long count = get4B(f);
	while(count--)
		fprintf(xml, "%04x", get2B(f));
}

// id, key, tag, desc, func
static struct dictentry rdesc[] = {
	{1000, NULL, NULL, "PS2.0 mode data", NULL},
	{1001, NULL, NULL, "Macintosh print record", NULL},
	{1003, NULL, NULL, "PS2.0 indexed color table", NULL},
	{1005, NULL, "-RESOLUTION", "ResolutionInfo", ir_resolution},
	{1006, NULL, NULL, "Alpha names", NULL},
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
	{1032, NULL, NULL, "Grid and guides info", NULL},
	{1033, NULL, NULL, "Thumbnail resource", NULL},
	{1034, NULL, "-COPYRIGHTFLAG", "Copyright flag", ir_1byte},
	{1035, NULL, "-URL", "URL", ir_pstring},
	// v5.0
	{1036, NULL, NULL, "Thumbnail resource (5.0)", NULL},
	{1037, NULL, "-GLOBALANGLE", "Global Angle", ir_4byte},
	{1038, NULL, NULL, "Color samplers resource", NULL},
	{1039, NULL, NULL, "ICC Profile", NULL},
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
	{1060, NULL, NULL, "XMP metadata", NULL},
	{1061, NULL, "-CAPTIONDIGEST", "Caption digest (RSA MD5)", ir_digest},
	{1062, NULL, NULL, "Print scale", NULL},
	// CS
	{1064, NULL, "-PIXELASPECTRATIO", "Pixel aspect ratio", ir_pixelaspect},
	{1065, NULL, "LAYERCOMPS", "Layer comps", NULL /*ed_versdesc*/},
	{1066, NULL, NULL, "Alternate duotone colors", NULL},
	{1067, NULL, NULL, "Alternate spot colors", NULL},
	
	{2999, NULL, NULL, "Name of clipping path", NULL},
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

static long doirb(FILE *f){
	static struct dictentry resource = {0, NULL, "RESOURCE", "dummy", NULL};
	char type[4],name[0x100];
	int id,namelen;
	long size;
	struct dictentry *d;

	fread(type,1,4,f);
	id = get2B(f);
	namelen = fgetc(f);
	fread(name,1,PAD2(1+namelen)-1,f);
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
			entertag(f, 2, 1, &resource, d);
			fputs("\t</RESOURCE>\n", xml);
		}else{
			fputs(" />\n", xml);
		}
	}
	fseeko(f, PAD2(size), SEEK_CUR); // skip resource block data

	return 4+2+PAD2(1+namelen)+4+PAD2(size); /* returns total bytes in block */
}

void doimageresources(FILE *f){
	long len = get4B(f);
	VERBOSE("\nImage resources (%ld bytes):\n",len);
	while(len > 0)
		len -= doirb(f);
	if(len != 0) warn("image resources overran expected size by %d bytes\n", -len);
}
