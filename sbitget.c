/*
 * sbitget  --  extract bitmap-data from a TrueType font
 * version 0.1
 * Mon Jan 30 2002
 * Itou Hiroki
 */

/*
 * Copyright (c) 2002 ITOU Hiroki
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#define PROGNAME "sbitget"
#define PROGVERSION "0.1"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h> /* stat() */
#include <stdarg.h> /* vfprintf() */
#include <unistd.h>

#define uchar unsigned char
#define ulong unsigned long
#define ushort unsigned short
#define BUFSIZE 64
#define GLYPHBUFSIZE 1000
#define MAXGLYPHWIDTH 128
#define TMPFILE "sbit.tmp"
#define MAXFILENAMECHAR 256
#define MAXSTRINGINBDF 1000
#define LEVELCOPYRIGHTSTR 4
#define LEVELFONTNAMESTR 8
#define STRUNKNOWN "???"

//info of tables
typedef struct linkedlist_tag{
    struct linkedlist_tag *next; //next element's on-memory location
    ulong offset; //(byte) offset from top of TrueTypeFile to top of this table
    //    ulong len; //(byte) length of this table
    char tag[5];     //name of this table (4 characters + '\0')
} tableinfo;

//info of glyph-metric (metric means size)
typedef struct {
    uchar width;
    uchar height;
    ulong off;  //offset from top of EBDT to top of glyph data
    uchar advance; // same as DWIDTH(BDF)
    int offsetx;   //offset of boundingbox from origin to point of
                   //most right-under.
                   // that is, the axis of most right-under
    int offsety;
    uchar ppem; //pixel-size in this strike
    uchar *ebdtL; //on-memory location: top of EBDT
    int imageFormat;
    ushort id; //glyph ID number (index number)
} metricinfo;

typedef struct {
    uchar *subtableL; //location top of indexSubTable (in EBLC)
    ushort first; //firstGlyphIndex
    ushort last; //lastGlyphIndex
    int indexFormat;
    ulong off; //imageDataOffset from EBDTtop to locationOfGlyphsData
} indexSubTable_info;

typedef struct {
    uchar *strL;
    ushort slen;
} str_info;

/* func prototype */
int main(int argc, char **argv);
void see_eblc(uchar *eblcL, uchar *ebdtL, char *copyright, char *fontname);
ushort see_indexSubTable(indexSubTable_info *st, uchar *ebdtL, FILE *outfp);
void getTableInfo(uchar *p, tableinfo *t);
void validiateTTF(uchar *p);
uchar *mread(uchar *p, int size, char *s);
void see_bitmapSizeTable(uchar *p, int *numElem, ulong *arrayOffset, metricinfo *bbox);
uchar *see_sbitLineMetrics(uchar *p, metricinfo *bbox, int direction);
void see_indexSubTableArray(uchar *elemL, uchar *arrayL, indexSubTable_info *st);
int see_indexSubHeader(indexSubTable_info *st);
void putglyph(metricinfo *glyph, int size, FILE *outfp);
void setGlyphHead(metricinfo *g, char *s);
void setGlyphBody_byte(uchar *p, const uchar *end, metricinfo *g, char *s);
void setGlyphBody_bit(uchar *p, const uchar *end, metricinfo *g, char *s);
void errexit(char *fmt, ...);
uchar *see_glyphMetrics(uchar *p, metricinfo *met, int big);
void see_name(uchar *nameL, char *copyright, char *fontname);
void copystr(char *dst, uchar *src, ushort len, int forFilename);



/*
 * reading a TrueType font
 *   calling functions for 'name', 'EBLC', 'EBDT' tables
 */
