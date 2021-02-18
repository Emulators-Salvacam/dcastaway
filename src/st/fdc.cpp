/*
* Castaway
*  (C) 1994 - 2002 Martin Doering, Joachim Hoenig
*
* fdc.c - wd1772/dma emulation
*
* This file is distributed under the GPL, version 2 or at your
* option any later version.  See doc/license.txt for details.
*
* revision history
*  23.05.2002  0.02.00 JH  FAST1.0.1 code import: KR -> ANSI, restructuring
*  09.06.2002  0.02.00 JH  Renamed io.c to st.c again (io.h conflicts with system headers)
*/
static char     sccsid[] = "$Id: fdc.c,v 1.2 2002/06/08 23:31:58 jhoenig Exp $";
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include "config.h"
#include "unzip.h"
#include "st.h"
#include "savedisk.h"

#define DISKNULL \
	"\0\0\0\0\0\0\0\0\0\0" \
	"\0\0\0\0\0\0\0\0\0\0" \
	"\0\0\0\0\0\0\0\0\0\0" \
	"\0\0\0\0\0\0\0\0\0\0" \
	"\0\0\0\0\0\0\0\0\0\0" \
	"\0\0\0\0\0\0\0\0\0\0" \
	"\0\0\0\0\0\0\0\0\0\0" \
	"\0\0\0\0\0\0\0\0\0"

int fdc_commands_executed=0;

extern int dcastaway_disc_writed[2];
void dcastaway_disc_initsave(unsigned num);

/*
* FDC Registers
*/
extern int readdsk;
unsigned char   fdc_data, fdc_track, fdc_sector, fdc_status, fdc_command, fdc_motor;
unsigned char   fdc_int = 0;
char     fdcdir=1;
unsigned char	disk_ejected[2]={0,1};
unsigned char	disk_changed[2];
struct Disk     disk[2] = {
    { NULL, DISKNULL, 0, SIDES, TRACKS, SECTORS, SECSIZE },
    { NULL, DISKNULL, 0, SIDES, TRACKS, SECTORS, SECSIZE },
};

typedef struct {
	uint16 ID;					// Word	ID marker, should be $0E0F
	uint16 SectorsPerTrack;		// Word	Sectors per track
	uint16 Sides;				// Word	Sides (0 or 1; add 1 to this to get correct number of sides)
	uint16 StartingTrack;		// Word	Starting track (0-based)
	uint16 EndingTrack;			// Word	Ending track (0-based)
} MSAHEADERSTRUCT;
#define STMemory_Swap68000Int(val) ((val<<8)|(val>>8))


unsigned char msabuff[MAX_DISC_SIZE];

unsigned char disc[2][MAX_DISC_SIZE];
int discpos[2];

int discread(unsigned char *buf,int a,int len,int discn)
{
	int i;
	uint8 val1,val2,*dbuf;
	dbuf=buf;
	for (i=0;i<len;i++) *buf++=disc[discn][discpos[discn]+i];
#ifdef BYTES_SWAP
	for (i=0; i<len; i+=2) {
		val1 = dbuf[i];
		val2 = dbuf[i+1];
		dbuf[i] = val2;
		dbuf[i+1] =val1;
	}
#endif
	discpos[discn]=discpos[discn]+i;
	readdsk |= ( discn + 1 );
	return len;
}

int discwrite(unsigned char *buf,int a,int len,int discn)
{
	int i;
	uint8 val1,val2,*dbuf;
	dbuf=buf;
	dcastaway_disc_writed[discn]=1;
	for (i=0;i<len;i++) disc[discn][discpos[discn]+i]=*buf++;
#ifdef BYTES_SWAP
	for (i=0; i<len; i+=2) {
		val1 = disc[discn][discpos[discn]+i];
		val2 = disc[discn][discpos[discn]+i+1];
		disc[discn][discpos[discn]+i] = val2;
		disc[discn][discpos[discn]+i+1] =val1;
	}
#endif
	discpos[discn]=discpos[discn]+i;
	return len;
}

int discseek(int discn,int pos,int a)
{
	if (pos>(1050*1024)){
		return -1;
	}
	discpos[discn]=pos;
	return 0;
}

