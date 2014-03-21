#include <errno.h>
#include <string.h>

#include <arpa/inet.h>
#include <sys/types.h>

#include <stdexcept>
#include <memory>

#include "connection.h"

namespace NetworkUtils {

std::unique_ptr<addrinfo, AddrinfoDeleter>
getaddrinfo(const char *host, const char *service,
                        const addrinfo& hints)
{
	addrinfo *addressinfo;
	int err = ::getaddrinfo(host, service, &hints, &addressinfo);
	if (err) {
		throw std::runtime_error("getaddrinfo failed");
	}
	return std::unique_ptr<addrinfo, AddrinfoDeleter>{addressinfo};
}

const char *showAddress(const struct sockaddr *s) {
	static char buffer[INET6_ADDRSTRLEN];
	switch (s->sa_family) {
	case AF_INET:
		inet_ntop(s->sa_family,
		          &((struct sockaddr_in *)s)->sin_addr, buffer, sizeof(buffer));
		return buffer;
	case AF_INET6:
		inet_ntop(s->sa_family,
                  &((struct sockaddr_in6 *)s)->sin6_addr, buffer, sizeof(buffer));
		return buffer;
	default:
		return nullptr;
	}
}

unique_fd connectSocket(const char *host, const char *service) {
	addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	auto info = getaddrinfo(host, service, hints);
	for (addrinfo *x = info.get(); x; x = x->ai_next) {
		unique_fd s { socket(x->ai_family, x->ai_socktype, x->ai_protocol) };
		if (s < 0) {
			perror("socket");
			continue;
		}

		int err = connect(s, x->ai_addr, x->ai_addrlen);
		if (err) {
			warn("connect to %s", showAddress(x->ai_addr));
			continue;
		}

		return s;
	}
	throw std::runtime_error("connection failed");
}

};

void Connection::writeBytes(const char *buf, size_t len) {
	size_t off = 0;
	while (off < len) {
		ssize_t n = send(mSocket, buf + off, len - off, 0);
		if (n < 0) {
			if (errno != EINTR) {
				perror("send");
				abort();
			}
		}
		else {
			off += n;
		}
	}
}

char* Connection::readBytes(size_t len) {
	if (mTempBufferLen < len) {
		while (mTempBufferLen < len)
			mTempBufferLen <<= 1;
		mTempBuffer = reinterpret_cast<char*>(
			realloc(mTempBuffer, mTempBufferLen));
		if (!mTempBuffer)
			abort();
	}
	return readBytes(mTempBuffer, len);
}

char *Connection::readBytes(char *buf, size_t len) {
	size_t off = 0;

	// take from buffer
	if (mDataLen) {
		size_t take = std::min(mDataLen, len);
		memcpy(buf, mCursor, take);

		mDataLen -= take;
		mCursor += take;
		off += take;
	}

	// take from socket, ignoring buffer if > buffer size
	while (len - off > mRecvBufferLen) {
		ssize_t n = recv(mSocket, buf + off, len - off, 0);
		if (n < 0) {
			if (errno != EINTR) {
				perror("recv");
				throw std::runtime_error("read failed");
			}
		}
		else if (n == 0) {
			// we got shut down
			throw std::runtime_error("remote host shut us down");
		}
		else {
			off += n;
		}
	}

	// take from socket to buffer, keeping leftovers in the buffer
	if (len - off > 0) {
		// buffer is empty, reset and fill buffer to at least len -
		// off
		mCursor = mRecvBuffer;
		mDataLen = 0;
		while (len - off > mDataLen) {
			ssize_t n = recv(mSocket, mRecvBuffer + mDataLen, mRecvBufferLen - mDataLen, 0);
			if (n < 0) {
				if (errno != EINTR) {
					perror("recv");
					throw std::runtime_error("read failed");
				}
			}
			else if (n == 0) {
				// we got shut down
				throw std::runtime_error("remote host shut us down");
			}
			else {
				mDataLen += n;
			}
		}

		size_t take = len - off;
		memcpy(buf + off, mCursor, take);
		mDataLen -= take;
		mCursor += take;
		off += take;
	}

	return buf;
}