int main(int argc, char **argv){
    uchar *ttfL; //on memory location: top of TrueTypeFile
    char copyright[MAXSTRINGINBDF] = STRUNKNOWN;
    char fontname[MAXSTRINGINBDF] = STRUNKNOWN;


    if(argc!=2){
        fprintf(stderr, PROGNAME " version " PROGVERSION " - extract bitmap-data from a TrueType font\n");
        fprintf(stderr, "usage:  " PROGNAME " file.ttf\n");
        exit(1);
    }

    /*
     * reading TrueTypeFile to Memory
     */
    {
        FILE *fp;
        size_t ttfsize;
        struct stat info;

        if((fp=fopen(argv[1],"rb"))==NULL)
            errexit("cannot open '%s'", argv[1]);
        if(stat(argv[1], &info) != 0)
            errexit("stat");
        ttfsize = info.st_size;
        if((ttfL=malloc(ttfsize))==NULL)
            errexit("malloc");
        if(fread(ttfL, 1, ttfsize, fp)!=ttfsize)
            errexit("fread");
        fclose(fp);
    }

    //ckeck this file is TrueType? or not
    validiateTTF(ttfL);

    {
        tableinfo table; //store the first table
                          //dynamically allocate second and after tables
        tableinfo *t; //loop
        uchar *eblcL = NULL; //on memory location: top of EBLC table
        uchar *ebdtL = NULL; //on memory location: top of EBDT table

        //get locations of tables
        getTableInfo(ttfL, &table);

        /*
         * reading name table
         *   get strings of copyright, fontname
         */
        for(t=table.next; t->next!=NULL; t=t->next){
            if(strcmp("name", t->tag)==0){
                see_name(ttfL + t->offset, copyright, fontname);
                break;
            }
        }

        //EBDT table
        for(t=table.next; t->next!=NULL; t=t->next){
            if(strcmp("EBDT", t->tag)==0 || strcmp("bdat", t->tag)==0)
                ebdtL = ttfL + t->offset;
        }
        if(ebdtL == NULL)
            errexit("This font has no bitmap-data.");

        // EBLC table
        for(t=table.next; t->next!=NULL; t=t->next){
            if(strcmp("EBLC", t->tag)==0 || strcmp("bloc", t->tag)==0)
                eblcL = ttfL + t->offset;
        }
        if(eblcL == NULL)
            errexit("This font has no bitmap-data.");
        see_eblc(eblcL, ebdtL, copyright, fontname);
    }

    exit(EXIT_SUCCESS);
}


/*
 * reading EBLC table
 * in:  on-memory location: top of EBLC
 *      on-memory location: top of EBDT
 *      strings of copyright
 *      strings of fontname
 * out: nothing
 */
void see_eblc(uchar *eblcL, uchar *ebdtL, char *copyright, char *fontname){
    char s[BUFSIZE];
    int numSize; //number of BitmapSizeTable
    int i,j;

    /*
     * reading EBLC header
     *   get number of bitmapSizeTable
     */
    {
        uchar *p = eblcL;

        //version number
        p = mread(p, sizeof(ulong), s);

        //number of bitmapSizeTable
        p = mread(p, sizeof(ulong), s);
        numSize = (ulong)strtol(s,(char**)NULL,16);
    }

    /*
     * reading bitmapSizeTables
     */
    for(i=0; i<numSize; i++){
        uchar *arrayL; //on-memory location: top of indexSubTableArray
        int numElem; //number of indexSubTableArray-elements
        metricinfo bbox; //strike's bounding box: metric info for strike
        FILE *outfp;
        ushort totalglyphs; //this program can handle under 65536 glyphs

        /*
         * reading a bitmapSizeTable
         *    get number of indexSubTableArrays
         *    get location of a first indexSubTableArray
         *    get info of BoundingBox
         */
        {
            ulong offset; //from EBLCtop to a first indexSubTableArray
            //8 = size of EBLCheader,  48 = size of one bitmapSizeTable
            see_bitmapSizeTable(eblcL+8+(48*i), &numElem, &offset, &bbox);
            arrayL = eblcL + offset;
        }

        /*
         * prepare to write a tmp file
         */
        if((outfp=fopen(TMPFILE,"wb"))==NULL)
            errexit("fopen");
        totalglyphs = 0;

        /*
         * reading indexSubTableArrays and indexSubTables
         */
        for(j=0; j<numElem; j++){
            indexSubTable_info st;

            /*
             * reading an indexSubTableArray
             *   get firstGlyphIndex, lastGlyphIndex
             *   get location of indexSubTable
             */
            //8 = size of one indexSubTableArray
        
            see_indexSubTableArray(arrayL+(j*8), arrayL, &st);

            /*
             * reading indexSubTable
             *   get info of glyphs... read bitmapdata... write to tmpfile...
             */
            totalglyphs += see_indexSubTable(&st, ebdtL, outfp);
        }

        /*
         * write to a tmp file
         */
        if(fclose(outfp)!=0)
            errexit("fclose");

        /*
         * add some to the tmp file, and write a bdf file
         */
        {
            FILE *infp;
            char *filebuf;
            struct stat info;
            char fname[MAXFILENAMECHAR];

            if(strcmp(fontname,STRUNKNOWN)==0)
                sprintf(fname, "sbit-%02dpx.bdf", bbox.ppem);
            else
                sprintf(fname, "%s-%02dpx.bdf", fontname, bbox.ppem);

            if((outfp=fopen(fname,"wb"))==NULL)
                errexit("fopen");

            //read that tmp file
            if((infp=fopen(TMPFILE,"rb"))==NULL)
                errexit("fopen");
            if(stat(TMPFILE, &info) != 0)
                errexit("stat");
            if((filebuf=calloc(info.st_size, sizeof(char)))==NULL)
                errexit("calloc");
            if(fread(filebuf,1,info.st_size,infp)!=info.st_size)
                errexit("fread");

            fprintf(outfp,
                    "STARTFONT 2.1\n"
                    "COMMENT extracted with %s %s\n"
                    "FONT %s\n"
                    "SIZE %d 75 75\n"
                    "FONTBOUNDINGBOX %d %d %d %d\n"
                    "STARTPROPERTIES 1\n"
                    "COPYRIGHT \"%s\"\n"
                    "ENDPROPERTIES\n"
                    "CHARS %d\n"

                    ,PROGNAME, PROGVERSION
                    ,fontname
                    ,bbox.ppem
                    ,bbox.width, bbox.height, bbox.offsetx, bbox.offsety
                    ,copyright
                    ,totalglyphs);

            if(fwrite(filebuf,1,info.st_size,outfp)!=info.st_size)
                errexit("fwrite");
            fprintf(outfp, "ENDFONT\n");
            fclose(outfp);
            fprintf(stderr, "  wrote '%s'\n", fname);

            //remove tmp file
            fclose(infp);
            if(remove(TMPFILE)!=0)
                errexit("remove");
        }
    }
}





