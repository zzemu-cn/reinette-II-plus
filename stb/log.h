#ifndef LOG_H_
#define LOG_H_

#ifdef ENABLE_LOG

#include <stddef.h>
#include <stdio.h>

#define LOG_FILE	"log.txt"
static FILE* log_file;

inline static __attribute__((constructor))
void log_file_init(void)
{
	log_file = stderr;
	log_file = fopen(LOG_FILE, "w");
	if(!log_file) {
		log_file = stderr;
		fprintf(stderr, "Cannot open dbg file '%s'\n", LOG_FILE);
	}
}

#define LOG(...)	do{fprintf(log_file, __VA_ARGS__);fflush(log_file);break;}while(0)

#else

#define LOG(...)	do{}while(0)

#endif


#endif	// LOG_H_
