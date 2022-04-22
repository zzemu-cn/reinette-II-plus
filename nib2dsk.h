#ifndef NIB2DSK_H_
#define NIB2DSK_H_

//
// nib2dsk.c - convert Apple II NIB image file into DSK file
// Copyright (C) 1996, 2017 slotek@nym.hush.com
//
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/********** Symbolic Constants **********/

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

/********** Typedefs **********/

/********** Statics **********/
//static char *ueof = "Unexpected End of File";
static uint8_t addr_prolog[] = { 0xd5, 0xaa, 0x96 };
static uint8_t addr_epilog[] = { 0xde, 0xaa, 0xeb };
static uint8_t data_prolog[] = { 0xd5, 0xaa, 0xad };
static uint8_t data_epilog[] = { 0xde, 0xaa, 0xeb };
static int soft_interleave[ SECTORS_PER_TRACK ] =
    { 0, 7, 0xE, 6, 0xD, 5, 0xC, 4, 0xB, 3, 0xA, 2, 9, 1, 8, 0xF };

//
// do "6 and 2" un-translation
//
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

/********** Globals **********/
//int infd, outfd;
//uint8_t sector, track, volume;
//uint8_t primary_buf[ PRIMARY_BUF_LEN ];
//uint8_t secondary_buf[ SECONDARY_BUF_LEN ];
//uint8_t *dsk_buf[ TRACKS_PER_DISK ];

/********** Prototypes **********/
int nib2dsk( uint8_t *dsk_buf, uint8_t *nib_buf, int max_tracks );
int process_data( uint8_t byte, uint8_t *dsk_buf, uint8_t *nib_buf, int max_tracks, uint8_t track, uint8_t sector, int index );
uint8_t odd_even_decode( uint8_t byte1, uint8_t byte2 );
uint8_t untranslate( uint8_t x );
int get_nib_byte( uint8_t *byte, uint8_t *buf, int max_tracks, int index );


