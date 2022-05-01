/*
 * Reinette II plus, a french Apple II emulator, using SDL2
 * and powered by puce6502 - a MOS 6502 cpu emulator by the same author
 * Last modified 21st of June 2021
 * Copyright (c) 2020 Arthur Ferreira (arthur.ferreira2@gmail.com)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include <SDL2/SDL.h>

#include "puce6502.h"
#include "dsk2nib.h"
#include "nib2dsk.h"

//#define SDL_RDR_SOFTWARE
//#define ENABLE_SL6

//#define ENABLE_DBG
#include "stb/dbg.h"
//#define ENABLE_LOG
#include "stb/log.h"

#ifdef ENABLE_LOG
#define DASM_6502
#endif

int debug=0;

#include "stb/stb_file.h"
#include "stb/filehelp.h"


#define SCREEN_RES_W	280
#define SCREEN_RES_H	192
#define SCREEN_BPP		8

// memory layout
#define RAMSIZE	 0xC000
#define ROMSTART 0xD000
#define ROMSIZE	 0x3000
#define FONTROMSIZE	 0x0800
uint8_t ram[RAMSIZE];	// 48K of ram in $000-$BFFF
uint8_t rom[ROMSIZE];	// 12K of rom in $D000-$FFFF
//uint8_t fontrom[FONTROMSIZE];	// 2K of rom

// language card
#define LGCSTART 0xD000
#define LGCSIZE	 0x3000
#define BK2START 0xD000
#define BK2SIZE	 0x1000
uint8_t lgc[LGCSIZE];	// Language Card 12K in $D000-$FFFF
uint8_t bk2[BK2SIZE];	// bank 2 of Language Card 4K in $D000-$DFFF

// disk ][ prom
#define SL6START 0xC600
#define SL6SIZE	 0x0100
uint8_t sl6[SL6SIZE];	// P5A disk ][ prom in slot 6

#include "rom/apple2plus.h"

//================================================================ SOFT SWITCHES

uint8_t KBD	  = 0;// $C000, $C010 ascii value of keyboard input
bool TEXT  = true;// $C050 CLRTEXT	 / $C051 SETTEXT
bool MIXED = false;// $C052 CLRMIXED / $C053 SETMIXED
bool PAGE2 = false;// $C054 PAGE2 off / $C055 PAGE2 on
bool HIRES = false;// $C056 GR		 / $C057 HGR
bool LCWR  = true;// Language Card writable
bool LCRD  = false;// Language Card readable
bool LCBK2 = true;// Language Card bank 2 enabled
bool LCWFF = false;// Language Card pre-write flip flop


//====================================================================== PADDLES

uint8_t PB0 = 0;// $C061 Push Button 0 (bit 7) / Open Apple
uint8_t PB1 = 0;// $C062 Push Button 1 (bit 7) / Solid Apple
uint8_t PB2 = 0;// $C063 Push Button 2 (bit 7) / shift mod !!!
float GCP[2] = { 127.0f, 127.0f };												// GC Position ranging from 0 (left) to 255 right
float GCC[2] = { 0.0f };														// $C064 (GC0) and $C065 (GC1) Countdowns
int GCD[2] = { 0 };// GC0 and GC1 Directions (left/down or right/up)
int GCA[2] = { 0 };// GC0 and GC1 Action (push or release)
uint8_t GCActionSpeed = 8;														// Game Controller speed at which it goes to the edges
uint8_t GCReleaseSpeed = 8;														// Game Controller speed at which it returns to center
long long int GCCrigger;														// $C070 the tick at which the GCs were reseted

inline static void resetPaddles() {
	GCC[0] = GCP[0] * GCP[0];													// initialize the countdown for both paddles
	GCC[1] = GCP[1] * GCP[1];													// to the square of their actuall values (positions)
	GCCrigger = ticks;															// records the time this was done
}

inline static uint8_t readPaddle(int pdl) {
	const float GCFreq = 6.6;													// the speed at which the GC values decrease

	GCC[pdl] -= (ticks - GCCrigger) / GCFreq;									// decreases the countdown
	if (GCC[pdl] <= 0)															// timeout
		return GCC[pdl] = 0;														// returns 0
	return 0x80;	// not timeout, return something with the MSB set
}


//====================================================================== SPEAKER

#define audioBufferSize 4096													// found to be large enought
Sint8 audioBuffer[2][audioBufferSize] = { 0 };									// see in main() for more details
SDL_AudioDeviceID audioDevice;
bool muted = false;// mute/unmute switch

static void playSound() {
	static long long int lastTick = 0LL;
	static bool SPKR = false;													// $C030 Speaker toggle

	if (!muted) {
		SPKR = !SPKR;// toggle speaker state
		Uint32 length = (int)((double)(ticks - lastTick) / 10.65625f);				// 1023000Hz / 96000Hz = 10.65625
		lastTick = ticks;
		if (length > audioBufferSize) length = audioBufferSize;
		SDL_QueueAudio(audioDevice, audioBuffer[SPKR], length | 1);					// | 1 TO HEAR HIGH FREQ SOUNDS
	}
}


//====================================================================== DISK ][

// DSK 143360/256/16 = 35
uint8_t dsk_buf[ MAX_TRACKS_PER_DISK*BYTES_PER_TRACK ];
uint8_t nib_buf[ MAX_TRACKS_PER_DISK*BYTES_PER_NIB_TRACK ];

int curDrv = 0;	// Current Drive - only one can be enabled at a time

struct drive {
	char		filename[400];													// the full disk image pathname
	int			dsk_type;
	bool		readOnly;														// based on the image file attributes
	uint8_t		data[MAX_TRACKS_PER_DISK*BYTES_PER_NIB_TRACK];					// nibblelized disk image
	int			max_tracks;
	bool		motorOn;// motor status
	bool		writeMode;														// writes to file are not implemented
	uint8_t		track;	// current track position
	uint16_t	nibble;// ptr to nibble under head position
} disk[2] = { 0 };// two disk ][ drive units

int phs[2][4]={{0,0,0,0},{0,0,0,0}};

//#include "apple2log.h"

int is_dsk_file(size_t flen)
{
	int trk;
	if(flen%BYTES_PER_TRACK) return 0;
	trk = flen/BYTES_PER_TRACK;
	if(trk>=35&&trk<=40) return trk;
	return 0;
}

int is_nib_file(size_t flen)
{
	int trk;
	if(flen%BYTES_PER_NIB_TRACK) return 0;
	trk = flen/BYTES_PER_NIB_TRACK;
	if(trk>=35&&trk<=40) return trk;
	return 0;
}

int insertFloppy(SDL_Window *wdo, char *filename, int drv) {
	FILE *f;
	size_t r_len;
	size_t flen = fn_filesize(filename);
	int trk, trk_dsk, trk_nib;

	disk[drv].dsk_type=0;
	disk[drv].max_tracks=0;

	trk = 0;
	trk_dsk = is_dsk_file(flen);
	trk_nib = is_nib_file(flen);
	if(trk_dsk) trk=trk_dsk;
	if(trk_nib) trk=trk_nib;
	if(trk==0) return 0;

	memset(disk[drv].data, 0, MAX_TRACKS_PER_DISK*BYTES_PER_NIB_TRACK);

	if(trk_dsk) {
		fread_buf_bin(filename, dsk_buf, flen, &r_len);
		if(r_len!=flen) return 0;
		dsk2nib( trk_dsk, DEFAULT_VOLUME, dsk_buf, nib_buf );
		memcpy(disk[drv].data, nib_buf, trk_dsk*BYTES_PER_NIB_TRACK);
		disk[drv].max_tracks=trk_dsk;
		disk[drv].dsk_type=2;
	}

	if(trk_nib) {
		f = fopen(filename, "rb");												// open file in read binary mode
		if (!f || fread(disk[drv].data, 1, flen, f) != flen)					// load it into memory and check size
			return 0;
		fclose(f);
		disk[drv].max_tracks=trk_nib;
		disk[drv].dsk_type=1;
	}

	sprintf(disk[drv].filename, "%s", filename);								// update disk filename record

	f = fopen(filename, "ab");													// try to open the file in append binary mode
	if (f) {		// success, file is writable
		disk[drv].readOnly = false;												// update the readOnly flag
		fclose(f);	// and close it untouched
	} else {
		disk[drv].readOnly = true;												// f is NULL, no writable, no need to close it
	}
	char title[1000];// UPDATE WINDOW TITLE
	int i, a, b;

	i = a = 0;
	while (disk[0].filename[i] != 0)											// find start of filename for disk0
		if (disk[0].filename[i++] == '\\') a = i;
	i = b = 0;
	while (disk[1].filename[i] != 0)											// find start of filename for disk1
		if (disk[1].filename[i++] == '\\') b = i;

	sprintf(title, "Reinette ][+   D1: %s	D2: %s", disk[0].filename + a, disk[1].filename + b);
	SDL_SetWindowTitle(wdo, title);												// updates window title

	return 1;
}

#ifdef LOADDSK
int loadFloppy(SDL_Window *wdo, char *filename, const uint8_t* data, int data_len, int drv) {
	FILE *f;
	size_t r_len;
	size_t flen = data_len;
	int trk, trk_dsk, trk_nib;

	disk[drv].dsk_type=0;
	disk[drv].max_tracks=0;

	trk = 0;
	trk_dsk = is_dsk_file(flen);
	trk_nib = is_nib_file(flen);
	if(trk_dsk) trk=trk_dsk;
	if(trk_nib) trk=trk_nib;
	if(trk==0) return 0;

	memset(disk[drv].data, 0, MAX_TRACKS_PER_DISK*BYTES_PER_NIB_TRACK);

	if(trk_dsk) {
		memcpy(dsk_buf, data, flen);
		dsk2nib( trk_dsk, DEFAULT_VOLUME, dsk_buf, nib_buf );
		memcpy(disk[drv].data, nib_buf, trk_dsk*BYTES_PER_NIB_TRACK);
		disk[drv].max_tracks=trk_dsk;
		disk[drv].dsk_type=2;
	}

	if(trk_nib) {
		memcpy(disk[drv].data, data, flen);
		disk[drv].max_tracks=trk_nib;
		disk[drv].dsk_type=1;
	}

	sprintf(disk[drv].filename, "%s", filename);								// update disk filename record

	disk[drv].readOnly = false;

	char title[1000];// UPDATE WINDOW TITLE
	int i, a, b;

	i = a = 0;
	while (disk[0].filename[i] != 0)											// find start of filename for disk0
		if (disk[0].filename[i++] == '\\') a = i;
	i = b = 0;
	while (disk[1].filename[i] != 0)											// find start of filename for disk1
		if (disk[1].filename[i++] == '\\') b = i;

	sprintf(title, "Reinette ][+   D1: %s	D2: %s", disk[0].filename + a, disk[1].filename + b);
	SDL_SetWindowTitle(wdo, title);												// updates window title

	return 1;
}
#endif

int saveFloppy(int drive) {
	size_t sz;
	if (!disk[drive].filename[0]) return 0;										// no file loaded into drive
	if (disk[drive].readOnly) return 0;											// file is read only write no aptempted
	if (disk[drive].dsk_type==0) return 0;

	// DSK
	if(disk[drive].dsk_type==2) {
		if(nib2dsk( dsk_buf, disk[drive].data, disk[drive].max_tracks)) {
			sz = disk[drive].max_tracks*BYTES_PER_TRACK;
			if(fwrite_buf_bin(disk[drive].filename, dsk_buf, sz)!=sz)
				return 0;
			else
				return 1;
		}
	}

	// NIB
	sz = disk[drive].max_tracks*BYTES_PER_NIB_TRACK;

	if(fwrite_buf_bin(disk[drive].filename, disk[drive].data, sz)!=sz)
		return 0;
	else
		return 1;
}

/*
int halfTrackPos[2] = { 0 };

void stepMotor(uint16_t address) {
	static bool phases[2][4] = { 0 };											// phases states (for both drives)
	static bool phasesB[2][4] = { 0 };											// phases states Before
	static bool phasesBB[2][4] = { 0 };											// phases states Before Before
	static int pIdx[2] = { 0 };													// phase index (for both drives)
	static int pIdxB[2] = { 0 };												// phase index Before
	//static int halfTrackPos[2] = { 0 };

	address &= 7;
	int phase = address >> 1;

	phasesBB[curDrv][pIdxB[curDrv]] = phasesB[curDrv][pIdxB[curDrv]];
	phasesB[curDrv][pIdx[curDrv]]	= phases[curDrv][pIdx[curDrv]];
	pIdxB[curDrv] = pIdx[curDrv];
	pIdx[curDrv]  = phase;

	if (!(address & 1)) {														// head not moving (PHASE x OFF)
		phases[curDrv][phase] = false;
		return;
	}

	if ((phasesBB[curDrv][(phase + 1) & 3]) && (--halfTrackPos[curDrv] < 0))	// head is moving in
		halfTrackPos[curDrv] = 0;

//	if ((phasesBB[curDrv][(phase - 1) & 3]) && (++halfTrackPos[curDrv] > 140))	// head is moving out
//		halfTrackPos[curDrv] = 140;
	if ((phasesBB[curDrv][(phase - 1) & 3]) && (++halfTrackPos[curDrv] > MAX_TRACKS_PER_DISK*4-2))	// head is moving out
		halfTrackPos[curDrv] = MAX_TRACKS_PER_DISK*4-2;

	phases[curDrv][phase] = true;												// update track#
	disk[curDrv].track = (halfTrackPos[curDrv] + 1) / 2;
}
*/

