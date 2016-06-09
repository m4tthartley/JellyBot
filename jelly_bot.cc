
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

#define assert(expression) if (!expression) { *((int*)0) = 0; }
#define arraySize(array) (sizeof(array)/sizeof(array[0]))

#define MINUTES(num) (num*60)
#define HOURS(num) (num*MINUTES(60))

void exitWithError (const char *message) {
	printf("%s, exiting... \n", message);
	exit(0);
}

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
	// printf("curl: %s \n", ptr);
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

int main () {
	printf("Message count %i \n", arraySize(slackMessageTable));

	loadCurlCode();
	initCurl();

	timespec lastTime;
	clock_gettime(CLOCK_REALTIME, &lastTime);
	srand(lastTime.tv_nsec);

	int interval = HOURS(1);

	while (true) {
		time_t rawTime;
		time(&rawTime);
		tm *localTime = localtime(&rawTime);
#if 0
		printf("hour %i, minute %i, second %i \n", localTime->tm_hour, localTime->tm_min, localTime->tm_sec);
#endif

		timespec currentTime;
		clock_gettime(CLOCK_REALTIME, &currentTime);

		if (currentTime.tv_sec - lastTime.tv_sec >= interval) {
			lastTime = currentTime;

			// Only run if it's not night time
			if (localTime->tm_hour >= 9) {
				printf("Sending slack message... \n");
				sendSlackMessage(slackMessageTable[rand() % arraySize(slackMessageTable)]);
			} else {
				printf("It's in-between mid-night and 9am so I'll keep sleeping... \n");
			}
		}

		sleep(MINUTES(1));
	}

	stopCurl();
}

// Doesn't work for slack cause you need ssl
void bsdSocketCode () {
	addrinfo addressHints = {};
	addressHints.ai_family = AF_UNSPEC;
	addressHints.ai_socktype = SOCK_STREAM;

	addrinfo *suggestedAddrInfo;
	int result = getaddrinfo(slackWebHookHost, "80", &addressHints, &suggestedAddrInfo);
	if (result != 0) {
		printf("getaddrinfo error \n");
	}

	int socketHandle;
	addrinfo *addr = suggestedAddrInfo;
	while (addr != NULL) {
		socketHandle = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
		if (socketHandle != -1) {
			break;
		}

		addr = addr->ai_next;
	}
	
	if (socketHandle == -1) {
		printf("Couldn't create socket \n");
	}

	char str[1024];
	inet_ntop(addr->ai_family, &(((sockaddr_in*)addr->ai_addr)->sin_addr), str, sizeof(str));
	printf("Connecting to %s \n", str);

	result = connect(socketHandle, addr->ai_addr, addr->ai_addrlen);
	if (result == -1) {
		printf("Couldn't connect \n");
	}

	const char *data = "POST "slackWebHookUrl" HTTP/1.1\nHost: "slackWebHookHost"\nContent-type: application/json\npayload={\"text\":\""testMessage"\"}\n\n";
	int dataLen = strlen(data)+1;
	printf("\n%s \n\n", data);

	result = write(socketHandle, data, dataLen);
	if (result != dataLen) {
		printf("Write error \n");
	}

	char buffer[4096];
	int readSize = read(socketHandle, buffer, sizeof(buffer));
	if (readSize == -1) {
		printf("Read error \n");
	}

	printf("Response \n%s \n", buffer);

	freeaddrinfo(suggestedAddrInfo);
	close(socketHandle);
}