int unzipdisk(unsigned char *RomPath,unsigned char *buf)
{
    unzFile fp;
    unsigned long gROMLength; //size in bytes of the ROM

	
	if(fp = unzOpen((const char *)RomPath))
	{
		unsigned char szFileName[256];
		if(unzGoToFirstFile(fp) == UNZ_OK)
		{
			do
			{
				unz_file_info file_info;
				if(unzGetCurrentFileInfo(fp,&file_info, (char *)szFileName,256,NULL,0,NULL,0) == UNZ_OK)
				{
					if(strcasecmp((const char *)&szFileName[strlen((const char *)szFileName) - 3],".st") == 0||strcasecmp((const char *)&szFileName[strlen((const char *)szFileName) - 4],".msa") == 0)
					{
						gROMLength = file_info.uncompressed_size; 
						if(unzOpenCurrentFile(fp)== UNZ_OK)
						{
							if(unzReadCurrentFile(fp,buf ,gROMLength) == (int)( gROMLength))
							{
								unzClose(fp);
								return gROMLength;
							}
							else
							{
								unzClose(fp);
								return 0;
							}
							unzCloseCurrentFile(fp);
						}
						else
						{
							unzClose(fp);
							return 0;
						}
					}
				}
				else
				{
					unzClose(fp);
					return 0;
				}
			}
			while(unzGoToNextFile(fp) == UNZ_OK);
		}
		unzClose(fp);
	}
	return 0;
}

int MSA_UnCompress(unsigned char *pBuffer)
{
	MSAHEADERSTRUCT *pMSAHeader;
	unsigned char *pMSAImageBuffer,*pImageBuffer;
	unsigned char Byte,Data;
	int ImageSize = 0;
	int i,Track,Side,DataLength,NumBytesUnCompressed,RunLength;
	for (i=0;i<1050*1024;i++) msabuff[i]=pBuffer[i];
	// Is an '.msa' file?? Check header
	pMSAHeader = (MSAHEADERSTRUCT *)msabuff;
	if (pMSAHeader->ID==0x0F0E) {
		// First swap 'header' words around to PC format - easier later on
		pMSAHeader->SectorsPerTrack = STMemory_Swap68000Int(pMSAHeader->SectorsPerTrack);
		pMSAHeader->Sides = STMemory_Swap68000Int(pMSAHeader->Sides);
		pMSAHeader->StartingTrack = STMemory_Swap68000Int(pMSAHeader->StartingTrack);
		pMSAHeader->EndingTrack = STMemory_Swap68000Int(pMSAHeader->EndingTrack);
		
		// Set pointers
		pImageBuffer = (unsigned char *)pBuffer;
		pMSAImageBuffer = (unsigned char *)((unsigned long)msabuff+sizeof(MSAHEADERSTRUCT));
		
		// Uncompress to memory as '.ST' disc image - NOTE assumes 512 bytes per sector(use NUMBYTESPERSECTOR define)!!!
		for(Track=pMSAHeader->StartingTrack; Track<(pMSAHeader->EndingTrack+1); Track++) {
			for(Side=0; Side<(pMSAHeader->Sides+1); Side++) {
				// Uncompress MSA Track, first check if is not compressed
				DataLength = ((uint16)(*pMSAImageBuffer)<<8)|((uint16)(*(pMSAImageBuffer+1))&0xff);
				pMSAImageBuffer += sizeof(short int);
				if (DataLength==(512*pMSAHeader->SectorsPerTrack)) {
					// No compression on track, simply copy and continue
					memcpy(pImageBuffer,pMSAImageBuffer,512*pMSAHeader->SectorsPerTrack);
					pImageBuffer += 512*pMSAHeader->SectorsPerTrack;
					pMSAImageBuffer += DataLength;
				}
				else {
					// Uncompress track
					NumBytesUnCompressed = 0;
					while(NumBytesUnCompressed<(512*pMSAHeader->SectorsPerTrack)) {
						Byte = *pMSAImageBuffer++;
						if (Byte!=0xE5) {												// Compressed header??
							*pImageBuffer++ = Byte;										// No, just copy byte
							NumBytesUnCompressed++;
						}
						else {
							Data = *pMSAImageBuffer++;									// Byte to copy
							RunLength =((uint16)(*pMSAImageBuffer)<<8)|((uint16)(*(pMSAImageBuffer+1))&0xff);	// For length
							// Limit length to size of track, incorrect images may overflow
							if ( (RunLength+NumBytesUnCompressed)>(512*pMSAHeader->SectorsPerTrack) )
								RunLength = (512*pMSAHeader->SectorsPerTrack)-NumBytesUnCompressed;
							pMSAImageBuffer += sizeof(short int);
							for(i=0; i<RunLength; i++)
								*pImageBuffer++ = Data;									// Copy byte
							NumBytesUnCompressed += RunLength;
						}
					}
				}					
			}
		}
		
		// Set size of loaded image
		ImageSize = (unsigned long)pImageBuffer-(unsigned long)pBuffer;
	}
	
	// Return number of bytes loaded, '0' if failed
	return(ImageSize);
}


