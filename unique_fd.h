// -*- c++ -*-
#ifndef _UNIQUE_FD_H_
#define _UNIQUE_FD_H_

#include <unistd.h>
#include <algorithm>

// Adapted from http://stackoverflow.com/a/15762682

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

#endif /* _UNIQUE_FD_H_ */
