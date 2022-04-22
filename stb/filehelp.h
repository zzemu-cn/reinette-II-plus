#ifndef FILEHELP_H_
#define FILEHELP_H_

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <direct.h>


#define MAXPATH 512

#define win_separator_ch '\\'
#define win_separator_str "\\"
#define unix_separator_ch '/'
#define unix_separator_str "/"


#if defined(WIN32) || defined(_WIN32)
	#define separator_ch '\\'
	#define separator_str "\\"

#elif defined(__unix__) || defined(linux)
	#define separator_ch '/'
	#define separator_str "/"

#endif

static __attribute__((unused))
int is_separator(char ch)
{
	return ch==win_separator_ch || ch==unix_separator_ch;
}


static __attribute__((unused))
int is_special_dir(const char *s)
{
	return s[0]=='.' && (s[1]=='\0' || (s[1]=='.' && s[2]=='\0'));
}


static __attribute__((unused))
int fexist(const char *filename)
{
	FILE *fp;

	fp = fopen(filename, "r");
	if(fp) fclose(fp);

	return (fp!=0);
}

static __attribute__((unused))
int fn_direxist(const char *path)
{
    // 文件夹不存在则创建文件夹
    return (_access(path, 0)!=-1);
}

static __attribute__((unused))
void fn_mkdir(const char *path)
{
    // 文件夹不存在则创建文件夹
    if(_access(path, 0)==-1) {
        _mkdir(path);
    }
}

static __attribute__((unused))
const char* fn_mkdirs(const char *path)
{
	char s[MAXPATH];

	for(int i=0; i<strlen(path); i++) {
		s[i] = path[i];
		if(is_separator(s[i])) {
			s[i+1]= 0;
			//VAL(s);
			fn_mkdir(s);
		}
	}

	return path;
}

#endif	// FILEHELP_H_
