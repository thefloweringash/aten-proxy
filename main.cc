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

#include "unique_fd.h"
#include "connection.h"
#include "keymap.h"

struct rfb_event_check {
	ev_check check;
	rfbScreenInfoPtr rfb;
};

struct WriteAction {
	enum Type {
		Key, UpdateFramebuffer, Ping
	} type;

	union {
		struct {
			rfbBool down;
			rfbKeySym keySym;
		} keyEvent;
		struct {
			uint8_t incremental;
			uint16_t x; uint16_t y;
			uint16_t w; uint16_t h;
		} updateFramebuffer;
	};

	template <Type type> struct setter;
};

template <> struct WriteAction::setter<WriteAction::Key> {
	template <typename... Args> static void set(WriteAction& u, Args&& ...args) {
		u.keyEvent = {args...};
	}
};
template <> struct WriteAction::setter<WriteAction::UpdateFramebuffer> {
	static void set(WriteAction& u, uint8_t i, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
		u.updateFramebuffer = {i, x, y, w ,h};
	}
};
template <> struct WriteAction::setter<WriteAction::Ping> {
	static void set(WriteAction& u) {
		(void) u;
	}
};


struct RFBUpdate {
	enum Type {
		SetFramebuffer,
		AddDirtyRect,
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
			const char *name;
		} setServerName;
	};

	template <Type type> struct setter;
};

template <> struct RFBUpdate::setter<RFBUpdate::SetFramebuffer> {
	template <typename... Args> static void set(RFBUpdate& u, Args&& ...args) {
		u.setFramebuffer = {args...};
	}
};
template <> struct RFBUpdate::setter<RFBUpdate::SetServerName> {
	template <typename... Args> static void set(RFBUpdate& u, Args&& ...args) {
		u.setServerName = {args...};
	}
};
template <> struct RFBUpdate::setter<RFBUpdate::AddDirtyRect> {
	template <typename... Args> static void set(RFBUpdate& u, Args&& ...args) {
		u.addDirtyRect = {args...};
	}
};


#define EV(type, tag) type, type::tag

template <typename E, typename E::Type type, typename... Args>
E makeEvent(Args&& ...args) {
	E e;
	e.type = type;
	E::template setter<type>::set(e, std::forward<Args>(args)...);
	return e;
}
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
	int mFBWidth, mFBHeight;
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
				// TODO FIXME: byte ordering of x, y, width and height
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
			memset(fb, 0xf0, mFBWidth * mFBHeight * 2);
			sendRFBUpdate(makeEvent<EV(RFBUpdate, AddDirtyRect)>(0, 0, mFBWidth, mFBHeight));
		}
		else {
			if (mScreenOff) {
				printf("screen back again\n");
				mScreenOff = false;
			}
			if (width != mFBWidth || height != mFBHeight) {
				printf("framebuffer resizing!  %dx%d  -> %dx%d\n",
					   mFBWidth, mFBHeight, width, height);
				fb = mFrameBuffer =
					reinterpret_cast<char*>(malloc(width * height * 2));
				if (!fb) abort();

				mFBWidth = width;
				mFBHeight = height;

				sendRFBUpdate(makeEvent<EV(RFBUpdate, SetFramebuffer)>(fb, width, height));
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

						char *out = fb + 2 * (y * bsz * mFBWidth + x * bsz);
						char *end = fb + 2 * (mFBHeight * mFBWidth);
						for (int line = 0; line < bsz; line++) {
							int size = bsz * 2;
							if (out > end)
								break;
							if (out + size > end)
								size = end - out;
							copyPixels(out, data, size >> 1);
							out += 2 * mFBWidth;
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

				sendRFBUpdate(makeEvent<EV(RFBUpdate, AddDirtyRect)>(0, 0, mFBWidth, mFBHeight));
				break;
			}
		}
	}
	sendAction(
		makeEvent<EV(WriteAction, UpdateFramebuffer)>(
			mScreenOff ? 0 /* full */ : 1 /* incrememntal */,
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
		sendAction(makeEvent<EV(WriteAction,Ping)>());
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
	sendAction(makeEvent<EV(WriteAction,Key)>(down, keySym));
}


AtenServer::AtenServer(int *argc, char **argv) {
	mFBWidth = 640;
	mFBHeight = 480;
	mRFB = rfbGetScreen(argc, argv, mFBWidth, mFBHeight, 5, 3, 2);
	mFrameBuffer = reinterpret_cast<char*>(malloc(mFBWidth * mFBHeight * 2));
	memset(mFrameBuffer, 0, mFBWidth * mFBHeight * 2);

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
			sendAction(makeEvent<EV(WriteAction, UpdateFramebuffer)>(0, 0, 0, 0, 0));

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
