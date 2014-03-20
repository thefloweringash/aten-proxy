#include <cstdio>
#include <err.h>

#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <rfb/rfb.h>
#include <rfb/rfbregion.h>
#undef max // undo namespace pollution by rfb.h

#include <ev.h>

#include "keymap.h"

struct rfb_event_check {
	ev_check check;
	rfbScreenInfoPtr rfb;
};

class unique_fd {
	int mFD;
public:
	         unique_fd()      : mFD(-1) {}
	explicit unique_fd(int x) : mFD(x)  {}

	~unique_fd() {
		if (mFD != -1)
			close(mFD);
	}

	operator int() { return mFD; }

	unique_fd(unique_fd&& x)
		: mFD(-1)
	{
		std::swap(mFD, x.mFD);
	}
	unique_fd& operator =(unique_fd&& x) {
		std::swap(mFD, x.mFD);
		return *this;
	}
};

struct AddrinfoDeleter {
	void operator()(addrinfo *x) {
		freeaddrinfo(x);
	}
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

	static std::unique_ptr<addrinfo, AddrinfoDeleter>
	getaddrinfo_sp(const char *host, const char *service,
				   const addrinfo& hints)
	{
		addrinfo *addressinfo;
		int err = getaddrinfo(host, service, &hints, &addressinfo);
		if (err) {
			throw std::runtime_error("getaddrinfo failed");
		}
		return std::unique_ptr<addrinfo, AddrinfoDeleter>{addressinfo};
	}

