
#include "../gjLib/gj_lib.h"
#include "../gjLib/gj_linux.cc"

#include <stdlib.h>
#include <stdio.h>
// #include <stdlib.h>
#include <string.h>

#include <netdb.h>
// #include <sys/types.h>
// #include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>

#include <dlfcn.h>
#include <curl/curl.h>

#define SECS(num) (num)
#define MINS(num) (num*60)
#define HOURS(num) (num*MINUTES(60))

void exitWithError (const char *message) {
	printf("%s, exiting... \n", message);
	exit(0);
}

/*void _logMessage (const char *str) {
	printf(str);
}
#define logMessage(str, ...) { char buffer[1024]; sprintf(buffer, str"\n", __VA_ARGS__); _logMessage(buffer); }*/

#define printf(...)\
	printf(__VA_ARGS__);\
	// write to file

#define curl_version_info_proc(name) curl_version_info_data *name(CURLversion type)
curl_version_info_proc((*curlVersionInfo));

#define curl_global_init_proc(name) CURLcode name(long flags)
curl_global_init_proc((*curlGlobalInit));

#define curl_easy_init_proc(name) CURL *name( )
curl_easy_init_proc((*curlEasyInit));

#define curl_easy_setopt_proc(name) CURLcode name(CURL *handle, CURLoption option, const void *parameter)
curl_easy_setopt_proc((*curlEasySetopt));

#define curl_easy_perform_proc(name) CURLcode name(CURL *easy_handle)
curl_easy_perform_proc((*curlEasyPerform));

#define curl_easy_cleanup_proc(name) void name(CURL *handle)
curl_easy_cleanup_proc((*curlEasyCleanup));

#define curl_global_cleanup_proc(name) void name(void)
curl_global_cleanup_proc((*curlGlobalCleanup));

void loadCurlCode () {
	void *curlLib = dlopen("libcurl.so", RTLD_LAZY);
	if (!curlLib) {
		exitWithError("Failed to load libcurl.so");
	}

	#define loadCurlProc(dest, source) dest = (source##_proc((*)))dlsym(curlLib, #source); if (!dest) { exitWithError("Failed to load "#source); }
	loadCurlProc(curlVersionInfo, curl_version_info);
	loadCurlProc(curlGlobalInit, curl_global_init);
	loadCurlProc(curlEasyInit, curl_easy_init);
	loadCurlProc(curlEasySetopt, curl_easy_setopt);
	loadCurlProc(curlEasyPerform, curl_easy_perform);
	loadCurlProc(curlEasyCleanup, curl_easy_cleanup);
	loadCurlProc(curlGlobalCleanup, curl_global_cleanup);
}

// https://hooks.slack.com
#define slackWebHookUrl "https://hooks.slack.com/services/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"
#define slackWebHookHost "https://hooks.slack.com"

#define testMessage "JellyBot is born."

#include "message_data.h"

size_t curlWriteCallback (char *ptr, size_t size, size_t nmemb, void *userdata) {
	// logMessage("curl: %s", ptr);
	return size * nmemb;
}

CURL *globalCurl = NULL;
void initCurl () {
	curl_version_info_data *versionInfo = curlVersionInfo(CURLVERSION_NOW);

	curlGlobalInit(CURL_GLOBAL_DEFAULT);
	globalCurl = curlEasyInit();

	if (!globalCurl) {
		exitWithError("Curl init failed");
	}

	curlEasySetopt(globalCurl, CURLOPT_WRITEFUNCTION, (void*)curlWriteCallback);
}

void stopCurl () {
	if (globalCurl) {
		curlEasyCleanup(globalCurl);
		curlGlobalCleanup();
	}
}

void sendSlackMessage (const char *message) {
	printf("Message: %s \n", message);
	if (globalCurl) {
		curlEasySetopt(globalCurl, CURLOPT_URL, slackWebHookUrl);
		char str[1024];
		sprintf(str, "payload={\"text\":\"%s\"}", message);
		curlEasySetopt(globalCurl, CURLOPT_POSTFIELDS, str);
		CURLcode result = curlEasyPerform(globalCurl);
		if (result != CURLE_OK) {
			printf("curl_easy_perform error \n");
		}	
	}
}

