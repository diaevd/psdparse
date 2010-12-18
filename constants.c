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

const char *mode_names[] = {
	"Bitmap", "GrayScale", "IndexedColor", "RGBColor",
	"CMYKColor", "HSLColor", "HSBColor", "Multichannel",
	"Duotone", "LabColor", "Gray16", "RGB48",
	"Lab48", "CMYK64", "DeepMultichannel", "Duotone16"
};

const char *channelsuffixes[] = {
	"", "", "", "RGB",
	"CMYK", "HSL", "HSB", "",
	"", "Lab", "", "RGB",
	"Lab", "CMYK", "", ""
};

// The number of channels in each mode that contain image data.
const int mode_channel_count[] = {
	1, 1, 1, 3, 4, 3, 3, 0, 1, 3, 1, 3, 3, 4, 0, 1
};