char *dcastaway_image_file=(char *)&disk[0].name[0];
char *dcastaway_image_file2=(char *)&disk[1].name[0];

int FDCInit(int i)
{
    unsigned char  *buf;
    memset((void *)&disc[i][0],0,MAX_DISC_SIZE);
    int             len,len2,calcsides,calcsectors,calctracks,badbootsector;
	discpos[i]=0;

	if (NULL != (disk[i].file = fopen (disk[i].name, "rb"))) {
		buf=&disc[i][0];
		
//		if(!strcasecmp(&disk[i].name[strlen(disk[i].name) - 4],".zip")){
			fclose(disk[i].file);
			len=unzipdisk((unsigned char*)disk[i].name,buf);
			if (len<=0)
			{
				disk[i].file=fopen (disk[i].name, "rb");
//		}else {
			fseek(disk[i].file,0,SEEK_END);
			len=ftell(disk[i].file);
			disk[i].disksize = len;
			fseek(disk[i].file,0,SEEK_SET);
			fread(buf,1,len,disk[i].file);
			fclose(disk[i].file);
		}
		len2=MSA_UnCompress(buf);
		if (len2) len=len2;
		
        disk[i].head = 0;
        disk[i].sides = (int) *(buf + 26);
        disk[i].sectors = (int) *(buf + 24);
        disk[i].secsize = 512; //(int) ((*(buf + 12) << 8) | *(buf + 11));
		if (disk[i].sectors * disk[i].sides)
			disk[i].tracks = (int) ((*(buf + 20) << 8) | *(buf + 19)) /
            (disk[i].sectors * disk[i].sides);
        
		// Second Check more precise 
		if (len> (500*1024)) calcsides = 2;
		else calcsides = 1;
		if (!(((len/calcsides)/512)%9)&&(((len/calcsides)/512)/9)<86) calcsectors=9;
		else if (!(((len/calcsides)/512)%10)&&(((len/calcsides)/512)/10)<86) calcsectors=10;
		else if (!(((len/calcsides)/512)%11)&&(((len/calcsides)/512)/11)<86) calcsectors=11;
		else if (!(((len/calcsides)/512)%12)) calcsectors=12;
		calctracks =((len/calcsides)/512)/calcsectors;
		
		if (disk[i].sides!=calcsides||disk[i].sectors!=calcsectors||disk[i].tracks!=calctracks){
			if (disk[i].sides==calcsides&&disk[i].sectors==calcsectors){
				disk[i].tracks=calctracks;
				badbootsector=0;
			}else{
				disk[i].sides=calcsides;
				disk[i].tracks=calctracks;
				disk[i].sectors=calcsectors;
				badbootsector=(i<<24)|(calcsides<<16)|(calctracks<<8)|(calcsectors);
			}
			
		}else{
			badbootsector=0;
		}
		disk_ejected[i]=0;
		disk_changed[i]=1;
		fdc_status |= 0x40;
		disk[i].head = 0;
		fdc_track = 0;
	}
	
	dcastaway_disc_initsave(i);

	return badbootsector;
}

void FDCchange(int i){
	disk[(i>>24)&0xff].sides=(i>>16)&0xff;
	disk[(i>>24)&0xff].tracks=(i>>8)&0xff;
	disk[(i>>24)&0xff].sectors=i&0xff;
}

void FDCeject(int num){
	int i;
	for (i=0;i<1050*1024;i++) disc[num][i]=0;
	disk[num].file = NULL;
	sprintf(disk[num].name,"disk%01d",num);
	disk[num].sides = SIDES;
	disk[num].tracks = TRACKS;
	disk[num].sectors = SECTORS;
	disk[num].secsize = 512;
	disk_ejected[num]=1;
	fdc_status |= 0x40;
}

