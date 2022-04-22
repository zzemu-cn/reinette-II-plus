#ifndef STB_FILE_H_
#define STB_FILE_H_


#include <unistd.h>
#include <stdlib.h>

// get file size
static __attribute__((unused))
size_t fn_filesize(const char *filename)
{
	FILE *fp;
	size_t sz = 0;

	fp = fopen(filename, "rb");
	if(!fp) {
		//MSG("open file failed");
		exit(EXIT_FAILURE);
	} else {
		fseek(fp, 0, SEEK_END);
		sz = ftell(fp);
		fclose(fp);
	}

	return sz;
}

static __attribute__((unused))
size_t fp_filesize(FILE *fp)
{
	size_t	curpos, pos;

	if( (curpos = ftello(fp))==-1LL )
		return -1LL;

	if( fseeko( fp, 0LL, SEEK_END ) )
		return -1LL;

	if( (pos = ftello(fp))==-1LL )
		return -1LL;

	if( fseeko( fp, curpos, SEEK_SET ) )
		return -1LL;

	return pos;
}

static __attribute__((unused))
size_t fread_buf_bin(const char *filename, void *buf, size_t len, size_t *r_len)
{
	FILE *fp;
	size_t sz = 0;
	size_t r = 0;

	*r_len = 0;
	fp = fopen(filename, "rb");
	if(!fp) {
		//MSG("open file failed");
		//exit(EXIT_FAILURE);
		goto cleanup;
	}

	fseek(fp, 0, SEEK_END);
	sz = ftell(fp);
	if(sz==0||sz>len) {
		goto cleanup;
	}
	fseek(fp, 0, SEEK_SET);

    if( fread(buf, sz, 1, fp)!=1 ) {
		goto cleanup;
	}

	r = sz;
	*r_len = sz;
cleanup:
	if(fp)	fclose(fp);
	return r;
}

static __attribute__((unused))
size_t fwrite_buf_bin(const char *filename, void *buf, size_t len)
{
	FILE *fp;
	size_t r = 0;

	fp = fopen(filename, "wb");
	if(!fp) {
		//MSG("open file failed");
		//exit(EXIT_FAILURE);
		goto cleanup;
	}

	if(len==0) goto cleanup;
	if(fwrite(buf, len, 1, fp)!=1) goto cleanup;
	r = len;
cleanup:
	if(fp)	fclose(fp);
	return r;
}


/* 实测，win64 mingw环境下，fread 一次读取不能超过 2G (包括2G) */
/* 每次读取 1G */
#define FREAD_BLOCK_SZ 0x40000000LL

size_t fn_readfile(const char *fn, uint8_t *buf)
{
	FILE	*fp;
	size_t	sz;
	size_t	read_c=0LL;
	size_t	read_sz;

	if( ( fp = fopen(fn, "rb") ) == NULL ) return 0;

	sz = fp_filesize(fp);
	if( sz<=0 ) { fclose(fp); return 0; }

	while(read_c<sz) {
		read_sz = (read_c+FREAD_BLOCK_SZ<=sz) ? FREAD_BLOCK_SZ : sz-read_c;
		if( fread( (char*)buf+read_c, read_sz, 1, fp ) != 1LL ) { fclose(fp); return 0; }
		read_c+=read_sz;
	}

	fclose(fp);

	return read_c;
}

#undef FREAD_BLOCK_SZ


#endif	// STB_FILE_H_