int quarterTrackPos[2] = { 0 };

void stepMotorQ(uint16_t address) {
	address &= 7;
	int phase = address >> 1;

	int ph[4];
	int q, qT;

	phs[curDrv][phase] = address&1;

	for(int i=0;i<4;i++) ph[i]=phs[curDrv][i];
	if(ph[0]==ph[2]) ph[0]=ph[2]=0;
	if(ph[1]==ph[3]) ph[1]=ph[3]=0;

	q=8;
		if(ph[0]) { q=0; if(ph[1]) q=1; if(ph[3]) q=7; }
	else
		if(ph[1]) { q=2; if(ph[2]) q=3; }
	else
		if(ph[2]) { q=4; if(ph[3]) q=5; }
	else
		if(ph[3]) { q=6; }

	if(!disk[curDrv].motorOn) return;

	if(q!=8) {
		qT=quarterTrackPos[curDrv]&0x7;
		if(q<qT) q = q+8-qT; else q = q-qT;
		// q 0--7
		if(q>=1&&q<=3) quarterTrackPos[curDrv]+=q;
		if(q>=5&&q<=7) quarterTrackPos[curDrv]+=q-8;
		if (quarterTrackPos[curDrv] < 0)	quarterTrackPos[curDrv] = 0;
		if (quarterTrackPos[curDrv] >= MAX_TRACKS_PER_DISK*8-4)	quarterTrackPos[curDrv] = MAX_TRACKS_PER_DISK*8-4;

		//showDiskMotor(address, q);
	}

	disk[curDrv].track = (quarterTrackPos[curDrv] + 1) / 4;
}

void setDrv(int drv) {
	disk[drv].motorOn = disk[!drv].motorOn || disk[drv].motorOn;				// if any of the motors were ON
	disk[!drv].motorOn = false;													// motor of the other drive is set to OFF
	curDrv = drv;	// set the current drive
}

void apple2_reset()
{
	KBD	  = 0;			// $C000, $C010 ascii value of keyboard input
	TEXT  = true;		// $C050 CLRTEXT  / $C051 SETTEXT
	MIXED = false;		// $C052 CLRMIXED / $C053 SETMIXED
	PAGE2 = false;		// $C054 PAGE2 off / $C055 PAGE2 on
	HIRES = false;		// $C056 GR		  / $C057 HGR
	LCWR  = true;		// Language Card writable
	LCRD  = false;		// Language Card readable
	LCBK2 = true;		// Language Card bank 2 enabled
	LCWFF = false;		// Language Card pre-write flip flop
}

