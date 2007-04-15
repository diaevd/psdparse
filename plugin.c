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

int pl_fgetc(psd_file_t f){
	// FIXME: replace with buffered version, this will be SLOW
	unsigned char c;
	long count = 1;
	return FSRead(f, &count, &c) ? EOF : c;
}

size_t pl_fread(void *ptr, size_t s, size_t n, psd_file_t f){
	long count = s*n;
	return FSRead(f, &count, ptr) ? 0 : n;
}

off_t pl_fseeko(psd_file_t f, off_t pos, int wh){
	int err;

	switch(wh){
	case SEEK_SET: err = SetFPos(f, fsFromStart, pos); break;
	case SEEK_CUR: err = SetFPos(f, fsFromMark, pos); break;
	case SEEK_END: err = SetFPos(f, fsFromLEOF, pos); break;
	default: return -1;
	}
	return err ? -1 : 0;
}

off_t pl_ftello(psd_file_t f){
	FILEPOS pos;

	return GetFPos(f, &pos) ? -1 : pos;
}
