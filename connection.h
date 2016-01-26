// -*- c++ -*-
#ifndef _CONNECTION_H_
#define _CONNECTION_H_

#include <unistd.h>
#include <stdlib.h>
#include <err.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "unique_fd.h"

namespace NetworkUtils {

struct AddrinfoDeleter {
	void operator()(addrinfo *x) {
		freeaddrinfo(x);
	}
};

std::unique_ptr<addrinfo, AddrinfoDeleter>
getaddrinfo(const char *host, const char *service, const addrinfo& hints);

const char *showAddress(const struct sockaddr *s);

unique_fd connectSocket(const char *host, const char *service);

};

class Connection {
	unique_fd mSocket;

	// convenience buffer
	char *mTempBuffer;
	size_t mTempBufferLen;

	char *mRecvBuffer;
	size_t mRecvBufferLen;

	char *mCursor;
	size_t mDataLen;

public:
	Connection(const char *host, const char *service)
		: mSocket(NetworkUtils::connectSocket(host, service))
	{
		mTempBufferLen = 1024;
		mTempBuffer = (char*) malloc(mTempBufferLen);
		if (!mTempBuffer)
			abort();

		mRecvBufferLen = 1024;
		mRecvBuffer = (char*) malloc(mRecvBufferLen);
		if (!mRecvBuffer)
			abort();

		mCursor = mRecvBuffer;
		mDataLen = 0;
	}
	~Connection() {
		// TODO FIXME: better to be unique_ptr?
		free(mTempBuffer);
		free(mRecvBuffer);
	}

	void writeBytes(const char *buf, size_t len);
	void writeString(const char *buf) {
		writeBytes(buf, strlen(buf));
	}
	char* readBytes(size_t len);
	char* readBytes(char *buf, size_t len);

	template <typename T>
	void writeRaw(T x) {
		writeBytes((char*) &x, sizeof(x));
	}
	template <typename T>
	T readRaw() {
		T x;
		readBytes((char*) &x, sizeof(x));
		return x;
	}
};

#endif /* _CONNECTION_H_ */