//
// Convert NIB image into DSK image
//
#define STATE_INIT  0
#define STATE_DONE  666
int nib2dsk( uint8_t *dsk_buf, uint8_t *nib_buf, int max_tracks )
{
    int state;
    int addr_prolog_index, addr_epilog_index;
    int data_prolog_index, data_epilog_index;
    uint8_t byte;
	uint8_t track, sector, volume;
	int index = 0;

    //
    // Image conversion FSM
    //
    if ( get_nib_byte( &byte, nib_buf, max_tracks, index++ ) ) return 0;

    for ( state = STATE_INIT; state != STATE_DONE; ) {

        switch( state ) {

            //
            // Scan for 1st addr prolog byte (skip gap bytes)
            //
            case 0:
                addr_prolog_index = 0;
                if ( byte == addr_prolog[ addr_prolog_index ] ) {
                    ++addr_prolog_index;
                    ++state;
                }
                if ( get_nib_byte( &byte, nib_buf, max_tracks, index++ ) )
                    state = STATE_DONE;
                break;

            //
            // Accept 2nd and 3rd addr prolog bytes
            //
            case 1:
            case 2:
                if ( byte == addr_prolog[ addr_prolog_index ] ) {
                    ++addr_prolog_index;
                    ++state;
                    if ( get_nib_byte( &byte, nib_buf, max_tracks, index++ ) ) return 0;
                } else
                    state = 0;
                break;

            //
            // Read and decode volume number
            //
            case 3:
            {
                uint8_t byte2;
                if ( get_nib_byte( &byte2, nib_buf, max_tracks, index++ ) ) return 0;
                volume = odd_even_decode( byte, byte2 );
                //myprintf( "V:%02x ", volume );
                //myprintf( "{%02x%02x} ", byte, byte2 );
                ++state;
                if ( get_nib_byte( &byte, nib_buf, max_tracks, index++ ) ) return 0;
                break;
            }

            //
            // Read and decode track number
            //
            case 4:
            {
                uint8_t byte2;
                if ( get_nib_byte( &byte2, nib_buf, max_tracks, index++ ) ) return 0;
                track = odd_even_decode( byte, byte2 );
                //myprintf( "T:%02x ", track );
                //myprintf( "{%02x%02x} ", byte, byte2 );
                ++state;
                if ( get_nib_byte( &byte, nib_buf, max_tracks, index++ ) ) return 0;
                break;
            }

            //
            // Read and decode sector number
            //
            case 5:
            {
                uint8_t byte2;
                if ( get_nib_byte( &byte2, nib_buf, max_tracks, index++ ) ) return 0;
                sector = odd_even_decode( byte, byte2 );
                //myprintf( "S:%02x ", sector );
                //myprintf( "{%02x%02x} ", byte, byte2 );
                ++state;
                if ( get_nib_byte( &byte, nib_buf, max_tracks, index++ ) ) return 0;
                break;
            }

            //
            // Read and decode addr field checksum
            //
            case 6:
            {
                uint8_t byte2, csum;
                if ( get_nib_byte( &byte2, nib_buf, max_tracks, index++ ) ) return 0;
                csum = odd_even_decode( byte, byte2 );
                //myprintf( "C:%02x ", csum );
                //myprintf( "{%02x%02x} - ", byte, byte2 );
                ++state;
                if ( get_nib_byte( &byte, nib_buf, max_tracks, index++ ) ) return 0;
                break;
            }

            //
            // Accept 1st addr epilog byte
            //
            case 7:
                addr_epilog_index = 0;
                if ( byte == addr_epilog[ addr_epilog_index ] ) {
                    ++addr_epilog_index;
                    ++state;
                    if ( get_nib_byte( &byte, nib_buf, max_tracks, index++ ) ) return 0;
                } else {
                    //myprintf( "Reset!\n" );
                    state = 0;
                }
                break;

            //
            // Accept 2nd addr epilog byte
            //
            case 8:
                if ( byte == addr_epilog[ addr_epilog_index ] ) {
                    ++state;
                    if ( get_nib_byte( &byte, nib_buf, max_tracks, index++ ) ) return 0;
                } else {
                    //myprintf( "Reset!\n" );
                    state = 0;
                }
                break;

            //
            // Scan for 1st data prolog byte (skip gap bytes)
            //
            case 9:
                data_prolog_index = 0;
                if ( byte == data_prolog[ data_prolog_index ] ) {
                    ++data_prolog_index;
                    ++state;
                }
                if ( get_nib_byte( &byte, nib_buf, max_tracks, index++ ) ) return 0;
                break;

            //
            // Accept 2nd and 3rd data prolog bytes
            //
            case 10:
            case 11:
                if ( byte == data_prolog[ data_prolog_index ] ) {
                    ++data_prolog_index;
                    ++state;
                    if ( get_nib_byte( &byte, nib_buf, max_tracks, index++ ) ) return 0;
                } else
                    state = 9;
                break;

            //
            // Process data
            //
            case 12:
				index = process_data( byte, dsk_buf, nib_buf, max_tracks, track, sector, index ); if( index==0 ) return 0;
                //myprintf( "OK!\n" );
                ++state;
                if ( get_nib_byte( &byte, nib_buf, max_tracks, index++ ) ) return 0;
                break;

            //
            // Scan(!) for 1st data epilog byte
            //
            case 13:
            {
                static int extra = 0;
                data_epilog_index = 0;
                if ( byte == data_epilog[ data_epilog_index ] ) {
                    if ( extra ) {
                        //printf( "Warning: %d extra bytes before data epilog\n", extra );
                        extra = 0;
                    }
                    ++data_epilog_index;
                    ++state;
                } else
                    ++extra;
                if ( get_nib_byte( &byte, nib_buf, max_tracks, index++ ) ) return 0;
                break;
            }

            //
            // Accept 2nd data epilog byte
            //
            case 14:
                if ( byte == data_epilog[ data_epilog_index ] ) {
                    ++data_epilog_index;
                    ++state;
                    if ( get_nib_byte( &byte, nib_buf, max_tracks, index++ ) ) return 0;
                } else {
                    //fatal( "data epilog mismatch (%02x)\n", byte );
					return 0;
				}
                break;

            //
            // Accept 3rd data epilog byte
            //
            case 15:
                if ( byte == data_epilog[ data_epilog_index ] ) {
                    if ( get_nib_byte( &byte, nib_buf, max_tracks, index++ ) )
                        state = STATE_DONE;
                    else
                        state = 0;
                } else {
                    //fatal( "data epilog mismatch (%02x)\n", byte );
					return 0;
				}
                break;

            default:
				{
					//fatal( "Undefined state!" );
					return 0;
				}
                break;
        }
    }

	return 1;
}