/*
 * reading indexSubTable
 * in:  info of indexSubTable
 * out: number of glyphs contained in this indexSubTable
 */
ushort see_indexSubTable(indexSubTable_info *st, uchar *ebdtL, FILE *outfp){
    metricinfo glyph;
    int i;
    char s[BUFSIZE];
    uchar *p;
    ushort numGlyphs = 0;

    /*
     * reading the header of indexSubTable
     *   get indexFormat (order of glyphs in EBDT)
     *   get imageFormat (type of image format of glyphs in EBDT)
     *   get offset from EBDTtop to locationOfGlyphsData
     */
    glyph.imageFormat = see_indexSubHeader(st);
    p = st->subtableL + 8; //8 = size of indexSubHeader
    glyph.ebdtL = ebdtL;

    /*
     * reading the body of indexSubTable
     */
    switch (st->indexFormat){
    case 1: // proportional with 4byte offset
        {
            ulong nextoff, curoff; //next glyph's offset, current offset
            // to know last glyph's length, +1 is needed
            for(i=st->first; i<= st->last + 1; i++){
                p = mread(p, sizeof(ulong), s);
                nextoff = (ulong)strtol(s,(char**)NULL,16);
                if(i!=st->first && nextoff-curoff>0){
                    glyph.off =  st->off + curoff;
                    glyph.id = i-1;
                    putglyph(&glyph, nextoff-curoff, outfp);
                    numGlyphs++;
                }
                curoff = nextoff;
            }
        }
        break;
    case 3: //proportional with 2byte offset
        {
            ushort nextoff, curoff;
            for(i=st->first; i<= st->last + 1; i++){
                p = mread(p, sizeof(ushort), s);
                nextoff = (ushort)strtol(s,(char**)NULL,16);
                if(i!=st->first && nextoff-curoff>0){
                    glyph.off =  st->off + curoff;
                    glyph.id = i-1;
                    putglyph(&glyph, nextoff-curoff, outfp);
                    numGlyphs++;
                }
                curoff = nextoff;
            }
        }
        break;
    case 4: // proportional with sparse codes
        {
            ulong nGlyphs;
            ushort nextoff, curoff, nextid, curid; //current ID, nextID

            p = mread(p, sizeof(ulong), s);
            nGlyphs = (ulong)strtol(s,(char**)NULL,16);

            for(i=0; i<nGlyphs+1; i++){
                p = mread(p, sizeof(ushort), s);
                nextid = (ushort)strtol(s,(char**)NULL,16);

                p = mread(p, sizeof(ushort), s);
                nextoff = (ushort)strtol(s,(char**)NULL,16);
                if(i!=0 && nextoff-curoff>0){
                    glyph.off = st->off + curoff;
                    glyph.id = curid;
                    putglyph(&glyph, nextoff-curoff, outfp);
                    numGlyphs++;
                }
                curoff = nextoff;
                curid = nextid;
            }
        }
        break;
    case 2:     //monospaced with close codes
        {
            ulong imageSize;

            p = mread(p, sizeof(ulong), s);
            imageSize = (ulong)strtol(s,(char**)NULL,16);
            p = see_glyphMetrics(p, &glyph, 1);

            for(i=st->first; i<=st->last; i++){
                glyph.off = st->off + imageSize * (i - st->first);
                glyph.id = i;
                putglyph(&glyph, imageSize, outfp);
                numGlyphs++;
            }
        }
        break;
    case 5:     //monospaced with sparse codes
        {
            ulong imageSize, nGlyphs;

            p = mread(p, sizeof(ulong), s);
            imageSize = (ulong)strtol(s,(char**)NULL,16);
            p = see_glyphMetrics(p, &glyph, 1);

            p = mread(p, sizeof(ulong), s);
            nGlyphs = (ulong)strtol(s,(char**)NULL,16);

            for(i=0; i<nGlyphs; i++){
                p = mread(p, sizeof(ushort), s);
                glyph.id = (ushort)strtol(s,(char**)NULL,16);
                glyph.off = st->off + imageSize * i;
                putglyph(&glyph, imageSize, outfp);
                numGlyphs++;
            }
        }
        break;
    default:
        errexit("indexFormat %d is not supported.", st->indexFormat);
        break;
    }

    //skip padding
    for(i=0; i<(p - st->subtableL)%4; i++)
        p = mread(p, sizeof(uchar), s);

    return numGlyphs;
}


