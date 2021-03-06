# Other include directory (for CFITSIO, libsla, which is in PRESTO)
OTHERINCLUDE = -I/usr/include/cfitsio 
#OTHERINCLUDE = -I/data/home/phil/pks/linux64/include/cfitsio
#OTHERINCLUDE = /home.local/phil/svn/pdev/include
# Other link directory (for CFITSIO)
OTHERLINK = -L/usr/lib64 -lcfitsio #-L/home.local/phil/svn/pdev/libs 


# Source directory
SRCDIR = $(shell pwd)

#BINDIR = /home/deneva/bin64
BINDIR = .

# Which C compiler
CC = gcc
CFLAGS = $(OTHERINCLUDE) -DSRCDIR=\"$(SRCDIR)\"\
	-D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64\
	-g -Wall -W -O1
#-fthread-jumps \
#-fcrossjumping \
#-foptimize-sibling-calls \
#-fcse-follow-jumps  \
#-fcse-skip-blocks \
#-fgcse  \
#-fgcse-lm \
#-fexpensive-optimizations \
#-fstrength-reduce \
#-frerun-loop-opt \
#-fcaller-saves \
#-fpeephole2 \
#-fschedule-insns \
#-fschedule-insns2 \
#-fsched-interblock \
#-fsched-spec \
#-fregmove \
#-fstrict-aliasing \
#-fdelete-null-pointer-checks \
#-freorder-blocks  \
#-freorder-functions \
#-falign-functions  \
#-falign-jumps \
#-falign-loops  \
#-falign-labels \
#-ftree-vrp \
#-ftree-pre \
#-finline-functions \
#-funswitch-loops \
#-fgcse-after-reload

CLINKFLAGS = $(CFLAGS)

# When modifying the CLIG files, the is the location of the clig binary
#CLIG = /usr/bin/clig
CLIG =/home/deneva/local/bin64/clig 
# Rules for CLIG generated files
%_cmd.c : %_cmd.cli
	$(CLIG) -o $*_cmd -d $<
	cp $*_cmd.c ..

OBJS1 = vectors.o write_psrfits.o rescale.o read_psrfits.o

psrfits2psrfits: psrfits2psrfits.o psrfits2psrfits_cmd.o $(OBJS1)
	$(CC) $(CLINKFLAGS) -o $(BINDIR)/$@ psrfits2psrfits.o psrfits2psrfits_cmd.o $(OBJS1) -lm $(OTHERLINK)

# Default indentation is K&R style with no-tabs,
# an indentation level of 4, and a line-length of 85
indent:
	indent -kr -nut -i4 -l85 *.c
	rm *.c~

clean:
	rm -f *.o *~ *#

cleaner: clean
	rm -f psrfits2psrfits
