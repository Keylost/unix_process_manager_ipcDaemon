#include "utilis.h"

/*
 * Возвращает дату и время в формате DD.MM.YYYY/HH:MM:SS
 */
char *get_datetime()
{
	char *datetime = (char*)malloc(30);
	time_t rawtime;
	struct tm *timeinfo;
	time (&rawtime);
	timeinfo = localtime(&rawtime);
	sprintf (datetime,"%d.%d.%d/%d:%d:%d",timeinfo->tm_mday,timeinfo->tm_mon+1, timeinfo->tm_year+1900,
												timeinfo->tm_hour,timeinfo->tm_min, timeinfo->tm_sec);
	return datetime;
}

/* распарсить строку на массив аргументов */
char **string_to_argv(char *string, int *argc)
{
	int exec_len = strlen(string);
	int begin =0;
	int end =0;
	int argc_local = 0;
	char **argv_local;
	
	while(begin<exec_len)
	{
		while(begin<exec_len && is_delimetr(string[begin])) begin++;
		end = begin;
		while(end<exec_len && !is_delimetr(string[end])) end++;
		begin = end;
		argc_local++;
	}
	
	argv_local = (char**)malloc(argc_local+1);
	int i = 0;
	end = 0;
	begin = 0;
	
	while(begin<exec_len)
	{
		while(begin<exec_len && is_delimetr(string[begin])) begin++;
		end = begin;
		while(end<exec_len && !is_delimetr(string[end])) end++;
		
		int arr_size = end-begin;
		argv_local[i] = malloc(arr_size+2);
		memcpy(argv_local[i],string+begin,arr_size);
		argv_local[i][arr_size] = '\0';
		
		begin = end;
		i++;
	}
	argv_local[argc_local] = NULL;
	
	*argc = argc_local;
	return argv_local;
}

/* является ли символ разделителем */
const char delims[] = {'\t','\n',' '};
int delims_count = sizeof(delims)/sizeof(delims[0]);
int is_delimetr(char symbol) //return 1 if symbol is delimeter or 0
{
	for(int i=0;i<delims_count;i++)
	{
		if(delims[i]==symbol) return 1;
	}
	return 0;
}

/* добавляет флаг к дескриптору */
void add_flags(int fd, int flags)
{
	int old_flags = fcntl(fd, F_GETFL, 0);
	fcntl(fd,F_SETFL,old_flags|flags);
	return;
}