/*
 * reading TrueTypefont header
 * in:  on-memory top of truetype font
 *      (for out) info of each table
 * out: nothing
 */
void getTableInfo(uchar *p, tableinfo *t){
    int i;
    ushort numTable;
    char s[BUFSIZE];

    p = mread(p, sizeof(ulong), s); //version

    p = mread(p, sizeof(ushort), s); //number of tables
    numTable = (ushort)strtol(s,(char**)NULL,16);

    p = mread(p, sizeof(ushort), s); //searchRange
    p = mread(p, sizeof(ushort), s); //entrySelector
    p = mread(p, sizeof(ushort), s); //rangeShift

    /*
     * assign info of table
     */
    for(i=0; i<numTable; i++){
        //allocate for a tableinfo (free() don't exist)
        if( (t->next=malloc(sizeof(tableinfo))) == NULL)
            errexit("malloc");
        t = t->next;

        //a name of table (tag)
        memset(t->tag, 0x00, 5); // 0 clear
        memcpy(t->tag, p, 4);
        p += sizeof(ulong);

        p += sizeof(ulong); //checksum (not use now)

        //offset
        p = mread(p, sizeof(ulong), s); //number of tables
        t->offset = (ulong)strtol(s,(char**)NULL,16);

        p += sizeof(ulong); //length (not use now)

    }
    t->next = NULL;
}


/*
 * checking TrueTypefont or not
 * in:  on-memory location: top of truetype font
 * out: nothing
 *
 *  If errors, exit program.
 */
void validiateTTF(uchar *p){
    char s[BUFSIZE];

    p = mread(p, sizeof(ulong), s); //version
    if(strcmp(s,"0x00010000")==0){
        printf("  Microsoft TrueType/OpenType\n");
    }else if(strcmp(s, "0x74746366")==0){
        printf("  Microsoft TrueTypeCollection\n");
        errexit("TTC file is not supported yet.");
    }else if(strcmp(s, "0x74727565")==0){
        printf("  Apple TrueType\n");
    }else{
        errexit("This file is not a TrueTypeFont.");
    }
}

/*
 * reading given bytes, return to string & pointer
 * in:  on-memory location to read
 *      byte size to read
 *      (for out) string pointer to assign
 * out: on-memory location to read next
 */
uchar *mread(uchar *p, int size, char *s){
    int i;
    memset(s, 0x00, BUFSIZE); //zero clear
    sprintf(s, "0x"); //add '0x' for make indentified as hexadecimal

    for(i=0; i<size; i++){
        sprintf(s+(i*2)+2, "%02x", *p);
        p++;
    }
    return p;
}



/*
 * reading bitmapSizeTable(defines info of strike) in EBLC
 * in:  on-memory location: top of a bitmapSizeTable
 *      (for out) number of indexSubTableArray-elements
 *      (for out) on-memory location: top of indexSubTableArray
 *      (for out) info of BoundingBox
 * out: nothing
 */
