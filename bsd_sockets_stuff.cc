
// Doesn't work for slack cause you need ssl
void bsdSocketCode () {
	addrinfo addressHints = {};
	addressHints.ai_family = AF_UNSPEC;
	addressHints.ai_socktype = SOCK_STREAM;

	addrinfo *suggestedAddrInfo;
	int result = getaddrinfo(slackWebHookHost, "80", &addressHints, &suggestedAddrInfo);
	if (result != 0) {
		logMessage("getaddrinfo error");
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
		logMessage("Couldn't create socket");
	}

	char str[1024];
	inet_ntop(addr->ai_family, &(((sockaddr_in*)addr->ai_addr)->sin_addr), str, sizeof(str));
	logMessage("Connecting to %s", str);

	result = connect(socketHandle, addr->ai_addr, addr->ai_addrlen);
	if (result == -1) {
		logMessage("Couldn't connect");
	}

	const char *data = "POST "slackWebHookUrl" HTTP/1.1\nHost: "slackWebHookHost"\nContent-type: application/json\npayload={\"text\":\""testMessage"\"}\n\n";
	int dataLen = strlen(data)+1;
	logMessage("\n%s \n", data);

	result = write(socketHandle, data, dataLen);
	if (result != dataLen) {
		logMessage("Write error");
	}

	char buffer[4096];
	int readSize = read(socketHandle, buffer, sizeof(buffer));
	if (readSize == -1) {
		logMessage("Read error");
	}

	logMessage("Response \n%s", buffer);

	freeaddrinfo(suggestedAddrInfo);
	close(socketHandle);
}