	static const char *showAddress(const struct sockaddr *s) {
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

	static unique_fd connectSocket(const char *host, const char *service) {
		addrinfo hints;
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = PF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;

		auto info = getaddrinfo_sp(host, service, hints);
		for (addrinfo *x = info.get(); x; x = x->ai_next) {
			int s = socket(x->ai_family, x->ai_socktype, x->ai_protocol);
			if (s < 0) {
				perror("socket");
				continue;
			}

			int err = connect(s, x->ai_addr, x->ai_addrlen);
			if (err) {
				warn("connect to %s", showAddress(x->ai_addr));
				continue;
			}

			return unique_fd{ s };
		}
		throw std::runtime_error("connection failed");
	}


public:
	Connection(const char *host, const char *service)
		: mSocket(connectSocket(host, service))
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
		printf("connection constructor says hi\n");
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

struct WriteAction {
	enum {
		Key, UpdateFramebuffer, Ping
	} type;

	union {
		struct {
			rfbBool down;
			rfbKeySym keySym;
		} keyEvent;
		struct {
			uint8_t incremental;
			uint8_t x; uint8_t y;
			uint8_t w; uint8_t h;
		} updateFramebuffer;
	};

	static WriteAction makeKey(rfbBool down, rfbKeySym keySym) {
		WriteAction e;
		e.type = Key;
		e.keyEvent.keySym = keySym;
		e.keyEvent.down = down;
		return e;
	}
	static WriteAction makeUpdateFramebuffer(uint8_t incremental, uint8_t x, uint8_t y,
											 uint8_t w, uint8_t h)
	{
		WriteAction e;
		e.type = UpdateFramebuffer;
		e.updateFramebuffer = {incremental, x, y, w, h};
		return e;
	}
	static WriteAction makePing() {
		WriteAction e;
		e.type = Ping;
		return e;
	}
};

struct RFBUpdate {
	enum {
		SetFramebuffer,
		AddDirtyRect,
		AddDirtyRegion,
		SetServerName,
	} type;
	union {
		struct {
			char *newFramebuffer;
			int width;
			int height;
		} setFramebuffer;
		struct {
			int x1; int y1;
			int x2; int y2;
		} addDirtyRect;
		struct {
			sraRegionPtr region;
		} addDirtyRegion;
		struct {
			const char *name;
		} setServerName;
	};
};

class AtenServer {
public:
	AtenServer(int *argc, char **argv);
	void run();

private:
	WriteAction nextWriteAction();

	void doWriter();
	void doReader();

	void keyEventHandler(rfbBool down, rfbKeySym keySym, rfbClientPtr cl);

	void handleFrameUpdate();
	void handleRFBUpdates();

	void sendRFBUpdate(const RFBUpdate& u);
	void sendAction(const WriteAction& w);

	// rfb side
	rfbScreenInfoPtr mRFB;
	char *mFrameBuffer;
	int fbWidth, fbHeight;
	bool mSetServerName;
	bool mScreenOff;

	// upstream side
	std::unique_ptr<Connection> mConnection;

	std::thread mReaderThread;
	std::thread mWriterThread;

	std::mutex mActionMutex;
	std::condition_variable mActionCond;
	std::queue<WriteAction> mActionQueue;

	std::atomic_bool mTerminating;

	struct ev_loop *mEVLoop;
	std::mutex mRFBMutex;
	std::queue<RFBUpdate> mRFBUpdates;
	struct {
		ev_async async;
		AtenServer *self;
	} mRFBSignal;
};

WriteAction AtenServer::nextWriteAction() {
	std::unique_lock<std::mutex> lock{mActionMutex};
	std::queue<WriteAction>& q = mActionQueue;
	while (q.empty()) {
		mActionCond.wait(lock);
	}
	WriteAction ev = q.front();
	q.pop();
	return ev;
}

void AtenServer::doWriter() {
	// writer pulls from event queue and shoves out to upstream
	// socket. TODO FIXME: very close to an unlimited size binary
	// buffer, what, if anything, does this actually win us over
	// something like sendAsync(char*)? Answers: drop expired key
	// events, coalesce pointer events(possible with ikvm?), event
	// boundaries for re-establishing connections (char**), event
	// introspection on connection reset.

	try {
		while (!mTerminating) {
			WriteAction ev = nextWriteAction();
			switch (ev.type) {
			case WriteAction::Key: {
				auto& p = ev.keyEvent;
				struct {
					uint8_t messageType;
					uint8_t padding1;
					uint8_t down;
					char padding2[2];
					uint32_t key;
					char padding3[9];
				} __attribute__((packed)) req;
				memset(&req, 0, sizeof(req));
				uint8_t usage = keymap_usageForKeysym(p.keySym);
				printf("key %s keysym=%x usage=%x\n",
					   p.down ? "down" : "up",
					   p.keySym, usage);
				if (usage) {
					req.messageType = 4;
					req.down = p.down;
					req.key = htonl(usage);
					mConnection->writeBytes((char*) &req, sizeof(req));
				}
				break;
			}

			case WriteAction::UpdateFramebuffer: {
				auto& p = ev.updateFramebuffer;
				struct {
					uint8_t messageType;
					uint8_t incremental;
					uint16_t x,y,width,height;
				} req = {3, p.incremental, p.x, p.y, p.w, p.h};
				mConnection->writeBytes((char*) &req, sizeof(req));
				break;
			}

			case WriteAction::Ping:
				break;
			}
		}
	}
	catch (const std::runtime_error& e) {
		printf("writer terminating due to %s\n", e.what());
		mTerminating = true;
	}
	printf("writer exit\n");
}

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

static void copyPixels(char *out, const char *in, size_t count) {
	// TODO FIXME: potential bottleneck
	while (count --> 0) {
		const uint16_t ip = (in[0] & 0xff) | (in[1] << 8);

		uint8_t r = (ip >> 10) & 0x1f;
		uint8_t g = (ip >> 5) & 0x1f;
		uint8_t b = ip & 0x1f;

		const uint16_t op = r | g << 5 | b << 10;

		out[0] = op & 0xff;
		out[1] = op >> 8;
		out += 2;
		in += 2;
	}
}

void AtenServer::handleFrameUpdate() {
	char *fb = mFrameBuffer;

	mConnection->readBytes(1); // padding

	int nUpdates = ntohs(mConnection->readRaw<uint16_t>());
	// printf("nUpdates=%i\n", nUpdates);
	for (int update = 0; update < nUpdates; update++) {
		int x = ntohs(mConnection->readRaw<uint16_t>());
		int y = ntohs(mConnection->readRaw<uint16_t>());
		(void) x; (void) y;
		int width = ntohs(mConnection->readRaw<uint16_t>());
		int height = ntohs(mConnection->readRaw<uint16_t>());
		int encoding = ntohl(mConnection->readRaw<uint32_t>());
		int unknown = ntohl(mConnection->readRaw<uint32_t>());
		(void) unknown;
		(void) encoding;
		int dataLen = ntohl(mConnection->readRaw<uint32_t>());
		(void) dataLen;
		// printf("update[%d]: (%dx%d)+%d+%d len=%d\n",
		//	   update, width, height, x, y, dataLen);

		if (width == uint16_t(-640) && height == uint16_t(-480)) {
			if (!mScreenOff) {
				mScreenOff = true;
				printf("screen disappeared, showing error\n");
			}
			// screen is disabled
			memset(fb, 0xf0, fbWidth * fbHeight * 2);
			RFBUpdate u;
			u.type = RFBUpdate::AddDirtyRect;
			u.addDirtyRect.x1 = 0;
			u.addDirtyRect.y1 = 0;
			u.addDirtyRect.x2 = fbWidth;
			u.addDirtyRect.y2 = fbHeight;
			sendRFBUpdate(u);
		}
		else {
			if (mScreenOff) {
				printf("screen back again\n");
				mScreenOff = false;
			}
			if (width != fbWidth || height != fbHeight) {
				printf("framebuffer resizing!  %dx%d  -> %dx%d\n",
					   fbWidth, fbHeight, width, height);
				fb = mFrameBuffer =
					reinterpret_cast<char*>(malloc(width * height * 2));
				if (!fb) abort();

				fbWidth = width;
				fbHeight = height;

				RFBUpdate u;
				u.type = RFBUpdate::SetFramebuffer;
				u.setFramebuffer.width = width;
				u.setFramebuffer.height = height;
				u.setFramebuffer.newFramebuffer = fb;
				sendRFBUpdate(u);
			}
		}

		if (!mScreenOff) {
			int type = mConnection->readRaw<uint8_t>();
			(void) mConnection->readBytes(1);
			int segments = ntohl(mConnection->readRaw<uint32_t>());
			int totalLen = ntohl(mConnection->readRaw<uint32_t>());
			switch (type) {
			case 0: // subrects
				{
					bool haveRect = false;
					RFBUpdate u;
					u.type = RFBUpdate::AddDirtyRect;

					const int bsz = 16;
					for (int s = 0; s < segments; s++) {
						(void) mConnection->readBytes(4);
						int y = mConnection->readRaw<uint8_t>();
						int x = mConnection->readRaw<uint8_t>();
						const char *data = mConnection->readBytes(2 * bsz * bsz);

						char *out = fb + 2 * (y * bsz * fbWidth + x * bsz);
						char *end = fb + 2 * (fbHeight * fbWidth);
						for (int line = 0; line < bsz; line++) {
							int size = bsz * 2;
							if (out > end)
								break;
							if (out + size > end)
								size = end - out;
							copyPixels(out, data, size >> 1);
							out += 2 * fbWidth;
							data += size;
						}

						{
							int x1 = x * bsz,
								y1 = y * bsz,
								x2 = (x + 1) * bsz,
								y2 = (y + 1) * bsz;
							if (!haveRect) {
								u.addDirtyRect.x1 = x1;
								u.addDirtyRect.y1 = y1;
								u.addDirtyRect.x2 = x2;
								u.addDirtyRect.y2 = y2;
								haveRect = true;
							}
							else {
								u.addDirtyRect.x1 = std::min(u.addDirtyRect.x1, x1);
								u.addDirtyRect.y1 = std::min(u.addDirtyRect.y1, y1);
								u.addDirtyRect.x2 = std::max(u.addDirtyRect.x2, x2);
								u.addDirtyRect.y2 = std::max(u.addDirtyRect.y2, y2);
							}
						}
					}
					if (haveRect) {
						// printf("subrect update merged to: %dx%d+%d+%d\n",
						//	   u.addDirtyRect.x2 - u.addDirtyRect.x1,
						//	   u.addDirtyRect.y2 - u.addDirtyRect.y1,
						//	   u.addDirtyRect.x1,
						//	   u.addDirtyRect.y1);
						sendRFBUpdate(u);
					}
				}
				break;
			case 1:  // entire frame
				const char *data = mConnection->readBytes(totalLen - 10);
				copyPixels(fb, data, (totalLen - 10) >> 1);

				RFBUpdate u;
				u.type = RFBUpdate::AddDirtyRect;
				u.addDirtyRect.x1 = 0;
				u.addDirtyRect.y1 = 0;
				u.addDirtyRect.x2 = fbWidth;
				u.addDirtyRect.y2 = fbHeight;
				sendRFBUpdate(u);
				break;
			}
		}
	}
	sendAction(
		WriteAction::makeUpdateFramebuffer(mScreenOff ? 0 /* full */ : 1 /* incrememntal */,
										   0, 0, 0, 0));
}

void AtenServer::doReader() {
	try {
		while (!mTerminating) {
			int messageType = mConnection->readRaw<uint8_t>();
			// printf("messagetype=%i\n", messageType);
			switch (messageType) {
			case 0:
				handleFrameUpdate();
				break;
			case 4:
				(void) mConnection->readBytes(20);
				break;
			case 0x16:
				(void) mConnection->readBytes(1);
				break;
			case 0x37:
				(void) mConnection->readBytes(2);
				break;
			case 0x39:
				(void) mConnection->readBytes(264);
				break;
			case 0x3c:
				(void) mConnection->readBytes(8);
				break;
			default:
				abort();
			}
		}
	}
	catch (const std::runtime_error& e) {
		mTerminating = true;
		sendAction(WriteAction::makePing());
		printf("Reader terminating due to error: %s", e.what());
	}
	printf("reader exit\n");
}


void AtenServer::sendAction(const WriteAction& w) {
	std::unique_lock<std::mutex> lock{mActionMutex};
	mActionQueue.push(w);
	mActionCond.notify_all();
}

void AtenServer::sendRFBUpdate(const RFBUpdate& u) {
	std::unique_lock<std::mutex> lock{mRFBMutex};
	mRFBUpdates.push(u);
	ev_async_send(mEVLoop, &mRFBSignal.async);
}

void AtenServer::keyEventHandler(rfbBool down, rfbKeySym keySym, rfbClientPtr cl) {
	(void) cl;
	sendAction(WriteAction::makeKey(down, keySym));
}


AtenServer::AtenServer(int *argc, char **argv) {
	fbWidth = 640;
	fbHeight = 480;
	mRFB = rfbGetScreen(argc, argv, fbWidth, fbHeight, 5, 3, 2);
	mFrameBuffer = reinterpret_cast<char*>(malloc(fbWidth * fbHeight * 2));
	memset(mFrameBuffer, 0, fbWidth * fbHeight * 2);

	mRFB->frameBuffer = mFrameBuffer;
	mRFB->kbdAddEvent = [](rfbBool down, rfbKeySym keySym, rfbClientPtr cl){
		AtenServer *self = reinterpret_cast<AtenServer*>(
			cl->screen->screenData);
		self->keyEventHandler(down, keySym, cl);
	};

	rfbInitServer(mRFB);

	keymap_init();
}

void AtenServer::handleRFBUpdates() {
	while (true) {
		RFBUpdate ev;
		{
			std::unique_lock<std::mutex> lock{mRFBMutex};
			if (mRFBUpdates.empty())
				return;
			ev = mRFBUpdates.front();
			mRFBUpdates.pop();
		}
		// printf("handleRFBUpdate, type=%d\n", ev.type);
		switch (ev.type) {
		case RFBUpdate::SetFramebuffer: {
			auto& p = ev.setFramebuffer;
			char *oldFramebuffer = mRFB->frameBuffer;
			printf("framebuffer change: %p[%dx%d] -> %p[%dx%d]\n",
				   oldFramebuffer, mRFB->width, mRFB->height,
				   p.newFramebuffer, p.width, p.height);
			rfbNewFramebuffer(mRFB, p.newFramebuffer, p.width, p.height, 5, 3, 2);
			free(oldFramebuffer);
			break;
		}

		case RFBUpdate::AddDirtyRect: {
			auto &p = ev.addDirtyRect;
			rfbMarkRectAsModified(mRFB, p.x1, p.y1, p.x2, p.y2);
			break;
		}

		case RFBUpdate::AddDirtyRegion: {
			auto &p = ev.addDirtyRegion;
			rfbMarkRegionAsModified(mRFB, p.region);
			// sraRgnDestroy(p.region);
			break;
		}

		case RFBUpdate::SetServerName: {
			auto &p = ev.setServerName;
			const char *oldName = mRFB->desktopName;
			mRFB->desktopName = p.name;
			if (mSetServerName) {
				free((void*) oldName);
			}
			mSetServerName = true;
			break;
		}

		}
	}
}

void AtenServer::run() {
	// set here instead of constructor to not break inheritance
	mRFB->screenData = this;

	struct ev_loop *loop = mEVLoop = EV_DEFAULT;

	// really simple integration of libvncserver's event loop into
	// libev. while completely useless now, ideally this integration
	// would be extended to monitor the sockets used by libvncserver.
	ev_idle keepalive;
	ev_idle_init(&keepalive, [](EV_P_ ev_idle *w, int revents){
			(void) loop; (void) w; (void) revents;
			// global idle handler prevents loop from sleeping.
		});
	ev_idle_start(loop, &keepalive);

	rfb_event_check c;
	c.rfb = mRFB;
	ev_check_init(&c.check, [](EV_P_ ev_check *w, int revents){
			// once around every libev loop we step the libvncserver
			// loop.
			(void) loop; (void) revents;
			rfbScreenInfoPtr p = reinterpret_cast<rfb_event_check*>(w)->rfb;
			rfbProcessEvents(p, -1);
		});
	ev_check_start(loop, &c.check);

	// and a way to stuff events into the libvncserver loop:
	mRFBSignal.self = this;
	ev_async_init(&mRFBSignal.async, [](EV_P_ ev_async *w, int revents) {
			(void) loop; (void) revents;
			AtenServer *self = reinterpret_cast<decltype(mRFBSignal)*>(w)->self;
			self->handleRFBUpdates();
		});
	ev_async_start(loop, &mRFBSignal.async);

	std::thread{ev_run, loop, 0}.detach();

	while (true) {
		try {
			struct {
				char username[24];
				char password[24];
			} auth = {{0},{0}};
			strlcpy(auth.username, "testuser", sizeof(auth.username));
			strlcpy(auth.password, "testpass", sizeof(auth.password));

			mConnection = std::unique_ptr<Connection>{
				new Connection("localhost", "5901")};

			// handshake
			mConnection->readBytes(strlen("RFB 003.008\n"));
			mConnection->writeString("RFB 003.008\n");

			// security type
			int nSecurity = mConnection->readRaw<uint8_t>();
			char *security = mConnection->readBytes(nSecurity);
			if (security[0] != 16) {
				abort();
			}
			mConnection->writeRaw<uint8_t>(16);

			// unknown reply from aten, 24 bytes
			(void) mConnection->readBytes(24);

			// auth
			mConnection->writeBytes((char*) &auth, sizeof(auth));
			int authErr = mConnection->readRaw<uint32_t>();
			if (authErr) {
				abort();
			}

			// client init
			mConnection->writeRaw<uint8_t>(0);

			// server init; aten sends complete garbaage
			(void) mConnection->readBytes(sizeof(uint16_t) * 2 + 16); // dimensions

			int serverNameLen = ntohl(mConnection->readRaw<uint32_t>());
			char *serverName = mConnection->readBytes(serverNameLen);
			RFBUpdate u;
			u.type = RFBUpdate::SetServerName;
			u.setServerName.name = strdup(serverName);
			sendRFBUpdate(u);

			// more aten unknown
			(void) mConnection->readBytes(12);

			// initial screen update
			sendAction(WriteAction::makeUpdateFramebuffer(0, 0, 0, 0, 0));

			mWriterThread = std::thread{[this]{doWriter();}};
			mReaderThread = std::thread{[this]{doReader();}};
			mWriterThread.join();
			mReaderThread.join();
			mTerminating.store(false);
			mConnection = nullptr;
		}
		catch (const std::runtime_error& x) {
			printf("connection error: %s\n", x.what());
			sleep(1);
		}
	}
}

int main(int argc, char **argv) {
	AtenServer server {&argc, argv};
	server.run();
}