void see_bitmapSizeTable(uchar *p, int *numElem, ulong *arrayOffset, metricinfo *bbox){
    char s[BUFSIZE]; //stored string

    //offset from EBLCtop to indexSubTableArray
    p = mread(p, sizeof(ulong), s);
    *arrayOffset = (ulong)strtol(s,(char**)NULL,16);

    //indexSubTable size (byte) (not use)
    p = mread(p, sizeof(ulong), s);

    //number of indexSubTableArray-elements (= number of indexSubTables)
    p = mread(p, sizeof(ulong), s);
    *numElem = (ulong)strtol(s,(char**)NULL,16);

    //colorRef (always 0)
    p = mread(p, sizeof(ulong), s);

    //sbitLineMetrics: hori
    //info of metric about all glyphs in a strike
    p = see_sbitLineMetrics(p, bbox, 1); // 1 means direction==horizontal


    //sbitLineMetrics: vert  (not use)
    p = see_sbitLineMetrics(p, NULL, 0); // 0 means direction==vertical

    //start glyphIndex for this size
    p = mread(p, sizeof(ushort), s);

    //end glyphIndex for this size
    p = mread(p, sizeof(ushort), s);

    //ppemX (the strike's boundingbox width (pixel))
    p = mread(p, sizeof(uchar), s);
    bbox->ppem = (uchar)strtol(s,(char**)NULL,16);

    //ppemY
    p = mread(p, sizeof(uchar), s);

    //bitDepth (always 1)
    p = mread(p, sizeof(uchar), s);

    //flags (1=horizontal, 2=vertical)
    p = mread(p, sizeof(char), s);

    return;
}


/*
 * reading info of metric
 * in:  on-memory location:  top of metric-info
 *      directionHori: 1=horizontal,  0=vertical
 * out: on-memory location:  just after metric-info
 */
uchar *see_sbitLineMetrics(uchar *p, metricinfo *bbox, int directionHori){
    char s[BUFSIZE];
    char widthMax, maxBeforeBL, minAfterBL, minOriginSB;

    //ascender: distance from baseline to upper-line (px)
    p = mread(p, sizeof(char), s);

    //descender: distance from baseline to bottom-line (px)
    p = mread(p, sizeof(char), s);

    //widthMax
    p = mread(p, sizeof(uchar), s);
    widthMax = (char)strtol(s,(char**)NULL,16);

    //caratSlopeNumerator
    p = mread(p, sizeof(char), s);

    //caratSlopeDenominator
    p = mread(p, sizeof(char), s);

    //caratOffset
    p = mread(p, sizeof(char), s);

    //minOriginSB
    p = mread(p, sizeof(char), s);
    minOriginSB = (char)strtol(s,(char**)NULL,16);

    //minAdvanceSB
    p = mread(p, sizeof(char), s);

    //maxBeforeBL
    p = mread(p, sizeof(char), s);
    maxBeforeBL = (char)strtol(s,(char**)NULL,16);

    //minAfterBL
    p = mread(p, sizeof(char), s);
    minAfterBL = (char)strtol(s,(char**)NULL,16);

    //pad1
    p = mread(p, sizeof(char), s);

    //pad2
    p = mread(p, sizeof(char), s);

    //if direction==hori, assign metric-info
    //elseif direction==vert, don't assign
    if(directionHori){
        bbox->width = widthMax;
        bbox->height = maxBeforeBL - minAfterBL;
        bbox->offsetx = minOriginSB;
        bbox->offsety = minAfterBL;
    }

    return p;
}



/*
 * reading an indexSubTableArray
 * in:  on-memery location: top of indexSubTableArray-elements
 *      on-memery location: top of indexSubTableArray
 *      (for out) info of indexSubTable
 * out: nothing
 */
void see_indexSubTableArray(uchar *elemL, uchar *arrayL, indexSubTable_info *st){
    char s[BUFSIZE];
    uchar *p = elemL;

    //firstGlyphIndex
    p = mread(p, sizeof(ushort), s);
    st->first = (ushort)strtol(s,(char**)NULL,16);

    //lastGlyphIndex
    p = mread(p, sizeof(ushort), s);
    st->last = (ushort)strtol(s,(char**)NULL,16);

    //location of indexSubTable
    p = mread(p, sizeof(ulong), s);
    st->subtableL = arrayL + (ulong)strtol(s,(char**)NULL,16);
}


/*
 * raeding header of an indexSubTable
 * in:  (for in and out) info of indexSubTable
 * out: imageFormat
 */