//
// Convert 343 6+2 encoded bytes into 256 data bytes and 1 checksum
//
int process_data( uint8_t byte, uint8_t *dsk_buf, uint8_t *nib_buf, int max_tracks, uint8_t track, uint8_t sector, int index )
{
	uint8_t primary_buf[ PRIMARY_BUF_LEN ];
	uint8_t secondary_buf[ SECONDARY_BUF_LEN ];

    int i, sec;
    uint8_t checksum, ch;
    uint8_t bit0, bit1;

    //
    // Fill primary and secondary buffers according to iterative formula:
    //    buf[0] = trans(byte[0])
    //    buf[1] = trans(byte[1]) ^ buf[0]
    //    buf[n] = trans(byte[n]) ^ buf[n-1]
    //
    checksum = ch = untranslate( byte ); if(ch==0xFF) return 0;
    secondary_buf[ 0 ] = checksum;

    for ( i = 1; i < SECONDARY_BUF_LEN; i++ ) {
        if ( get_nib_byte( &byte, nib_buf, max_tracks, index++ ) ) {
            //fatal( ueof );
			return 0;
		}
		ch = untranslate( byte ); if(ch==0xFF) return 0;
        checksum ^= ch;
        secondary_buf[ i ] = checksum;
    }

    for ( i = 0; i < PRIMARY_BUF_LEN; i++ ) {
        if ( get_nib_byte( &byte, nib_buf, max_tracks, index++ ) ) {
            //fatal( ueof );
			return 0;
		}
		ch = untranslate( byte ); if(ch==0xFF) return 0;
        checksum ^= ch;
        primary_buf[ i ] = checksum;
    }

    //
    // Validate resultant checksum
    //
    if ( get_nib_byte( &byte, nib_buf, max_tracks, index++ ) ) {
        //fatal( ueof );
		return 0;
	}
	ch = untranslate( byte ); if(ch==0xFF) return 0;
    checksum ^= ch;
    if ( checksum != 0 ) {
        //printf( "Warning: data checksum mismatch\n" );
		return 0;
	}

    //
    // Denibbilize
    //
    for ( i = 0; i < PRIMARY_BUF_LEN; i++ ) {
        int idx = i % SECONDARY_BUF_LEN;

        switch( i / SECONDARY_BUF_LEN ) {
            case 0:
                bit0 = ( secondary_buf[ idx ] & 2 ) > 0;
                bit1 = ( secondary_buf[ idx ] & 1 ) > 0;
                break;
            case 1:
                bit0 = ( secondary_buf[ idx ] & 8 ) > 0;
                bit1 = ( secondary_buf[ idx ] & 4 ) > 0;
                break;
            case 2:
                bit0 = ( secondary_buf[ idx ] & 0x20 ) > 0;
                bit1 = ( secondary_buf[ idx ] & 0x10 ) > 0;
                break;
            default:
				{
					//fatal( "huh?" );
					return 0;
				}
                break;
        }

        sec = soft_interleave[ sector ];

        dsk_buf[ track*BYTES_PER_TRACK + (sec*BYTES_PER_SECTOR) + i ]
            = ( primary_buf[ i ] << 2 ) | ( bit1 << 1 ) | bit0;
    }

	return index;
}

//
// decode 2 "4 and 4" bytes into 1 byte
//
uint8_t odd_even_decode( uint8_t byte1, uint8_t byte2 )
{
    uint8_t byte;

    byte = ( byte1 << 1 ) & 0xaa;
    byte |= byte2 & 0x55;

    return byte;
}

//
// do "6 and 2" un-translation
//
uint8_t untranslate( uint8_t x )
{
    uint8_t idx=0xFF;

	for(int i=0;i<TABLE62_SIZE;i++)
		if(table62[i]==x) {idx=i; break;}

    return idx;
}

//
// Read byte from input file
// Returns 0 on EOF
//
//HACK #define BUFLEN 16384
#define BUFLEN 232960
// BYTES_PER_NIB_TRACK*35
//static uint8_t buf[ BUFLEN ];
int get_nib_byte( uint8_t *byte, uint8_t *buf, int max_tracks, int index )
{
	int buflen=max_tracks*BYTES_PER_NIB_TRACK;
    if ( index >= buflen ) {
		return 1;
    }

    *byte = buf[ index ];
    return 0;
}


#endif	// NIB2DSK_H_
