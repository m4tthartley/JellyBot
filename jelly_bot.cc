
#include "../gjLib/gj_lib.h"
#include "../gjLib/gj_linux.cc"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <dlfcn.h>
#include <curl/curl.h>

// https://hooks.slack.com
#define slackTestUrl "https://hooks.slack.com/services/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"
#define slackPublicUrl "https://hooks.slack.com/services/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"
#define slackWebHookUrl slackPublicUrl
#define slackWebHookHost "https://hooks.slack.com"
#define ENABLE_SENDING true

#define SECS(num) (num)
#define MINS(num) (num*60)
#define HOURS(num) (num*MINUTES(60))

struct Message {
	int originalId;
	int storageOffset;
	int idOfLastUse;
};

struct State {
	gjMemStack fileStack;
	gjMemStack messageStorage;
	gjMemStack messageStack;
	Message *messageTable;
	int messageTableSize;
	int currentIncrement;
	int originalIdIncrement;
};

void exitWithError (const char *message) {
	printf("%s, exiting... \n", message);
	exit(0);
}

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

void sendSlackMessage (State *state, int storageIndex, const char *message) {
	printf("%s \n\n", message);

#if ENABLE_SENDING
		if (globalCurl) {
			const char *formatStr = "payload={\"text\":\"%s\"}";
			int strSize = gjStrlen(message) + gjStrlen(formatStr);
			char *str = gjPushMemStack(&state->fileStack, strSize, true);
			sprintf(str, formatStr, message);

			curlEasySetopt(globalCurl, CURLOPT_URL, slackWebHookUrl);
			curlEasySetopt(globalCurl, CURLOPT_POSTFIELDS, str);
			CURLcode result = curlEasyPerform(globalCurl);
			if (result != CURLE_OK) {
				printf("curl_easy_perform error \n");
			}
			
			gjPopMemStack(&state->fileStack, strSize);
		}
#endif
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

void loadMessageData (State *state);

void saveStateToDisk (State *state) {
	// todo: Backup save file
	gjFile saveFile = gjCreateFile("state.sav");
	gjWrite(saveFile, state, sizeof(State));
	gjWrite(saveFile, state->messageStorage.mem, state->messageStorage.size);
	gjWrite(saveFile, state->messageStack.mem, state->messageStack.size);
	gjCloseFile(saveFile);

	// save to info file
	gjFile infoFile = gjCreateFile("info.txt");
	char str[1024];
	sprintf(str, "messageTableSize %i\ncurrentIncrement %i\n\n", state->messageTableSize, state->currentIncrement);
	gjWrite(infoFile, str, gjStrlen(str));

	fiz (state->messageTableSize) {
		gjClearMem(str, 1024);
		sprintf(str, "id %i use %i \"%.64s\" \n", state->messageTable[i].originalId, state->messageTable[i].idOfLastUse, state->messageStorage.mem + state->messageTable[i].storageOffset);
		gjWrite(infoFile, str, gjStrlen(str));
	}
	gjCloseFile(infoFile);
}

void loadStateFromDiskOrInit (State *state) {
	state->fileStack = gjInitMemStack(megabytes(20));
	state->messageStorage = gjInitMemStack(megabytes(10));
	state->messageStack = gjInitMemStack(megabytes(1));
	state->messageTable = (Message*)state->messageStack.mem;

	if (gjFileExists("state.sav")) {
		printf("Save file found, loading state... \n");

		gjData saveData = gjReadFile("state.sav", &state->fileStack);
		State *tempState = (State*)saveData.mem;
		state->messageTableSize = tempState->messageTableSize;
		state->currentIncrement = tempState->currentIncrement;
		state->originalIdIncrement = tempState->originalIdIncrement;

		void *saveMessageStorage = saveData.mem + sizeof(State);
		gjMemcpy(state->messageStorage.mem, saveMessageStorage, tempState->messageStorage.size);

		void *saveMessageStack = (char*)saveMessageStorage + tempState->messageStorage.size;
		gjMemcpy(state->messageStack.mem, saveMessageStack, tempState->messageStack.size);

		saveStateToDisk(state);
	} else {
		printf("No save file found, creating state... \n");

		loadMessageData(state);
		saveStateToDisk(state);
	}
}

void loadMessagesFromQuoteFile (State *state, gjData fileData, int *skippedMessages, int *parsedMessages) {
	char *start = fileData.mem;
	char *ptr = start;
	char *tokenStart = ptr;
	while (ptr < start + fileData.size) {
		while (*ptr == ' ' || *ptr == '\t' || *ptr == '\n') {
			++ptr;
		}

		tokenStart = ptr;

		if (*ptr == '/') {
			while (*ptr != '\n' && ptr < start + fileData.size) {
				++ptr;
			}
			++ptr;
			tokenStart = ptr;
		} else if (ptr < start + fileData.size) {
			++*parsedMessages;

			++ptr;
			while ((*ptr != '\n' || ptr[1] != '\n') &&
				   (*ptr != '\n' || (ptr + 1) < start + fileData.size) &&
				   ptr < start + fileData.size) {
				++ptr;
			}

			*ptr = 0;
			
			bool alreadyLoaded = false;
			fiz (state->messageTableSize) {
				if (gjStrcmp(tokenStart, (state->messageStorage.mem + state->messageTable[i].storageOffset)) == 0) {
					alreadyLoaded = true;
					break;
				}
			}
			if (!alreadyLoaded) {
				++state->messageTableSize;
				Message *msg = (Message*)gjPushMemStack(&state->messageStack, sizeof(Message), true);
				char *storage = gjPushMemStack(&state->messageStorage, gjStrlen(tokenStart) + 1);
				gjStrcpy(storage, tokenStart);
				msg->storageOffset = (int)(storage - state->messageStorage.mem);
				msg->originalId = state->originalIdIncrement++;
			} else {
				++*skippedMessages;
			}

			++ptr;
			tokenStart = ptr;
		}
	}
}

void loadMessageData (State *state) {
	printf("Loading data... \n");
	int parsedMessages = 0;
	int skippedMessages = 0;
	
	char *files[64];
	int fileCount = gjFindFiles("*.quotes", &state->fileStack, files, arraySize(files));
	printf("Loading files: \n");
	fiz (fileCount) {
		printf("%s \n", files[i]);
	}
	printf("\n");

	fiz (fileCount) {
		gjData messageData = gjReadFile(files[i], &state->fileStack);
		loadMessagesFromQuoteFile(state, messageData, &skippedMessages, &parsedMessages);
	}

	printf("parsed messages %i \n", parsedMessages);
	printf("skipped messages %i \n", skippedMessages);
	printf("Done \n\n");

	gjClearMemStack(&state->fileStack);
}

void sendRandomMessage (State *state) {
	if (state->messageTableSize > 1) {
		// Sort message by idOfLastUse
		fiz (state->messageTableSize - 1) {
			fjz (state->messageTableSize - 1) {
				if (state->messageTable[j].idOfLastUse > state->messageTable[j+1].idOfLastUse) {
					// Swap
					Message tempMsg = state->messageTable[j];
					state->messageTable[j] = state->messageTable[j+1];
					state->messageTable[j+1] = tempMsg;
				}
			}
		}

		int randMsgIndex = rand() % (state->messageTableSize / 2);
		state->messageTable[randMsgIndex].idOfLastUse = ++state->currentIncrement;

		char *message = state->messageStorage.mem + state->messageTable[randMsgIndex].storageOffset;
		sendSlackMessage(state, randMsgIndex, message);
	}
}

int main () {
	State state = {};
	loadStateFromDiskOrInit(&state);

	const char *writeStr = "Princess Leia Organa: No! Alderaan is peaceful! We have no weapons, you can't possibly...";
	gjWriteFile("writetest.txt", (void*)writeStr, gjStrlen(writeStr));

	printf("messageTableSize %i \n", state.messageTableSize);
	printf("currentIncrement %i \n", state.currentIncrement);

	loadCurlCode();
	initCurl();

	timespec startTime;
	clock_gettime(CLOCK_REALTIME, &startTime);
	srand(startTime.tv_nsec);

	int hourOfLastMessage = getLocalHour();

	// int interval = HOURS(1);
	int interval = SECS(10);

	int lastMessageIndex = 0;

	int messageHours[] = {
		9, 12, 15, 18, 21, 0,
	};

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

		printf("Hour %i \n", hour);

		if (hour != hourOfLastMessage) {
			hourOfLastMessage = hour;

			// Only run if it's not night time, last is at midnight, first is at 9am
			bool isItTime = false;
			fiz (arraySize(messageHours)) {
				if (hour == messageHours[i]) {
					isItTime = true;
					break;
				}
			}
			if (isItTime/*hour >= 9 || hour == 0*/) {
				loadMessageData(&state);

				sendRandomMessage(&state);
				saveStateToDisk(&state);
			} else {
				printf("It's in-between mid-night and 9am so I'll keep sleeping... \n\n");
			}
		} else {
			printf("Not time to send message yet, %i minutes left, going back to sleep... \n\n", (60 - minute));
		}

		sleep(MINS(1));
	}

	stopCurl();
}
