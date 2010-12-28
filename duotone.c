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

#define DUOTONE_DATA_SIZE (4*(10+64+28) + 2 + 11*10)

void duotone_data(psd_file_t f, int level){
	const char *indent = tabs(level);
	const int overprints[] = {0, 1, 4, 11}; // number of overprint colours defined, per # inks
	int plates, i, j;
	unsigned char data[DUOTONE_DATA_SIZE], c, *p;
	psd_bytes_t n = get4B(f);

	if(n < DUOTONE_DATA_SIZE)
		alwayswarn("Duotone color mode data too short; %d bytes but expected %d", n, DUOTONE_DATA_SIZE);
	else if(xml){
		fprintf(xml, "%s<DUOTONE>\n", indent);
		fprintf(xml, "\t%s<VERSION>%d</VERSION>\n", indent, get2B(f));
		plates = get2B(f);
		fread(data, 1, DUOTONE_DATA_SIZE, f);
		n -= 4 + DUOTONE_DATA_SIZE; // skip any extra
		for(i = 0; i < 4; ++i){
			if(i < plates){
				fprintf(xml, "\t%s<PLATE>\n", indent);
				colorspace(level+2, (data[i*10]<<8) | data[i*10+1], data + i*10 + 2);
				fprintf(xml, "\t\t%s<INKNAME>", indent);
				p = data + 40 + i*64; // points to Pascal string (i.e. preceded by length)
				c = p[p[0]+1];
				p[p[0]+1] = 0;
				fputsxml((char*)(p+1), xml);
				p[p[0]+1] = c;
				fputs("</INKNAME>\n", xml);
				fprintf(xml, "\t\t%s<TRANSFER>\n", indent);
				p = data + 4*(10+64) + i*28;
				for(j = 0; j < 13; ++j){
					int v = (p[0]<<8) | p[1];
					if(v == 0xffff)
						fprintf(xml, "\t\t\t%s<NULL/>\n", indent);
					else
						fprintf(xml, "\t\t\t%s<POINT>%g</POINT>\n", indent, v/10.);
					p += 2;
				}
				fprintf(xml, "\t\t\t%s<OVERRIDE>%d</OVERRIDE>\n", indent, (p[0]<<8) | p[1]);
				fprintf(xml, "\t\t%s</TRANSFER>\n", indent);
				fprintf(xml, "\t%s</PLATE>\n", indent);
			}
		}
		p = data + 4*(10+64+28);
		fprintf(xml, "\t%s<DOTGAIN>%d</DOTGAIN>\n", indent, (p[0]<<8) | p[1]);
		p += 2;
		fprintf(xml, "\t%s<OVERPRINTCOLOR>\n", indent);
		for(i = 0; i < overprints[plates-1]; ++i){
			colorspace(level+2, (p[0]<<8) | p[1], p + 2);
			p += 10;
		}
		fprintf(xml, "\t%s</OVERPRINTCOLOR>\n", indent);
		fprintf(xml, "%s</DUOTONE>\n", indent);
	}

	fseeko(f, n, SEEK_CUR);
}
