#ifndef DSK2NIB_H_
#define DSK2NIB_H_

//https://github.com/slotek/dsk2nib
//
// dsk2nib.c - convert Apple II DSK image file format into NIB file
// Copyright (C) 1996, 2017 slotek@nym.hush.com
//
#include <stdint.h>
#include <string.h>

/********** symbolic constants **********/

#ifndef DISK_DEF_
#define DISK_DEF_

#define MAX_TRACKS_PER_DISK     40
//#define TRACKS_PER_DISK     35
#define SECTORS_PER_TRACK   16
#define BYTES_PER_SECTOR    256
#define BYTES_PER_TRACK     4096
#define DSK_LEN             143360L

#define PRIMARY_BUF_LEN     256
#define SECONDARY_BUF_LEN   86
#define DATA_LEN            (PRIMARY_BUF_LEN+SECONDARY_BUF_LEN)

#define BYTES_PER_NIB_SECTOR 416
#define BYTES_PER_NIB_TRACK  6656

/********** statics **********/
static uint8_t addr_prolog[] = { 0xd5, 0xaa, 0x96 };
static uint8_t addr_epilog[] = { 0xde, 0xaa, 0xeb };
static uint8_t data_prolog[] = { 0xd5, 0xaa, 0xad };
static uint8_t data_epilog[] = { 0xde, 0xaa, 0xeb };
static int soft_interleave[ SECTORS_PER_TRACK ] =
    { 0, 7, 0xE, 6, 0xD, 5, 0xC, 4, 0xB, 3, 0xA, 2, 9, 1, 8, 0xF };

// Do "6 and 2" translation
#define TABLE62_SIZE 0x40
static uint8_t table62[ TABLE62_SIZE ] = {
    0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6,
    0xa7, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb2, 0xb3,
    0xb4, 0xb5, 0xb6, 0xb7, 0xb9, 0xba, 0xbb, 0xbc,
    0xbd, 0xbe, 0xbf, 0xcb, 0xcd, 0xce, 0xcf, 0xd3,
    0xd6, 0xd7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde,
    0xdf, 0xe5, 0xe6, 0xe7, 0xe9, 0xea, 0xeb, 0xec,
    0xed, 0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6,
    0xf7, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
};

#endif	// DISK_DEF_

#define PROLOG_LEN          3
#define EPILOG_LEN          3
#define GAP1_LEN            48
#define GAP2_LEN            5

#define DEFAULT_VOLUME      254
#define GAP_BYTE            0xff

/********** typedefs **********/
typedef struct {
    uint8_t prolog[ PROLOG_LEN ];
    uint8_t volume[ 2 ];
    uint8_t track[ 2 ];
    uint8_t sector[ 2 ];
    uint8_t checksum[ 2 ];
    uint8_t epilog[ EPILOG_LEN ];
} addr_t;

typedef struct {
    uint8_t prolog[ PROLOG_LEN ];
    uint8_t data[ DATA_LEN ];
    uint8_t data_checksum;
    uint8_t epilog[ EPILOG_LEN ];
} data_t;

typedef struct {
    uint8_t gap1[ GAP1_LEN ];
    addr_t addr;
    uint8_t gap2[ GAP2_LEN ];
    data_t data;
} nib_sector_t;
    
/********** statics **********/
static int phys_interleave[ SECTORS_PER_TRACK ] =
    { 0, 0xD, 0xB, 9, 7, 5, 3, 1, 0xE, 0xC, 0xA, 8, 6, 4, 2, 0xF };

//
// Do "6 and 2" translation
//
static uint8_t translate( uint8_t byte )
{
    return table62[ byte & 0x3f ];
}

//
// Encode 1 byte into two "4 and 4" bytes
//
static void odd_even_encode( uint8_t a[], int i )
{
    a[ 0 ] = ( i >> 1 ) & 0x55;
    a[ 0 ] |= 0xaa;

    a[ 1 ] = i & 0x55;
    a[ 1 ] |= 0xaa;
}

//
// Return pointer to NIB image buffer
//
static uint8_t *nib_get( uint8_t *nib_buf, int track, int sector )
{
    return nib_buf + track*BYTES_PER_NIB_TRACK + sector*BYTES_PER_NIB_SECTOR;
}