//========================================== MEMORY MAPPED SOFT SWITCHES HANDLER
// this function is called from readMem and writeMem
// it complements both functions when address is in page $C0
uint8_t softSwitches(uint16_t address, uint8_t value, bool WRT) {
	static uint8_t dLatch = 0;													// disk ][ I/O register

	switch (address) {
	case 0xC000: return KBD;													// KEYBOARD
	case 0xC010: KBD &= 0x7F; return KBD;										// KBDSTROBE

	case 0xC020:// TAPEOUT (shall we listen it ? - try SAVE from applesoft)
	case 0xC030:// SPEAKER
	case 0xC033: playSound(); break;											// apple invader uses $C033 to output sound !

	case 0xC050: TEXT  = false; break;											// Graphics
	case 0xC051: TEXT  = true;	break;											// Text
	case 0xC052: MIXED = false; break;											// Mixed off
	case 0xC053: MIXED = true;	break;											// Mixed on
	case 0xC054: PAGE2 = false; break;											// PAGE2 off
	case 0xC055: PAGE2 = true;	break;											// PAGE2 on
	case 0xC056: HIRES = false; break;											// HiRes off
	case 0xC057: HIRES = true;	break;											// HiRes on

	case 0xC061: return PB0;													// Push Button 0
	case 0xC062: return PB1;													// Push Button 1
	case 0xC063: return PB2;													// Push Button 2
	case 0xC064: return readPaddle(0);											// Paddle 0
	case 0xC065: return readPaddle(1);											// Paddle 1

	case 0xC070: resetPaddles(); break;											// paddle timer RST

	case 0xC080:// LANGUAGE CARD :
	case 0xC084: LCBK2 = 1; LCRD = 1; LCWR = 0;		 LCWFF = 0;	   break;		// LC2RD
	case 0xC081:
	case 0xC085: LCBK2 = 1; LCRD = 0; LCWR |= LCWFF; LCWFF = !WRT; break;		// LC2WR
	case 0xC082:
	case 0xC086: LCBK2 = 1; LCRD = 0; LCWR = 0;		 LCWFF = 0;	   break;		// ROMONLY2
	case 0xC083:
	case 0xC087: LCBK2 = 1; LCRD = 1; LCWR |= LCWFF; LCWFF = !WRT; break;		// LC2RW
	case 0xC088:
	case 0xC08C: LCBK2 = 0; LCRD = 1; LCWR = 0;		 LCWFF = 0;	   break;		// LC1RD
	case 0xC089:
	case 0xC08D: LCBK2 = 0; LCRD = 0; LCWR |= LCWFF; LCWFF = !WRT; break;		// LC1WR
	case 0xC08A:
	case 0xC08E: LCBK2 = 0; LCRD = 0; LCWR = 0;		 LCWFF = 0;	   break;		// ROMONLY1
	case 0xC08B:
	case 0xC08F: LCBK2 = 0; LCRD = 1; LCWR |= LCWFF; LCWFF = !WRT; break;		// LC1RW

	case 0xC0E0:
	case 0xC0E1:
	case 0xC0E2:
	case 0xC0E3:
	case 0xC0E4:
	case 0xC0E5:
	case 0xC0E6:
	case 0xC0E7: stepMotorQ(address); break;									// MOVE DRIVE HEAD
	//case 0xC0E7: stepMotor(address); break;									// MOVE DRIVE HEAD

	case 0xCFFF:																// turn off all slots expansion ROMs - TODO : NEEDS REWORK
	case 0xC0E8: disk[curDrv].motorOn = false; break;							// MOTOROFF
	case 0xC0E9: disk[curDrv].motorOn = true;  break;							// MOTORON

	case 0xC0EA: setDrv(0); break;												// DRIVE0EN
	case 0xC0EB: setDrv(1); break;												// DRIVE1EN

	case 0xC0EC:// Shift Data Latch
		if (disk[curDrv].writeMode)												// writting
			disk[curDrv].data[disk[curDrv].track*0x1A00+disk[curDrv].nibble]=dLatch;// good luck gcc
		else		// reading
			dLatch=disk[curDrv].data[disk[curDrv].track*0x1A00+disk[curDrv].nibble];// easy peasy
		disk[curDrv].nibble = (disk[curDrv].nibble + 1) % 0x1A00;				// turn floppy of 1 nibble
		return dLatch;

	case 0xC0ED: dLatch = value; break;											// Load Data Latch

	case 0xC0EE:// latch for READ
		disk[curDrv].writeMode = false;
		return disk[curDrv].readOnly ? 0x80 : 0;								// check protection

	case 0xC0EF: disk[curDrv].writeMode = true; break;							// latch for WRITE
	}
	return ticks % 0xFF;														// catch all, gives a 'floating' value
}


//======================================================================= MEMORY
// these two functions are imported into puce6502.c

uint8_t readMem(uint16_t address) {
	if (address < RAMSIZE)
		return ram[address];														// RAM

	if (address >= ROMSTART) {
		if (!LCRD)
			return rom[address - ROMSTART];											// ROM

		if (LCBK2 && (address < 0xE000))
			return bk2[address - BK2START];											// BK2

		return lgc[address - LGCSTART];												// LC
	}

	if ((address & 0xFF00) == SL6START)
		return sl6[address - SL6START];												// disk][

	if ((address & 0xF000) == 0xC000)
		return softSwitches(address, 0, false);										// Soft Switches

	return ticks & 0xFF;														// catch all, gives a 'floating' value
}


void writeMem(uint16_t address, uint8_t value) {
	if (address < RAMSIZE) {
		ram[address] = value;														// RAM
		return;
	}

	if (LCWR && (address >= ROMSTART)) {
		if (LCBK2 && (address < 0xE000)) {
			bk2[address - BK2START] = value;										// BK2
			return;
		}
		lgc[address - LGCSTART] = value;											// LC
		return;
	}

	if ((address & 0xF000) == 0xC000) {
		softSwitches(address, value, true);											// Soft Switches
		return;
	}
}

void CpuExec(unsigned long long int cycleCount)
{
	unsigned int cycles_count=0;
	unsigned int cycles=0;

	while(cycles_count<cycleCount) {
		cycles=puce6502Step();
		cycles_count += cycles;
		ticks += cycles;

#ifdef DASM_6502
		char disasm[256];
		dasm(getPC(), disasm);
		LOG("%s\n", disasm);
#endif
	}
}

void SysInit()
{
}

void SysReset()
{
	apple2_reset();

	// reset the CPU
	puce6502RST();	// reset the 6502
}

//=======================================================================
/*
int readBinFile(uint8_t *buf, uint32_t sz, char *fn, char* workdir, int wd_len)
{
	int r = 0;
	FILE *f;
	char msg[512];
	workdir[wd_len] = 0;

	f = fopen(strncat(workdir, fn, 256), "rb");				// load the file

	if (f) {
		if (fread(buf, 1, sz, f) != sz) {							// the file is too small
			sprintf(msg, "%s should be exactly %d %s", fn, (sz>=1024)?sz/1024:sz, (sz>=1024)?"KB":"bytes");
			r=1;
		}
		fclose(f);
	} else {
		sprintf(msg, "Could not locate %s in the rom folder", fn);
		r=2;
	}

	if(r)
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Fatal error", msg, NULL);

	return r;
}
*/


#ifdef LOADDSK
#include "dsk.h"
#endif

//========================================================== PROGRAM ENTRY POINT

int main(int argc, char *argv[]) {

	//========================================================= SDL INITIALIZATION
	int zoom = 2;
	int fullscreen = 0;
	int color_mode = 0;

	uint8_t tries = 0;															// for disk ][ speed-up access
	SDL_Event event;
	SDL_bool running = true, paused = false, ctrl = false, shift = false, alt = false;

#ifdef LOADDSK
	fullscreen = 1;
#endif
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
		printf("failed to initialize SDL2 : %s", SDL_GetError());
		return -1;
	}

	//SDL_Window *wdo = SDL_CreateWindow("Reinette ][+", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_RES_W * zoom, SCREEN_RES_H * zoom, SDL_WINDOW_OPENGL);
	SDL_Window *wdo = SDL_CreateWindow("Reinette ][+", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_RES_W * zoom, SCREEN_RES_H * zoom, SDL_WINDOW_RESIZABLE);