int see_indexSubHeader(indexSubTable_info *st){
    char s[BUFSIZE];
    char *p = st->subtableL;
    int imageFormat;

    //indexFormat
    p = mread(p, sizeof(ushort), s);
    st->indexFormat = strtol(s,(char**)NULL,16);

    //imageFormat
    p = mread(p, sizeof(ushort), s);
    imageFormat = (ushort)strtol(s,(char**)NULL,16);

    //imageDataOffset
    p = mread(p, sizeof(ulong), s);
    st->off = (ulong)strtol(s,(char**)NULL,16);

    return imageFormat;
}


/*
 * read one glyph, and put to file
 * in:  info of a glyph (type of EBDT-imageFormat,... )
 *      size of bitmapdata
 *      writing-file pointer
 * out: nothing
 */
void putglyph(metricinfo *glyph, int size, FILE *outfp){
    uchar *glyphL = glyph->ebdtL + glyph->off;
    uchar *p = glyphL;
    char s[GLYPHBUFSIZE];
    memset(s, 0x00, GLYPHBUFSIZE);

    switch (glyph->imageFormat){
    case 1:
        //EBDT format 1: byte-aligned, small-metric
        p = see_glyphMetrics(p, glyph, 0);
        setGlyphHead(glyph, s);
        setGlyphBody_byte(p, glyphL+size, glyph, s);
        break;
    case 2:
        //EBDT format 2: bit-aligned, small-metric
        p = see_glyphMetrics(p, glyph, 0);
        setGlyphHead(glyph, s);
        setGlyphBody_bit(p, glyphL+size, glyph, s);
        break;
    case 5:
        //EBDT format 5: bit-aligned, EBLC-metric
        setGlyphHead(glyph, s);
        setGlyphBody_bit(p, glyphL+size, glyph, s);
        break;
    case 6:
        //EBDT format 6: byte-aligned, big-metric
        p = see_glyphMetrics(p, glyph, 1);
        setGlyphHead(glyph, s);
        setGlyphBody_byte(p, glyphL+size, glyph, s);
        break;
    case 7:
        //EBDT format 7: bit-aligned, big-metric
        p = see_glyphMetrics(p, glyph, 1);
        setGlyphHead(glyph, s);
        setGlyphBody_bit(p, glyphL+size, glyph, s);
        break;
    default:
        errexit("imageFormat %d is not supported.", glyph->imageFormat);
        break;
    }

    fprintf(outfp, "%sENDCHAR\n", s);
}


/*
 * set header of a BDF glyph to string
 * in:  info of glyph(glyph's boudingbox)
 *      (for out) string pointer
 * out: nothing
 */
void setGlyphHead(metricinfo *g, char *s){
    sprintf(s,
            "STARTCHAR glyphID:%04x\n"
            "ENCODING %d\n"
            "DWIDTH %d\n"
            "BBX %d %d %d %d\n"
            "BITMAP\n",
            g->id, -1, g->advance,
            g->width, g->height, g->offsetx, g->offsety
            );
}



/*
 * set bitmap of a BDF glyph: byte-aligned
 * in:  on-memory location: beginning of bitmapdata in EBDT
 *      on-memory location: end of bitmapdata in EBDT
 *      info of glyph (width)
 *      (for out) string pointer
 * out: nothing
 */
void setGlyphBody_byte(uchar *p, const uchar *end, metricinfo *g, char *s){
    int cnt = 0; //counter: print newline or not
    char str[GLYPHBUFSIZE];
    memset(str, 0x00, GLYPHBUFSIZE);

    while(p < end){
        //read one byte, change to 2 characters of hexadecimal
        sprintf(str, "%x%x", (*p)>>4, (*p)&0x0f);
        strcat(s, str);
        //not print '\n' if cnt < glyph.width-(int)1)/8
        if( (g->width-(int)1)/8 <= cnt){
            strcat(s, "\n");
            cnt = 0;
        }else{
            cnt++;
        }
        p++;
    }
}





/*
 * set bitmap of a BDF glyph: bit-aligned
 * in:  on-memory location: beginning of bitmapdata in EBDT
 *      on-memory location: end of bitmapdata in EBDT
 *      info of glyph (width, height)
 *      (for out) string pointer
 * out: nothing
 */