int getLocalMin () {
	time_t rawTime;
	time(&rawTime);
	tm *localTime = localtime(&rawTime);
	return localTime->tm_min;
}

int getLocalHour () {
	time_t rawTime;
	time(&rawTime);
	tm *localTime = localtime(&rawTime);
	return localTime->tm_hour;
}

struct Message {
	char *str;
	int usedCount;
};

gjMemStack messageStack = gjInitMemStack(megabytes(1));
Message *messageTable;
int messageTableSize = 0;

void loadMessageData () {
	gjMemStack memStack = gjInitMemStack(megabytes(1));
	gjData messageData = gjReadFile("messages.txt", &memStack);

	messageTable = (Message*)messageStack.mem;

	char *start = messageData.mem;
	char *ptr = start;
	char *tokenStart = ptr;
	while (ptr < start + messageData.size) {
		while (*ptr == ' ' || *ptr == '\t' || *ptr == '\n') {
			++ptr;
		}

		tokenStart = ptr;

		if (*ptr == '/') {
			while (*ptr != '\n' && ptr < start + messageData.size) {
				++ptr;
			}
			++ptr;
			tokenStart = ptr;
		} else if (ptr < start + messageData.size) {
			++ptr;
			while (*ptr != '\n' && ptr < start + messageData.size) {
				++ptr;
			}

			*ptr = 0;
			// printf("line: %s \n", tokenStart);
			++messageTableSize;
			Message *msg = (Message*)gjPushMemStack(&messageStack, sizeof(Message), true);
			msg->str = tokenStart;

			++ptr;
			tokenStart = ptr;
		}
	}

#if 0
	printf("messages {\n");
	fiz (messageTableSize) {
		printf("\t%s \n", messageTable[i].str);
	}
	printf("}\n");
#endif
}

int main () {
	loadMessageData();

	printf("Message count %i \n", messageTableSize);

	loadCurlCode();
	initCurl();

	timespec startTime;
	clock_gettime(CLOCK_REALTIME, &startTime);
	srand(startTime.tv_nsec);

	int hourOfLastMessage = getLocalHour();

	// int interval = HOURS(1);
	int interval = SECS(10);

	int lastMessageIndex = 0;

	while (true) {
		time_t rawTime;
		time(&rawTime);
		tm *localTime = localtime(&rawTime);
#if 0
		logMessage("hour %i, minute %i, second %i", localTime->tm_hour, localTime->tm_min, localTime->tm_sec);
#endif

		// timespec currentTime;
		// clock_gettime(CLOCK_REALTIME, &currentTime);

		int hour = getLocalHour();
		int minute = getLocalMin();

		if (hour != hourOfLastMessage) {
			hourOfLastMessage = hour;

			// Only run if it's not night time, last is at midnight, first is at 9am
			if (hour >= 9 || hour == 0) {
				bool allSame = true;
				fiz (messageTableSize) {
					if (messageTable[i].usedCount != messageTable[lastMessageIndex].usedCount) {
						allSame = false;
						break;
					}
				}

				if (allSame) {
					printf("All the usedCounts are the same \n");
				}

				int index = rand() % messageTableSize;
				printf("%i ", index);
				if (messageTable[index].usedCount > 0 && !allSame) {
					while (messageTable[index].usedCount >= messageTable[lastMessageIndex].usedCount) {
						index = rand() % messageTableSize;
						printf("searching %i->%i  %i->%i \n", index, messageTable[index].usedCount, lastMessageIndex, messageTable[lastMessageIndex].usedCount);
					}
				}

				printf("%i ", index);
				sendSlackMessage(messageTable[index].str);
				++messageTable[index].usedCount;
				lastMessageIndex = index;
			} else {
				printf("It's in-between mid-night and 9am so I'll keep sleeping... \n");
			}
		} else {
			printf("Not time to send message yet, %i minutes left, going back to sleep... \n", (60 - minute));
		}

		sleep(MINS(1));
	}

	stopCurl();
}