//
// Return pointer to DSK image buffer
//
static uint8_t *dsk_get( uint8_t *dsk_buf, int track, int sector )
{
    return dsk_buf + track*BYTES_PER_TRACK + sector*BYTES_PER_SECTOR;
}

//
// Convert 256 data bytes into 342 6+2 encoded bytes and a checksum
//
static void nibbilize( uint8_t *dsk_buf, int track, int sector, nib_sector_t *nib_sector)
{
	uint8_t primary_buf[ PRIMARY_BUF_LEN ];
	uint8_t secondary_buf[ SECONDARY_BUF_LEN ];

    int i, index, section;
    uint8_t pair;
    uint8_t *src = dsk_get( dsk_buf, track, sector );
    uint8_t *dest = nib_sector->data.data;

    //
    // Nibbilize data into primary and secondary buffers
    //
    memset( primary_buf, 0, PRIMARY_BUF_LEN );
    memset( secondary_buf, 0, SECONDARY_BUF_LEN );

    for ( i = 0; i < PRIMARY_BUF_LEN; i++ ) {
        primary_buf[ i ] = src[ i ] >> 2;

        index = i % SECONDARY_BUF_LEN;
        section = i / SECONDARY_BUF_LEN;
        pair = ((src[i]&2)>>1) | ((src[i]&1)<<1);       // swap the low bits
        secondary_buf[ index ] |= pair << (section*2);
    }

    //
    // Xor pairs of nibbilized bytes in correct order
    //
    index = 0;
    dest[ index++ ] = translate( secondary_buf[ 0 ] );

    for ( i = 1; i < SECONDARY_BUF_LEN; i++ )
        dest[index++] = translate( secondary_buf[i] ^ secondary_buf[i-1] );

    dest[index++] =
        translate( primary_buf[0] ^ secondary_buf[SECONDARY_BUF_LEN-1] );

    for ( i = 1; i < PRIMARY_BUF_LEN; i++ )
        dest[index++] = translate( primary_buf[i] ^ primary_buf[i-1] );

    nib_sector->data.data_checksum = translate( primary_buf[PRIMARY_BUF_LEN-1] );
}


//static uint8_t dsk_buf[ MAX_TRACKS_PER_DISK*BYTES_PER_TRACK ];
//static uint8_t nib_buf[ MAX_TRACKS_PER_DISK*BYTES_PER_NIB_TRACK ];

static void dsk2nib( int tracks, int volume, uint8_t *dsk_buf, uint8_t *nib_buf )
{
	nib_sector_t nib_sector;
	int sec, trk, csum;
	uint8_t *buf;
	//int volume = DEFAULT_VOLUME;

    //
    // Init addr & data field marks & volume number
    //
    memcpy( nib_sector.addr.prolog, addr_prolog, 3 );
    memcpy( nib_sector.addr.epilog, addr_epilog, 3 );
    memcpy( nib_sector.data.prolog, data_prolog, 3 );
    memcpy( nib_sector.data.epilog, data_epilog, 3 );
    odd_even_encode( nib_sector.addr.volume, volume );

    //
    // Init gap fields
    //
    memset( nib_sector.gap1, GAP_BYTE, GAP1_LEN );
    memset( nib_sector.gap2, GAP_BYTE, GAP2_LEN );

    //
    // Loop thru DSK tracks
    //
    for ( trk = 0; trk < tracks; trk++ ) {
        //
        // Loop thru DSK sectors
        //
        for ( sec = 0; sec < SECTORS_PER_TRACK; sec++ ) {
            int softsec = soft_interleave[ sec ];
            int physsec = phys_interleave[ sec ];

            //
            // Set ADDR field contents
            //
            csum = volume ^ trk ^ sec;
            odd_even_encode( nib_sector.addr.track, trk );
            odd_even_encode( nib_sector.addr.sector, sec );
            odd_even_encode( nib_sector.addr.checksum, csum );

            //
            // Set DATA field contents (encode sector data)
            //
            nibbilize( dsk_buf, trk, softsec, &nib_sector );

            //
            // Copy to NIB image buffer
            //
            buf = nib_get( nib_buf, trk, physsec );
            memcpy( buf, &nib_sector, sizeof( nib_sector ) );
        }
    }
}


#endif	// DSK2NIB_H_