void setGlyphBody_bit(uchar *p, const uchar *end, metricinfo *g, char *s){
    uchar *b, *mem, *binline, pad;
    int i,j,k;
    static char *hex2bin[]= {
        "0000","0001","0010","0011","0100","0101","0110","0111",
        "1000","1001","1010","1011","1100","1101","1110","1111"
    };
    char str[GLYPHBUFSIZE];
    memset(str, 0x00, GLYPHBUFSIZE);

    /*
     * write binary number without padding
     */
    mem = malloc(MAXGLYPHWIDTH * MAXGLYPHWIDTH); //width x height
    memset(mem, '0', MAXGLYPHWIDTH * MAXGLYPHWIDTH);
    b = mem;

    while(p < end){
        //read one byte, change to 2 characters of hexadecimal
        memcpy(b, hex2bin[(*p)>>4], 4);
        b+=4;
        memcpy(b, hex2bin[(*p)&0x0f], 4);
        b+=4;
        p++;
    }

    /*
     * set padding for hori
     */
    binline = malloc(MAXGLYPHWIDTH);
    memset(binline, '0', MAXGLYPHWIDTH);
    pad = ((g->width+7)/8*8) - g->width;

    for(i=0; i<g->height; i++){
        for(j=0; j<g->width; j++){
            *(binline+j) = *(mem + (i*g->width) + j);
        }
        for( ; j<g->width+pad; j++){
            *(binline+j) = '0';
        }

        /*
         * change back to hexadecimal
         */
        for(j=0; j<g->width+pad; j+=4){
            for(k=0x0; k<=0xf; k++){
                if(strncmp(binline+j,hex2bin[k],4)==0){
                    sprintf(str, "%x", k);
                    strcat(s, str);
                }
            }
        }
        strcat(s, "\n");
    }
    free(mem);
    free(binline);
}

/*
 * display error messages, and exit this program
 * in:  variable arguments to print
 * out: nothing
 */
void errexit(char *fmt, ...){
        va_list ap;
        va_start(ap, fmt);
        //        fprintf(stderr, "%s: ", PROGNAME);
        fprintf(stderr, "  Error: ");
        vfprintf(stderr, fmt, ap);
        fprintf(stderr, "\n");
        va_end(ap);

        exit(EXIT_FAILURE);
}


/*
 * reading BigGlyphMetrics/SmallGlyphMetrics in EBLC
 * in:  on-memory location: top of Big/Small GlyphMetrics
 *      (for out) info of glyph
 *      Big or Small (1==big, 0==small)
 * out: on-memory location: just after Big/Small GlyphMetrics
 */
uchar *see_glyphMetrics(uchar *p, metricinfo *met, int big){
    char s[BUFSIZE];

    //height
    p = mread(p, sizeof(uchar), s);
    met->height = (uchar)strtol(s,(char**)NULL,16);

    //width
    p = mread(p, sizeof(uchar), s);
    met->width = (uchar)strtol(s,(char**)NULL,16);

    //horiBearingX
    p = mread(p, sizeof(char), s);
    met->offsetx = (char)strtol(s,(char**)NULL,16);

    //horiBearingY
    p = mread(p, sizeof(char), s);
    met->offsety = (char)strtol(s,(char**)NULL,16) - met->height;

    //horiAdvance
    p = mread(p, sizeof(uchar), s);
    met->advance = (uchar)strtol(s,(char**)NULL,16);

    if(big){
        //vertBearingX
        p = mread(p, sizeof(char), s);

        //vertBearingY
        p = mread(p, sizeof(char), s);

        //vertAdvance
        p = mread(p, sizeof(uchar), s);
    }
    return p;
}

/*
 * reading 'name' table
 *   in:  on-memory location: top of 'name'
 *        (for out) strings of copyright
 *        (for out) strings of fontname
 *   out: nothing
 */
