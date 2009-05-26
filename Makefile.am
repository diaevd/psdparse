#    This file is part of "psdparse"
#    Copyright (C) 2004-9 Toby Thain, toby@telegraphics.com.au
#
#    This program is free software; you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by  
#    the Free Software Foundation; either version 2 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License  
#    along with this program; if not, write to the Free Software
#    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

bin_PROGRAMS = psdparse

psdparse_SOURCES = main.c writepng.c writeraw.c unpackbits.c write.c \
                   resources.c icc.c extra.c constants.c util.c \
                   descriptor.c channel.c psd.c scavenge.c mmap.c \
                   psdparse.h version.h
AM_CFLAGS   = -W -Wall -O2
AM_CPPFLAGS = -DDIRSEP="'/'" -DDEFAULT_VERBOSE=0 -DPSBSUPPORT \
              -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 \
              $(LIBPNG_CPPFLAGS)
LDADD = $(LIBPNG_LIBS)