#ifdef SDL_RDR_SOFTWARE
	SDL_Renderer *rdr = SDL_CreateRenderer(wdo, -1, SDL_RENDERER_SOFTWARE);
	//SDL_Renderer *rdr = SDL_CreateRenderer(wdo, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_PRESENTVSYNC);	// SDL_RENDERER_PRESENTVSYNC 无效
#else
	SDL_Renderer *rdr = SDL_CreateRenderer(wdo, -1, SDL_RENDERER_ACCELERATED);
	//SDL_Renderer *rdr = SDL_CreateRenderer(wdo, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
#endif
	//SDL_SetRenderDrawBlendMode(rdr, SDL_BLENDMODE_NONE);							// SDL_BLENDMODE_BLEND);
	SDL_EventState(SDL_DROPFILE, SDL_ENABLE);									// ask SDL2 to read dropfile events
	//SDL_RenderSetScale(rdr, zoom, zoom);
	SDL_Surface *sshot;															// used later for the screenshots

	SDL_SetWindowMinimumSize(wdo, SCREEN_RES_W, SCREEN_RES_H);

	if(fullscreen) SDL_SetWindowFullscreen(wdo, SDL_WINDOW_FULLSCREEN_DESKTOP);

	unsigned char screenData[SCREEN_RES_W*SCREEN_RES_H];
	SDL_Color colors[128+32];

	SDL_Surface *sdlSurface = SDL_CreateRGBSurface(0, SCREEN_RES_W, SCREEN_RES_H, 32, 0,0,0,0);
	SDL_Surface *sdlScreen = SDL_CreateRGBSurfaceFrom((void*)screenData, SCREEN_RES_W, SCREEN_RES_H, SCREEN_BPP, SCREEN_RES_W*1, 0, 0, 0, 0);

/*
	const int color[16][3] = {													// the 16 low res colors
		{ 0,   0,	0	  }, { 226, 57,	 86	 }, { 28,  116, 205 }, { 126, 110, 173 },
		{ 31,  129, 128 }, { 137, 130, 122 }, { 86,	 168, 228 }, { 144, 178, 223 },
		{ 151, 88,	34	}, { 234, 108, 21  }, { 158, 151, 143 }, { 255, 206, 240 },
		{ 144, 192, 49	}, { 255, 253, 166 }, { 159, 210, 213 }, { 255, 255, 255 }
	};
*/
	// mame
	const uint8_t color[16][3] = {													// the 16 low res colors
		{0x00, 0x00, 0x00},// Black
		{0xa7, 0x0b, 0x40},// Dark Red
		{0x40, 0x1c, 0xf7},// Dark Blue
		{0xe6, 0x28, 0xff},// Purple
		{0x00, 0x74, 0x40},// Dark Green
		{0x80, 0x80, 0x80},// Dark Gray
		{0x19, 0x90, 0xff},// Medium Blue
		{0xbf, 0x9c, 0xff},// Light Blue

		{0x40, 0x63, 0x00},// Brown
		{0xe6, 0x6f, 0x00},// Orange
		{0x80, 0x80, 0x80},// Light Grey
		{0xff, 0x8b, 0xbf},// Pink
		{0x19, 0xd7, 0x00},// Light Green
		{0xbf, 0xe3, 0x08},// Yellow
		{0x58, 0xf4, 0xbf},// Aquamarine
		{0xff, 0xff, 0xff} // White
	};