void            FDCCommand(void)
{
    static char     motor = 1;
    int             sides, drives;
    long            address;    /* dma target/source address */
    long            offset;     /* offset in disk file */
    unsigned long   count;      /* number of byte to transfer */
    char           *buffer;
	int n;

    if (fdc_commands_executed<64)
    	fdc_commands_executed++;
    /* DMA target/source address */
    address = (dma_adrh << 16) + (dma_adrm << 8) + dma_adrl;
	/*	if (address>MEMSIZE){
	#ifdef DISASS
	StartDisass();
	exit(1);
	#endif
	fdc_status |= 0x10;
	   return;	
	   
		 
		   
			 }
	*/
	buffer = (char *)(membase + address);
    /* status of side select and drive select lines */
    sides = (~psg[14]) & 0x1;
    drives = (~psg[14]) & 0x6;
    if (disk_ejected[drives>>2]==1) drives=2;
	switch (drives) {
    case 2:                     /* Drive A */
        drives = 0;
        break;
    case 4:                     /* Drive B */
        drives = 1;
        break;
    case 6:                     /* both, error */
    case 0:                     /* no drive selected */
        drives = -1;
        break;
    }
    fdc_status = 0;             /* clear fdc status */
    if (fdc_command < 0x80) {   /* TYPE-I fdc commands */
        if (drives >= 0) {      /* drive selected */
            switch (fdc_command & 0xf0) {
            case 0x00:          /* RESTORE */
                disk[drives].head = 0;
                fdc_track = 0;
                break;
            case 0x10:          /* SEEK */
                disk[drives].head += (fdc_data - fdc_track);
                fdc_track = fdc_data;
                if (disk[drives].head < 0
					|| disk[drives].head >= disk[drives].tracks)
                    disk[drives].head = 0;
                break;
            case 0x30:          /* STEP */
                fdc_track += fdcdir;
            case 0x20:
                disk[drives].head += fdcdir;
                break;
            case 0x50:          /* STEP-IN */
                fdc_track++;
            case 0x40:
                if (disk[drives].head < disk[drives].tracks)
                    disk[drives].head++;
                fdcdir = 1;
                break;
            case 0x70:          /* STEP-OUT */
                fdc_track--;
            case 0x60:
                if (disk[drives].head > 0)
                    disk[drives].head--;
                fdcdir = -1;
                break;
            }
            if (disk[drives].head == 0) {
                fdc_status |= 0x4;
            }
            if (disk[drives].head != fdc_track && fdc_command & 0x4) {  /* Verify? */
                fdc_status |= 0x10;
            }
            if (motor) {
                fdc_status |= 0x20;     /* spin-up flag */
            }
        } else {                /* no drive selected */
            fdc_status |= 0x10;
        }
    } else if ((fdc_command & 0xf0) == 0xd0) {  /* FORCE INTERRUPT */
        if (fdc_command == 0xd8) {
            fdc_int = 1;
        } else if (fdc_command == 0xd0) {
            fdc_int = 0;
        }
    } else {                    /* OTHERS */
        if (drives >= 0) {      /* drive selected */
            /* offset within floppy-file */
            offset = disk[drives].secsize *
                (((disk[drives].sectors * disk[drives].sides * disk[drives].head))
				+ (disk[drives].sectors * sides) + (fdc_sector - 1));
            switch (fdc_command & 0xf0) {
            case 0x80:          /* READ SECTOR */
                count = 512;
                if (!discseek (drives, offset, 0)) {
					if (address<MEMSIZE){
						if (count == discread ((unsigned char*)buffer, 1, count, drives)) {
							address += count;
							dma_adrl = address & 0xff;
							dma_adrm = (address >> 8) & 0xff;
							dma_adrh = (address >> 16) & 0xff;
							dma_scr = 0;
							dma_sr = 1;
							break;
						}
					}else{
                        address += count;
                        dma_adrl = address & 0xff;
                        dma_adrm = (address >> 8) & 0xff;
                        dma_adrh = (address >> 16) & 0xff;
                        dma_scr = 0;
                        dma_sr = 1;
						mfp_gpip |= 0x20;
						fdc_status |= 0x1;
                        break;
					}
                }
                fdc_status |= 0x10;
                dma_sr = 1;
                break;
			case 0x90:			/* READ SECTOR multiple */
				count = dma_scr * 512;
				if (count+(fdc_sector-1)*512>disk[drives].sectors*512) count=disk[drives].sectors*512-(fdc_sector-1)*512;
                if (!discseek (drives, offset, 0)) {
					if (address<MEMSIZE){
						if (count == discread ((unsigned char *)buffer, 1, count, drives)) {
							address += count;
							dma_adrl = address & 0xff;
							dma_adrm = (address >> 8) & 0xff;
							dma_adrh = (address >> 16) & 0xff;
							dma_scr = 0;
							dma_sr = 1;
							fdc_sector += count/disk[drives].secsize;
							break;
						}
					}else{
                        address += count;
                        dma_adrl = address & 0xff;
                        dma_adrm = (address >> 8) & 0xff;
                        dma_adrh = (address >> 16) & 0xff;
                        dma_scr = 0;
                        dma_sr = 1;
						mfp_gpip |= 0x20;
						fdc_status |= 0x1;
                        break;
					}
				}
				fdc_status |= 0x10;
                dma_sr = 1;
                break;
            case 0xa0:          /* WRITE SECTOR */
                count = dma_scr * 512;
                if (!discseek (drives, offset, 0)) {
                    if (count == discwrite ((unsigned char *)buffer, 1, count, drives)) {
                        address += count;
                        dma_adrl = address & 0xff;
                        dma_adrm = (address >> 8) & 0xff;
                        dma_adrh = (address >> 16) & 0xff;
                        dma_scr = 0;
                        dma_sr = 1;
                        break;
                    }
                }
                fdc_status |= 0x10;
                dma_sr = 1;
                break;
            case 0xb0:          /* WRITE SECTOR multiple */
                count = dma_scr * 512;
                if (!discseek (drives, offset, 0)) {
                    if (count == discwrite ((unsigned char *)buffer, 1, count, drives)) {
                        address += count;
                        dma_adrl = address & 0xff;
                        dma_adrm = (address >> 8) & 0xff;
                        dma_adrh = (address >> 16) & 0xff;
                        dma_scr = 0;
                        dma_sr = 1;
                        fdc_sector += dma_scr * (512 / disk[drives].secsize);
                        break;
                    }
                }
                fdc_status |= 0x10;
                dma_sr = 1;
                break;
            case 0xc0:          /* READ ADDRESS */
                fdc_status |= 0x10;
                break;
            case 0xe0:          /* READ TRACK */
				count = disk[drives].sectors * 512;
				offset = disk[drives].secsize *
					(((disk[drives].sectors * disk[drives].sides * disk[drives].head))
					+ (disk[drives].sectors * sides));
                if (!discseek (drives, offset, 0)) {
					if (address<MEMSIZE){
						if (dma_scr==0x1f){
							count=0;
							address += 302;
						}
						if (count == discread ((unsigned char *)buffer, 1, count, drives)) {
							dma_adrl = address & 0xff;
							dma_adrm = (address >> 8) & 0xff;
							dma_adrh = (address >> 16) & 0xff;
							dma_scr = 0;
							dma_sr = 1;
							break;
						}
					}else{
                        address += 302;
                        dma_adrl = address & 0xff;
                        dma_adrm = (address >> 8) & 0xff;
                        dma_adrh = (address >> 16) & 0xff;
                        dma_scr = 0;
                        dma_sr = 1;
						mfp_gpip |= 0x20;
						fdc_status |= 0x1;
                        break;
					}
				}
				fdc_status |= 0x10;
                dma_sr = 1;
                break;
            case 0xf0:          /* WRITE TRACK */
                fdc_status |= 0x10;
                break;
            }
            if (disk[drives].head != fdc_track) {
                fdc_status |= 0x10;
            }
        } else {
            fdc_status |= 0x10; /* no drive selected */
        }
    }
    if (motor) {
        fdc_status |= 0x80;     /* motor on flag */
		fdc_motor=1;
	}
    if (!(fdc_status & 0x01)) { /* not busy */
        mfp_iprb |= (0x80 & mfp_ierb);  /* Request Interrupt */
        mfp_gpip &= ~0x20;
    }
}