void see_name(uchar *nameL, char *copyright, char *fontname){
    char s[BUFSIZE];
    char *p = nameL;
    ushort numRecord, storageoff;
    int i, j;
    //make copyright/fontname string have priority(level).
    str_info sright[LEVELCOPYRIGHTSTR];
    str_info sname[LEVELFONTNAMESTR];

    for(i=0; i<LEVELCOPYRIGHTSTR; i++)
        sright[i].slen = 0;
    for(i=0; i<LEVELFONTNAMESTR; i++)
        sname[i].slen = 0;

    /*
     * reading header of 'name' table
     */
    p = mread(p, sizeof(ushort), s); //format selector

    //number of name records
    p = mread(p, sizeof(ushort), s);
    numRecord = (ushort)strtol(s,(char**)NULL,16);

    //offset from 'name'top to string storage
    p = mread(p, sizeof(ushort), s);
    storageoff =  (ushort)strtol(s,(char**)NULL,16);

    /*
     * reading nameRecords
     */
    for(i=0; i<numRecord; i++){
        ushort platformid, specificid, langid, nameid, slen, soff;

        //PlatformID
        p = mread(p, sizeof(ushort), s);
        platformid = (ushort)strtol(s,(char**)NULL,16);

        //Platform-specificID
        p = mread(p, sizeof(ushort), s);
        specificid = (ushort)strtol(s,(char**)NULL,16);

        //LanguageID
        p = mread(p, sizeof(ushort), s);
        langid = (ushort)strtol(s,(char**)NULL,16);

        //NameID
        p = mread(p, sizeof(ushort), s);
        nameid = (ushort)strtol(s,(char**)NULL,16);

        //String Length
        p = mread(p, sizeof(ushort), s);
        slen = (ushort)strtol(s,(char**)NULL,16);

        //string storage offset from start of storage area
        p = mread(p, sizeof(ushort), s);
        soff = (ushort)strtol(s,(char**)NULL,16);



        if(platformid==1 && langid==0){
            //Macintosh, ???(0==Roman), English
            if(nameid==0){
                //Copyright
                //  priority 0 (highest)
                sright[0].strL = nameL + storageoff + soff;
                sright[0].slen = slen;
            }else if(nameid==6){
                //fontname (postscript)
                sname[0].strL = nameL + storageoff + soff;
                sname[0].slen = slen;
            }else if(nameid==4){
                //fontname (full fontname)
                sname[1].strL = nameL + storageoff + soff;
                sname[1].slen = slen;
            }
        }else if(platformid==1){
            //Macintosh, ???(0==Roman), ???(0x0411=Japanese)
            if(nameid==0){
                sright[1].strL = nameL + storageoff + soff;
                sright[1].slen = slen;
            }else if(nameid==6){
                sname[2].strL = nameL + storageoff + soff;
                sname[2].slen = slen;
            }else if(nameid==4){
                sname[3].strL = nameL + storageoff + soff;
                sname[3].slen = slen;
            }
        }else if(platformid==3 && langid==0x0409){
            //Microsoft, ???, English
            if(nameid==0){
                sright[2].strL = nameL + storageoff + soff;
                sright[2].slen = slen;
            }else if(nameid==6){
                sname[4].strL = nameL + storageoff + soff;
                sname[4].slen = slen;
            }else if(nameid==4){
                sname[5].strL = nameL + storageoff + soff;
                sname[5].slen = slen;
            }
        }else if(platformid==3){
            //Microsoft, ???, ???(0x0411==Japanese)
            if(nameid==0){
                sright[3].strL = nameL + storageoff + soff;
                sright[3].slen = slen;
            }else if(nameid==6){
                sname[6].strL = nameL + storageoff + soff;
                sname[6].slen = slen;
            }else if(nameid==4){
                sname[7].strL = nameL + storageoff + soff;
                sname[7].slen = slen;
            }
        }       

        /*
         * assign copyright string
         */
        for(j=0; j<LEVELCOPYRIGHTSTR; j++){
            if(sright[j].slen && strcmp(copyright,STRUNKNOWN)==0)
                copystr(copyright, sright[j].strL, sright[j].slen, 0);
        }

        /*
         * assign fontname string
         */
        for(j=0; j<LEVELFONTNAMESTR; j++){
            if(sname[j].slen && strcmp(fontname,STRUNKNOWN)==0)
                copystr(fontname, sname[j].strL, sname[j].slen, 1);
        }
    }
}


/*
 * assign string with convert non-ascii character to ascii-character
 * in:  destination pointer
 *      on-memory location: top of string
 *      length of string
 *      this string is for filename or not? (1==yes, 0==no)
 * out: nothing
 */
void copystr(char *dst, uchar *src, ushort len, int forFilename){
    int i;

    memset(dst, 0x00, MAXSTRINGINBDF);
    for(i=0; i<len; i++){
        uchar s = *(src+i);

        if(s==0xa9){
            memcpy(dst, "(c)", 3);
            dst+=3;
        }else if(s==0xae){
            memcpy(dst, "(r)", 3);
            dst+=3;
        }else if(forFilename && (s=='?' || s=='*' || s=='/' || s=='\\' ||
                                 s==':' || s==';' || s=='|' || s=='<' ||
                                 s=='>' || s==0x22)){
            ; //ignore (unable to use as filename in Microsoft Windows)
        }else if(s<0x20 || s>0x7e){
            ; //ignore (probably, Unicode first byte)
        }else{
            *dst = s;
            dst++;
        }
    }
    *dst='\0';
}
//end of file