// 黑 深红 深蓝   紫 灰   蓝 浅蓝 深橙
// 棕   橙   灰 深红 绿 浅橙 浅绿   白

	const uint8_t hcolor[16][3] = {													// the high res colors (2 light levels)
		{ 0,   0,	0	}, { 144, 192, 49  }, { 126, 110, 173 }, { 255, 255, 255 },
		{ 0,   0,	0	}, { 234, 108, 21  }, { 86,	 168, 228 }, { 255, 255, 255 },
		{ 0,   0,	0	}, { 63,  55,  86  }, { 72,	 96,  25  }, { 255, 255, 255 },
		{ 0,   0,	0	}, { 43,  84,  114 }, { 117, 54,  10  }, { 255, 255, 255 }
	};

	const uint8_t hcolor_5[32][3] = {													// the high res colors (2 light levels)
		{ 0,   0,	0	},	// 00 000 黑
		{ 0,   0,	0	},	// 00 001 ？黑
		{ 255, 0,	255 },	// 00 010 紫
		{ 255, 255, 255 },	// 00 011 白
		{ 0,   0,	0	},	// 00 100 ？黑
		{ 0,   0,	0	},	// 00 101 ？黑
		{ 255, 255, 255 },	// 00 110 白
		{ 255, 255, 255 },	// 00 111 白

		{ 0,   0,	0	},	// 01 000 黑
		{ 0,   0,	0	},	// 01 001 ？黑
		{ 0,   0,	255 },	// 01 010 绿
		{ 255, 255, 255 },	// 01 011 白
		{ 0,   0,	0	},	// 01 100 ？黑
		{ 0,   0,	0	},	// 01 101 ？黑
		{ 255, 255, 255 },	// 01 110 白
		{ 255, 255, 255 },	// 01 111 白

		{ 0,   0,	0	},	// 10 000 黑
		{ 0,   0,	0	},	// 10 001 ？黑
		//{ 0,	 160, 255 },	// 10 010 蓝
		{ 50,  170, 220 },	// 10 010 蓝
		{ 255, 255, 255 },	// 10 011 白
		{ 0,   0,	0	},	// 10 100 ？黑
		{ 0,   0,	0	},	// 10 101 ？黑
		{ 255, 255, 255 },	// 10 110 白
		{ 255, 255, 255 },	// 10 111 白

		{ 0,   0,	0	},	// 11 000 黑
		{ 0,   0,	0	},	// 11 001 ？黑
		//{ 255, 128, 64  },	// 11 010 橙
		{ 255, 108, 64	},	// 11 010 橙
		{ 255, 255, 255 },	// 11 011 白
		{ 0,   0,	0	},	// 11 100 ？黑
		{ 0,   0,	0	},	// 11 101 ？黑
		{ 255, 255, 255 },	// 11 110 白
		{ 255, 255, 255 }	// 11 111 白
	};

	Uint8 color_r, color_g, color_b;
	int color_off;

	// color
	color_off = 0;
	for(int i=0;i<16;i++) {
		colors[i+color_off].r = color[i][0]; colors[i+color_off].g = color[i][1]; colors[i+color_off].b = color[i][2]; colors[i+color_off].a = 0xff;
	}
	for(int i=0;i<16;i++) {
		colors[i+color_off+16].r = hcolor[i][0]; colors[i+color_off+16].g = hcolor[i][1]; colors[i+color_off+16].b = hcolor[i][2]; colors[i+color_off+16].a = 0xff;
	}

	// green
	color_off = 32;
	color_r=color_g=color_b=0;
	for(int i=0;i<16;i++) {
		colors[i+color_off].r = color_r; colors[i+color_off].g = color_g; colors[i+color_off].b = color_b; colors[i+color_off].a = 0xff;
		color_g+=0x10;
	}

	color_r=color_g=color_b=0;
	for(int i=0;i<2;i++) {
		colors[i+color_off+16].r = color_r; colors[i+color_off+16].g = color_g; colors[i+color_off+16].b = color_b; colors[i+color_off+16].a = 0xff;
		color_g+=0xf0;
	}

	// gamber
	color_off = 32*2;
	color_r=color_g=color_b=0;
	for(int i=0;i<16;i++) {
		colors[i+color_off].r = color_r; colors[i+color_off].g = color_g; colors[i+color_off].b = color_b; colors[i+color_off].a = 0xff;
		color_r+=0x10;color_g+=0x08;
	}

	color_r=color_g=color_b=0;
	for(int i=0;i<2;i++) {
		colors[i+color_off+16].r = color_r; colors[i+color_off+16].g = color_g; colors[i+color_off+16].b = color_b; colors[i+color_off+16].a = 0xff;
		color_r+=0xf0;color_g+=0x78;
	}

	// white
	color_off = 32*3;
	color_r=color_g=color_b=0;
	for(int i=0;i<16;i++) {
		colors[i+color_off].r = color_r; colors[i+color_off].g = color_g; colors[i+color_off].b = color_b; colors[i+color_off].a = 0xff;
		color_r+=0x10;color_g+=0x10;color_b+=0x10;
	}

	color_r=color_g=color_b=0;
	for(int i=0;i<2;i++) {
		colors[i+color_off+16].r = color_r; colors[i+color_off+16].g = color_g; colors[i+color_off+16].b = color_b; colors[i+color_off+16].a = 0xff;
		color_r+=0xf0;color_g+=0xf0;color_b+=0xf0;
	}

	// 5 bits
	color_off = 32*4;
	for(int i=0;i<32;i++) {
		colors[i+color_off].r = hcolor_5[i][0]; colors[i+color_off].g = hcolor_5[i][1]; colors[i+color_off].b = hcolor_5[i][2]; colors[i+color_off].a = 0xff;
	}

	SDL_SetPaletteColors(sdlScreen->format->palette, colors, 0, 128+32);

	//=================================================== SDL AUDIO INITIALIZATION

	SDL_AudioSpec desired = { 96000, AUDIO_S8, 1, 0, 4096, 0, 0, NULL, NULL };
	audioDevice = SDL_OpenAudioDevice(NULL, 0, &desired, NULL, SDL_FALSE);		// get the audio device ID
	SDL_PauseAudioDevice(audioDevice, muted);									// unmute it (muted is false)
	uint8_t volume = 4;

	for (int i = 0; i < audioBufferSize; i++) {									// two audio buffers,
		audioBuffer[true][i] = volume;												// one used when SPKR is true
		audioBuffer[false][i] = -volume;											// the other when SPKR is false
	}

	//===================================== VARIABLES USED IN THE VIDEO PRODUCTION

	//int TextCache[24][40] = { 0 };
	//int LoResCache[24][40] = { 0 };
	//int HiResCache[192][40] = { 0 };												// check which Hi-Res 7 dots needs redraw
	//uint8_t previousBit[192][40] = { 0 };											// the last bit value of the byte before.

	enum characterAttribute { A_NORMAL, A_INVERSE, A_FLASH } glyphAttr;			// character attribute in TEXT
	uint8_t flashCycle = 0;														// TEXT cursor flashes at 2Hz

	SDL_Rect drvRect[2] = { { 272, 188, 4, 4 }, { 276, 188, 4, 4 } };			// disk drive status squares

	const int offsetGR[24] = {													// helper for TEXT and GR video generation
		0x0000, 0x0080, 0x0100, 0x0180, 0x0200, 0x0280, 0x0300, 0x0380,				// lines 0-7
		0x0028, 0x00A8, 0x0128, 0x01A8, 0x0228, 0x02A8, 0x0328, 0x03A8,				// lines 8-15
		0x0050, 0x00D0, 0x0150, 0x01D0, 0x0250, 0x02D0, 0x0350, 0x03D0				// lines 16-23
	};

	const int offsetHGR[192] = {												// helper for HGR video generation
		0x0000, 0x0400, 0x0800, 0x0C00, 0x1000, 0x1400, 0x1800, 0x1C00,				// lines 0-7
		0x0080, 0x0480, 0x0880, 0x0C80, 0x1080, 0x1480, 0x1880, 0x1C80,				// lines 8-15
		0x0100, 0x0500, 0x0900, 0x0D00, 0x1100, 0x1500, 0x1900, 0x1D00,				// lines 16-23
		0x0180, 0x0580, 0x0980, 0x0D80, 0x1180, 0x1580, 0x1980, 0x1D80,
		0x0200, 0x0600, 0x0A00, 0x0E00, 0x1200, 0x1600, 0x1A00, 0x1E00,
		0x0280, 0x0680, 0x0A80, 0x0E80, 0x1280, 0x1680, 0x1A80, 0x1E80,
		0x0300, 0x0700, 0x0B00, 0x0F00, 0x1300, 0x1700, 0x1B00, 0x1F00,
		0x0380, 0x0780, 0x0B80, 0x0F80, 0x1380, 0x1780, 0x1B80, 0x1F80,
		0x0028, 0x0428, 0x0828, 0x0C28, 0x1028, 0x1428, 0x1828, 0x1C28,
		0x00A8, 0x04A8, 0x08A8, 0x0CA8, 0x10A8, 0x14A8, 0x18A8, 0x1CA8,
		0x0128, 0x0528, 0x0928, 0x0D28, 0x1128, 0x1528, 0x1928, 0x1D28,
		0x01A8, 0x05A8, 0x09A8, 0x0DA8, 0x11A8, 0x15A8, 0x19A8, 0x1DA8,
		0x0228, 0x0628, 0x0A28, 0x0E28, 0x1228, 0x1628, 0x1A28, 0x1E28,
		0x02A8, 0x06A8, 0x0AA8, 0x0EA8, 0x12A8, 0x16A8, 0x1AA8, 0x1EA8,
		0x0328, 0x0728, 0x0B28, 0x0F28, 0x1328, 0x1728, 0x1B28, 0x1F28,
		0x03A8, 0x07A8, 0x0BA8, 0x0FA8, 0x13A8, 0x17A8, 0x1BA8, 0x1FA8,
		0x0050, 0x0450, 0x0850, 0x0C50, 0x1050, 0x1450, 0x1850, 0x1C50,
		0x00D0, 0x04D0, 0x08D0, 0x0CD0, 0x10D0, 0x14D0, 0x18D0, 0x1CD0,
		0x0150, 0x0550, 0x0950, 0x0D50, 0x1150, 0x1550, 0x1950, 0x1D50,
		0x01D0, 0x05D0, 0x09D0, 0x0DD0, 0x11D0, 0x15D0, 0x19D0, 0x1DD0,
		0x0250, 0x0650, 0x0A50, 0x0E50, 0x1250, 0x1650, 0x1A50, 0x1E50,
		0x02D0, 0x06D0, 0x0AD0, 0x0ED0, 0x12D0, 0x16D0, 0x1AD0, 0x1ED0,				// lines 168-183
		0x0350, 0x0750, 0x0B50, 0x0F50, 0x1350, 0x1750, 0x1B50, 0x1F50,				// lines 176-183
		0x03D0, 0x07D0, 0x0BD0, 0x0FD0, 0x13D0, 0x17D0, 0x1BD0, 0x1FD0				// lines 184-191
	};


	//================================= LOAD NORMAL AND REVERSE CHARACTERS BITMAPS

	char workDir[1000];															// find the working directory
	int workDirSize = 0, i = 0;
	while (argv[0][i] != '\0') {
		workDir[i] = argv[0][i];
		if (argv[0][++i] == '\\') workDirSize = i + 1;								// find the last '/' if any
	}

	//================================================================== LOAD ROMS

/*
	if(readBinFile(rom,		ROMSIZE,	 "roms/apple2/appleII+.rom", workDir, workDirSize)) return 1;
	if(readBinFile(fontrom, FONTROMSIZE, "roms/apple2/a2.chr",		 workDir, workDirSize)) return 1;
#ifdef ENABLE_SL6
	if(readBinFile(sl6,		SL6SIZE,	 "roms/apple2/diskII.rom",	 workDir, workDirSize)) return 1;
#endif
*/
	memcpy(rom, apple2plus_rom, ROMSIZE);
#ifdef ENABLE_SL6
	memcpy(sl6, disk2rom, SL6SIZE);
#endif

	SysInit();

	//========================================================== VM INITIALIZATION

	if (argc > 1)
		insertFloppy(wdo, argv[1], 0);									// load floppy if provided at command line
#ifdef LOADDSK
	else {

		if(fexist(dsk1_fn))
			insertFloppy(wdo, dsk1_fn, 0);
		else
			loadFloppy(wdo, dsk1_fn, dsk1_data, dsk1_len, 0);

#ifdef DOUBLE_DISK
		if(fexist(dsk2_fn))
			insertFloppy(wdo, dsk2_fn, 1);
		else
			loadFloppy(wdo, dsk2_fn, dsk2_data, dsk2_len, 1);
#endif// DOUBLE_DISK

	}

