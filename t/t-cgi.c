#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

int main ()
{
	time_t current_time;
	size_t i;
	static const char* var[] =
	{
		"REQUEST_METHOD",
		"REQUEST_URI",
		"QUERY_STRING",
		"REMOTE_ADDR",
		"REMOTE_PORT",
		"CONTENT_LENGTH"
	};

	printf("Content-type: text/plain\r\n\r\n");
	printf("C Program Version\n");
	current_time = time(NULL);
	printf("It is now %s\n", ctime(&current_time));

	for (i = 0; i < sizeof(var) / sizeof(var[0]); i++)
	{
		printf ("%s:%s\n", var[i], getenv(var[i]));
	}
	
	fflush(stdout);
	exit (0);
}