#else
/*
	else {
		// STC2.0
		color_mode = 1;
		insertFloppy(wdo, "stc.sy.dsk", 0);
		insertFloppy(wdo, "stc.lib.dsk", 1);
	}
*/
#endif

	SysReset();

	// dirty hack, fix soon... if I understand why
	ram[0x4D] = 0xAA;	// Joust crashes if this memory location equals zero
	ram[0xD0] = 0xAA;	// Planetoids won't work if this memory location equals zero

	//================================================================== MAIN LOOP
	uint64_t ticks_step=1;
	uint64_t last_ticks = SDL_GetTicks64();
	uint64_t current_ticks;

	while (running) {

		if (!paused) {// the apple II is clocked at 1023000.0 Hhz
			//puce6502Exec(17050);														// execute instructions for 1/60 of a second
			//while (disk[curDrv].motorOn && ++tries)									// until motor is off or i reaches 255+1=0
			//	puce6502Exec(5000);														// speed up drive access artificially

			CpuExec(17050);														// execute instructions for 1/60 of a second
			while (disk[curDrv].motorOn && ++tries)									// until motor is off or i reaches 255+1=0
				CpuExec(5000);													// speed up drive access artificially
		}


		//=============================================================== USER INPUT

		while(1) {

		while (SDL_PollEvent(&event)) {
			alt	  = SDL_GetModState() & KMOD_ALT   ? true : false;
			ctrl  = SDL_GetModState() & KMOD_CTRL  ? true : false;
			shift = SDL_GetModState() & KMOD_SHIFT ? true : false;
			PB0 = alt	? 0xFF : 0x00;												// update push button 0
			PB1 = ctrl	? 0xFF : 0x00;												// update push button 1
			PB2 = shift ? 0xFF : 0x00;												// update push button 2

			if (event.type == SDL_QUIT) running = false;							// WM sent TERM signal

			// if (event.type == SDL_WINDOWEVENT) {										// pause if the window loses focus
			//	 if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
			//	   paused = true;
			//	 else if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
			//	   paused = false;
			// }

			if (event.type == SDL_DROPFILE) {										// user dropped a file
				char *filename = event.drop.file;										// get full pathname
				if (!insertFloppy(wdo, filename, alt)) {								// if ALT is pressed : drv 1 else drv 0
					if(fullscreen) {SDL_SetWindowFullscreen(wdo, 0); fullscreen=0;}
					SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Load", "Not a valid nib file", NULL);
				}
				SDL_free(filename);														// free filename memory
				paused = false;															// might already be the case

				if (!(alt || ctrl)) {													// if ALT or CTRL were not pressed
					ram[0x3F4] = 0;														// unset the Power-UP byte
					SysReset();														// do a cold reset
					memset(ram, 0, sizeof(ram));
				}
			}

			if (event.type == SDL_KEYDOWN) {										// a key has been pressed
				switch (event.key.keysym.sym) {

				// EMULATOR CONTROLS :

				case SDLK_F1:														// help box
					if(fullscreen) {SDL_SetWindowFullscreen(wdo, 0); fullscreen=0;}
					SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Help",
						"\tReinette ][ plus v0.4.8\n"
						"\n"
						"F1\tthis help\n"
						"\n"
						"F2\tsave a screenshot into the screenshots directory\n"
						"F3\tpaste text from clipboard\n"
						"\n"
						"F4\tmute / un-mute sound\n"
						"shift/ctrl F4\tincrease/decrease volume\n"
						"\n"
						"F5\treset joystick release speed\n"
						"shift/ctrl F5\tincrease/decrease joystick release speed\n"
						"\n"
						"F6\treset joystick action speed\n"
						"shift/ctrl F6\tincrease/decrease joystick action speed\n"
						"\n"
						"F7\tfullscreen\n"
						"shift/ctrl F7\tincrease zoom up/down to 6/1\n"
						"\n"
						"ctrl F9\twrites the changes of the floppy in drive 0\n"
						"alt F9\twrites the changes of the floppy in drive 1\n"
						"\n"
						"F11\tpause / un-pause the emulator\n"
						"\n"
						"ctrl F12\treset\n"
						"\n"
						"More information at github.com/ArthurFerreira2\n", NULL);
						{ticks_step=1;last_ticks=SDL_GetTicks64();}
				break;

				case SDLK_F2: {															// SCREENSHOTS
					sshot = SDL_GetWindowSurface(wdo);
					SDL_RenderReadPixels(rdr, NULL, SDL_GetWindowPixelFormat(wdo), sshot->pixels, sshot->pitch);
					workDir[workDirSize] = 0;
					int i = -1, a = 0, b = 0;
					while (disk[0].filename[++i] != '\0') {
						if (disk[0].filename[i] == '\\') a = i;
						if (disk[0].filename[i] == '.') b = i;
					}
					strncat(workDir, "screenshots\\", 14);
					if (a != b)
						strncat(workDir, disk[0].filename + a, b - a);
					else
						strncat(workDir, "no disk", 10);
					strncat(workDir, ".bmp", 5);
					SDL_SaveBMP(sshot, workDir);
					SDL_FreeSurface(sshot);
					}
				break;

				case SDLK_F3:															// PASTE text from clipboard
					if (SDL_HasClipboardText()) {
						char *clipboardText = SDL_GetClipboardText();
						int c = 0;
						while (clipboardText[c]) {											// all chars until ascii NUL
							KBD = clipboardText[c++] | 0x80;								// set bit7
							if (KBD == 0x8A) KBD = 0x8D;									// translate Line Feed to Carriage Ret
							puce6502Exec(400000);											// give cpu (and applesoft) some cycles to process each char
						}
						SDL_free(clipboardText);											// release the ressource
					}
				break;

				case SDLK_F4:															// VOLUME
					if (shift && (volume < 120)) volume++;								// increase volume
					if (ctrl && (volume > 0)) volume--;									// decrease volume
					if (!ctrl && !shift) muted = !muted;								// toggle mute / unmute
					for (int i = 0; i < audioBufferSize; i++) {							// update the audio buffers,
						audioBuffer[true][i] = volume;										// one used when SPKR is true
						audioBuffer[false][i] = -volume;									// the other when SPKR is false
					}
				break;

				case SDLK_F5:															// JOYSTICK Release Speed
					if (shift && (GCReleaseSpeed < 127)) GCReleaseSpeed += 2;			// increase Release Speed
					if (ctrl && (GCReleaseSpeed > 1)) GCReleaseSpeed -= 2;				// decrease Release Speed
					if (!ctrl && !shift) GCReleaseSpeed = 8;							// reset Release Speed to 8
				break;

				case SDLK_F6:															// JOYSTICK Action Speed
					if (shift && (GCActionSpeed < 127)) GCActionSpeed += 2;				// increase Action Speed
					if (ctrl && (GCActionSpeed > 1)) GCActionSpeed -= 2;				// decrease Action Speed
					if (!ctrl && !shift) GCActionSpeed = 8;								// reset Action Speed to 8
				break;

				case SDLK_F7:															// ZOOM
					if (!ctrl && !shift) {
						fullscreen = fullscreen?0:1;
						SDL_SetWindowFullscreen(wdo, fullscreen?SDL_WINDOW_FULLSCREEN_DESKTOP:0);
					}
					if (!fullscreen) {
						if(ctrl && (zoom>1)) {	// zoom out
							zoom--;
							SDL_SetWindowSize(wdo, SCREEN_RES_W * zoom, SCREEN_RES_H * zoom);
						}
						if(shift && (zoom<6)) {	// zoom in
							zoom++;
							SDL_SetWindowSize(wdo, SCREEN_RES_W * zoom, SCREEN_RES_H * zoom);
						}
					}
					break;

				case SDLK_F8: color_mode++; color_mode%=4; break;								// color mode

				case SDLK_F9:															// SAVES
					if(fullscreen) {SDL_SetWindowFullscreen(wdo, 0); fullscreen=0;}
					if (ctrl) {
						if (saveFloppy(0))
							SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Save", "\nDisk 1 saved back to file\n", NULL);
						else
							SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Save", "\nTError while saving Disk 1\n", NULL);
					} else if (alt) {
						if (saveFloppy(1))
							SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Save", "\nDisk 2 saved back to file\n", NULL);
						else
							SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Save", "\nError while saving Disk 2\n", NULL);
					} else {
						SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Save", "CTRL-F9 to save D1\nALT-F9 to save D2\n", NULL);
					}
					{ticks_step=1;last_ticks=SDL_GetTicks64();}
				break;

				case SDLK_F10: debug = debug?0:1;break;

				case SDLK_F11: paused = !paused; if(!paused){ticks_step=1;last_ticks=SDL_GetTicks64();} break;									// toggle pause

				case SDLK_F12: if (ctrl) SysReset(); break;						// simulate a reset

				// EMULATED KEYS :

				case SDLK_a:			KBD = ctrl ? 0x81: 0xC1;   break;				// a
				case SDLK_b:			KBD = ctrl ? 0x82: 0xC2;   break;				// b STX
				case SDLK_c:			KBD = ctrl ? 0x83: 0xC3;   break;				// c ETX
				case SDLK_d:			KBD = ctrl ? 0x84: 0xC4;   break;				// d EOT
				case SDLK_e:			KBD = ctrl ? 0x85: 0xC5;   break;				// e
				case SDLK_f:			KBD = ctrl ? 0x86: 0xC6;   break;				// f ACK
				case SDLK_g:			KBD = ctrl ? 0x87: 0xC7;   break;				// g BELL
				case SDLK_h:			KBD = ctrl ? 0x88: 0xC8;   break;				// h BS
				case SDLK_i:			KBD = ctrl ? 0x89: 0xC9;   break;				// i HTAB
				case SDLK_j:			KBD = ctrl ? 0x8A: 0xCA;   break;				// j LF
				case SDLK_k:			KBD = ctrl ? 0x8B: 0xCB;   break;				// k VTAB
				case SDLK_l:			KBD = ctrl ? 0x8C: 0xCC;   break;				// l FF
				case SDLK_m:			KBD = ctrl ? shift ? 0x9D: 0x8D: 0xCD; break;	// m CR ]
				case SDLK_n:			KBD = ctrl ? shift ? 0x9E: 0x8E: 0xCE; break;	// n ^
				case SDLK_o:			KBD = ctrl ? 0x8F: 0xCF;   break;				// o
				case SDLK_p:			KBD = ctrl ? shift ? 0x80: 0x90: 0xD0; break;	// p @
				case SDLK_q:			KBD = ctrl ? 0x91: 0xD1;   break;				// q
				case SDLK_r:			KBD = ctrl ? 0x92: 0xD2;   break;				// r
				case SDLK_s:			KBD = ctrl ? 0x93: 0xD3;   break;				// s ESC
				case SDLK_t:			KBD = ctrl ? 0x94: 0xD4;   break;				// t
				case SDLK_u:			KBD = ctrl ? 0x95: 0xD5;   break;				// u NAK
				case SDLK_v:			KBD = ctrl ? 0x96: 0xD6;   break;				// v
				case SDLK_w:			KBD = ctrl ? 0x97: 0xD7;   break;				// w
				case SDLK_x:			KBD = ctrl ? 0x98: 0xD8;   break;				// x CANCEL
				case SDLK_y:			KBD = ctrl ? 0x99: 0xD9;   break;				// y
				case SDLK_z:			KBD = ctrl ? 0x9A: 0xDA;   break;				// z
				case SDLK_LEFTBRACKET:	KBD = ctrl ? 0x9B: 0xDB;   break;				// [ {
				case SDLK_BACKSLASH:	KBD = ctrl ? 0x9C: 0xDC;   break;				// \ |
				case SDLK_RIGHTBRACKET: KBD = ctrl ? 0x9D: 0xDD;   break;				// ] }
				case SDLK_BACKSPACE:	KBD = ctrl ? 0xDF: 0x88;   break;				// BS
				case SDLK_0:			KBD = shift? 0xA9: 0xB0;   break;				// 0 )
				case SDLK_1:			KBD = shift? 0xA1: 0xB1;   break;				// 1 !
				case SDLK_2:			KBD = shift? 0xC0: 0xB2;   break;				// 2
				case SDLK_3:			KBD = shift? 0xA3: 0xB3;   break;				// 3 #
				case SDLK_4:			KBD = shift? 0xA4: 0xB4;   break;				// 4 $
				case SDLK_5:			KBD = shift? 0xA5: 0xB5;   break;				// 5 %
				case SDLK_6:			KBD = shift? 0xDE: 0xB6;   break;				// 6 ^
				case SDLK_7:			KBD = shift? 0xA6: 0xB7;   break;				// 7 &
				case SDLK_8:			KBD = shift? 0xAA: 0xB8;   break;				// 8 *
				case SDLK_9:			KBD = shift? 0xA8: 0xB9;   break;				// 9 (
				case SDLK_QUOTE:		KBD = shift? 0xA2: 0xA7;   break;				// ' "
				case SDLK_EQUALS:		KBD = shift? 0xAB: 0xBD;   break;				// = +
				case SDLK_SEMICOLON:	KBD = shift? 0xBA: 0xBB;   break;				// ; :
				case SDLK_COMMA:		KBD = shift? 0xBC: 0xAC;   break;				// , <
				case SDLK_PERIOD:		KBD = shift? 0xBE: 0xAE;   break;				// . >
				case SDLK_SLASH:		KBD = shift? 0xBF: 0xAF;   break;				// / ?
				case SDLK_MINUS:		KBD = shift? 0xDF: 0xAD;   break;				// - _
				case SDLK_BACKQUOTE:	KBD = shift? 0xFE: 0xE0;   break;				// ` ~
				case SDLK_LEFT:			KBD = 0x88;				   break;				// BS
				case SDLK_RIGHT:		KBD = 0x95;				   break;				// NAK
				case SDLK_SPACE:		KBD = 0xA0;				   break;
				case SDLK_ESCAPE:		KBD = 0x9B;				   break;				// ESC
				case SDLK_RETURN:		KBD = 0x8D;				   break;				// CR

				// EMULATED JOYSTICK :

				case SDLK_KP_1:			GCD[0] = -1; GCA[0] = 1;   break;				// pdl0 <-
				case SDLK_KP_3:			GCD[0] = 1;	 GCA[0] = 1;   break;				// pdl0 ->
				case SDLK_KP_5:			GCD[1] = -1; GCA[1] = 1;   break;				// pdl1 <-
				case SDLK_KP_2:			GCD[1] = 1;	 GCA[1] = 1;   break;				// pdl1 ->
				}
			}

			if (event.type == SDL_KEYUP) {
				switch (event.key.keysym.sym) {
				case SDLK_KP_1:			GCD[0] = 1;	 GCA[0] = 0;   break;				// pdl0 ->
				case SDLK_KP_3:			GCD[0] = -1; GCA[0] = 0;   break;				// pdl0 <-
				case SDLK_KP_5:			GCD[1] = 1;	 GCA[1] = 0;   break;				// pdl1 ->
				case SDLK_KP_2:			GCD[1] = -1; GCA[1] = 0;   break;				// pdl1 <-
				}
			}
		}

		for (int pdl = 0; pdl < 2; pdl++) {											// update the two paddles positions
			if (GCA[pdl]) {															// actively pushing the stick
				GCP[pdl] += GCD[pdl] * GCActionSpeed;
				if (GCP[pdl] > 255) GCP[pdl] = 255;
				if (GCP[pdl] < 0)	GCP[pdl] = 0;
			} else {	// the stick is return back to center
				GCP[pdl] += GCD[pdl] * GCReleaseSpeed;
				if (GCD[pdl] == 1  && GCP[pdl] > 127) GCP[pdl] = 127;
				if (GCD[pdl] == -1 && GCP[pdl] < 127) GCP[pdl] = 127;
			}
		}

		current_ticks = SDL_GetTicks64();
		if( current_ticks-last_ticks > ticks_step*50/3 ) {		// ticks_step*1000/60 == ticks_step*50/3
			ticks_step++;
			//if(current_ticks-last_ticks>=1000 && ticks_step>=60) {last_ticks+=1000;ticks_step-=60;}
			break;
		}

		}	// while

		//============================================================= VIDEO OUTPUT

		// HIGH RES GRAPHICS
		if (!TEXT && HIRES) {
			uint16_t word;
			uint8_t bit, colorSet, even;
			uint16_t vRamBase = 0x2000 + PAGE2 * 0x2000;
			uint8_t lastLine = MIXED ? 160 : 192;
			uint16_t byte_1, byte_2;
			//uint8_t bits[16], pbit;
			//uint8_t colorIdx = 0;														// to index the color arrays

			for (int line = 0; line < lastLine; line++) {							// for every line
				int off = line*280;

				word = 0;
				byte_1 = ram[vRamBase + offsetHGR[line] + 0+0];
				byte_2 = 0;
				even = 0;
				for (int col = 0; col < 40; col ++) {								// for every 7 horizontal dots
					if(col==39)
						byte_2 = 0;
					else
						byte_2 = ram[vRamBase + offsetHGR[line] + col+1];

					colorSet = (byte_1&0x80)?16:0;
					word = word | ((byte_1&0x007f)<<1) | ((byte_2&0x0001)<<8);

					for (bit=0; bit<7; bit++) {
						if(color_mode)
							screenData[off] = color_mode*32+16+ ((byte_1>>bit)&1);
						else
							screenData[off] = 32*4+((word>>bit)&7) + even + colorSet;
						even = even?0:8;
						off++;
					}

					word = (byte_1>>6)&1;
					byte_1=byte_2;
				}

/*
				for (int col = 0; col < 40; col += 2) {									// for every 7 horizontal dots
					int x = col * 7;
					even = 0;

					word = (uint16_t)(ram[vRamBase + offsetHGR[line] + col + 1]) << 8;	// store the two next bytes into 'word'
					word +=			  ram[vRamBase + offsetHGR[line] + col];			// in reverse order

					if (HiResCache[line][col] != word || !flashCycle) {					// check if this group of 7 dots need a redraw
						for (bit=0; bit < 16; bit++)										// store all bits 'word' into 'bits'
							bits[bit] = (word >> bit) & 1;
						colorSet = bits[7] * 4;												// select the right color set
						pbit = previousBit[line][col];										// the bit value of the left dot
						bit = 0;															// starting at 1st bit of 1st byte

						while (bit < 15) {													// until we reach bit7 of 2nd byte
							if (bit == 7) {													// moving into the second byte
								colorSet = bits[15] * 4;										// update the color set
								bit++;															// skip bit 7
							}
							if(color_mode)
								colorIdx = color_mode*32+bits[bit];
							else
								colorIdx = even + colorSet + (bits[bit] << 1) + (pbit);
							screenData[off+x] = colorIdx+16; x++;
							pbit = bits[bit++];												// proceed to the next pixel
							even = even ? 0 : 8;											// one pixel every two is darker
						}

						HiResCache[line][col] = word;										// update the video cache
						if ((col < 37) && (previousBit[line][col + 2] != pbit)) {			// check color franging effect on the dot after
							previousBit[line][col + 2] = pbit;								// set pbit and clear the
							HiResCache[line][col + 2] = -1;									// video cache for next dot
						}

					}		// if (HiResCache[line][col] ...
				}
*/

			}

		}

		// lOW RES GRAPHICS
		else if (!TEXT) {															// and not in HIRES
			uint16_t vRamBase = 0x400 + PAGE2 * 0x0400;
			uint8_t lastLine = MIXED ? 20 : 24;
			uint8_t glyph;															// 2 blocks in GR
			uint8_t colorIdx = 0;													// to index the color arrays

			for (int col = 0; col < 40; col++) {									// for each column
				//pixelGR.x = col * 7;
				for (int line = 0; line < lastLine; line++) {							// for each row
					//pixelGR.y = line * 8;													// first block

					glyph = ram[vRamBase + offsetGR[line] + col];						// read video memory
					//if (LoResCache[line][col] != glyph || !flashCycle) {
					//	LoResCache[line][col] = glyph;

						int off;

						colorIdx = glyph & 0x0F;											// first nibble
						off = line*8*280+col*7;
						for(int j=0;j<4;j++) for(int i=0;i<7;i++)
							if(color_mode)
								screenData[off+j*280+i] = colorIdx+color_mode*32;
							else
								screenData[off+j*280+i] = colorIdx;

						//pixelGR.y += 4;														// second block
						colorIdx = (glyph & 0xF0) >> 4;										// second nibble
						off = (line*8+4)*280+col*7;
						for(int j=0;j<4;j++) for(int i=0;i<7;i++)
							if(color_mode)
								screenData[off+j*280+i] = colorIdx+color_mode*32;
							else
								screenData[off+j*280+i] = colorIdx;
					//}
				}
			}
		}

		// TEXT 40 COLUMNS
		if (TEXT || MIXED) {														// not Full Graphics
			uint16_t vRamBase = 0x400 +PAGE2 * 0x0400;
			uint8_t firstLine = TEXT ? 0 : 20;
			uint8_t glyph;															// a TEXT character

			for (int col = 0; col < 40; col++) {									// for each column
				for (int line = firstLine; line < 24; line++) {							// for each row
					glyph = ram[vRamBase + offsetGR[line] + col];						// read video memory
					if (glyph > 0x7F) glyphAttr = A_NORMAL;								// is NORMAL ?
					else if (glyph < 0x40) glyphAttr = A_INVERSE;						// is INVERSE ?
					else glyphAttr = A_FLASH;											// it's FLASH !

					//if (glyphAttr==A_FLASH || TextCache[line][col]!=glyph || !flashCycle){
					//	TextCache[line][col] = glyph;

						glyph &= 0x7F;														// unset bit 7
						if (glyph > 0x5F) glyph &= 0x3F;									// shifts to match
						if (glyph < 0x20) glyph |= 0x40;									// the ASCII codes


						int off = line*8*280+col*7;
						uint8_t colorIdx_0, colorIdx_1;
						uint8_t font_b;
						if (glyphAttr==A_NORMAL || (glyphAttr==A_FLASH && flashCycle<15)) {
							if(color_mode) {
								colorIdx_0=0+color_mode*32; colorIdx_1=15+color_mode*32;
							} else {
								colorIdx_0=0; colorIdx_1=15;
							}
						} else {
							if(color_mode) {
								colorIdx_0=15+color_mode*32; colorIdx_1=0+color_mode*32;
							} else {
								colorIdx_0=15; colorIdx_1=0;
							}
						}

						for(int j=0;j<8;j++) {
							font_b = fontrom[glyph*8+j];
							for(int i=0;i<7;i++) {
								font_b = font_b<<1;
								screenData[off+j*280+i] = (font_b&0x80)?colorIdx_1:colorIdx_0;
							}
						}
					//}
				}
			}
		}


		//========================================================= SDL RENDER FRAME

		if (++flashCycle == 30)														// increase cursor flash cycle
			flashCycle = 0;															// reset to zero every half second

		Uint32 fmt = sdlSurface->format->format;
		SDL_Surface *surf = SDL_ConvertSurfaceFormat(sdlScreen, fmt, 0);
		SDL_BlitScaled(surf,NULL,sdlSurface,NULL);
		SDL_FreeSurface(surf);

		//====================================================== DISPLAY DISK STATUS
		// red for writes
		// green for reads
		if (disk[curDrv].motorOn)												// drive is active
			SDL_FillRect(sdlSurface, &drvRect[curDrv],
				(disk[curDrv].writeMode)?SDL_MapRGBA(sdlSurface->format, 255, 0, 0,85):SDL_MapRGBA(sdlSurface->format, 0, 255, 0,85) );

		SDL_Texture *sdlTex;
		sdlTex = SDL_CreateTextureFromSurface(rdr, sdlSurface);
		SDL_RenderCopy(rdr, sdlTex, 0, 0);
		SDL_DestroyTexture(sdlTex);

		SDL_RenderPresent(rdr);														// swap buffers
	}				// while (running)


	//================================================ RELEASE RESSOURSES AND EXIT

	SDL_FreeSurface(sdlScreen);
	SDL_FreeSurface(sdlSurface);

	SDL_AudioQuit();
	SDL_Quit();
	return 0;
}
