/*
 * $Id$
 *
    Copyright (c) 2015-2016 Chung, Hyung-Hwan. All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:
    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WAfRRANTIES
    OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
    IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
    NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
    THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "mio-sys.h"

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

/* ========================================================================= */
#if defined(USE_POLL)
#	define MUX_INDEX_INVALID MIO_TYPE_MAX(mio_oow_t)
#endif

int mio_sys_initmux (mio_t* mio)
{
	mio_sys_mux_t* mux = &mio->sysdep->mux;

#if defined(USE_EPOLL)
	mux->hnd = epoll_create(1000); /* TODO: choose proper initial size? */
	if (mux->hnd == -1)
	{
		mio_seterrwithsyserr (mio, 0, errno);
		return -1;
	}
#endif

	return 0;
}

void mio_sys_finimux (mio_t* mio)
{
	mio_sys_mux_t* mux = &mio->sysdep->mux;
#if defined(USE_EPOLL)
	close (mux->hnd);
#endif
}

int mio_sys_ctrlmux (mio_t* mio, mio_sys_mux_cmd_t cmd, mio_dev_t* dev, int dev_cap)
{
#if defined(USE_POLL)
	mio_sys_mux_t* mux = &mio->sysdep->mux;
	mio_oow_t idx;

	if (dev->hnd >= mux->map.capa)
	{
		mio_oow_t new_capa;
		mio_oow_t* tmp;

		if (cmd != MIO_SYS_MUX_CMD_INSERT)
		{
			mio_seterrnum (mio, MIO_ENOENT);
			return -1;
		}

		new_capa = MIO_ALIGN_POW2((hnd + 1), 256);

		tmp = mio_reallocmem(mio, mux->map.ptr, new_capa * MIO_SIZEOF(*tmp));
		if (!tmp) return -1;

		for (idx = mux->map.capa; idx < new_capa; idx++) 
			tmp[idx] = MUX_INDEX_INVALID;

		mux->map.ptr = tmp;
		mux->map.capa = new_capa;
	}

	idx = mux->map.ptr[hnd];
	if (idx != MUX_INDEX_INVALID)
	{
		if (cmd == MIO_SYS_MUX_CMD_INSERT)
		{
			mio_seterrnum (mio, MIO_EEXIST);
			return -1;
		}
	}
	else
	{
		if (cmd != MIO_SYS_MUX_CMD_INSERT)
		{
			mio_seterrnum (mio, MIO_ENOENT);
			return -1;
		}
	}

	switch (cmd)
	{
		case MIO_SYS_MUX_CMD_INSERT:

			if (mux->pd.size >= mux->pd.capa)
			{
				mio_oow_t new_capa;
				struct pollfd* tmp1;
				mio_dev_t** tmp2;

				new_capa = MIO_ALIGN_POW2(mux->pd.size + 1, 256);

				tmp1 = mio_reallocmem(mio, mux->pd.pfd, new_capa * MIO_SIZEOF(*tmp1));
				if (!tmp1) return -1;

				tmp2 = mio_reallocmem(mio, mux->pd.dptr, new_capa * MIO_SIZEOF(*tmp2));
				if (!tmp2)
				{
					mio_freemem (mio, tmp1);
					return -1;
				}

				mux->pd.pfd = tmp1;
				mux->pd.dptr = tmp2;
				mux->pd.capa = new_capa;
			}

			idx = mux->pd.size++;

			mux->pd.pfd[idx].fd = hnd;
			mux->pd.pfd[idx].events = 0;
			if (dev_cap & MIO_DEV_CAP_IN_WATCHED) mux->pd.pfd[idx].events |= POLLIN;
			if (dev_cap & MIO_DEV_CAP_OUT_WATCHED) mux->pd.pfd[idx].events |= POLLOUT;
			mux->pd.pfd[idx].revents = 0;
			mux->pd.dptr[idx] = dev;

			mux->map.ptr[hnd] = idx;

			return 0;

		case MIO_SYS_MUX_CMD_UPDATE:
			MIO_ASSERT (mio, mux->pd.dptr[idx] == dev);
			mux->pd.pfd[idx].events = 0;
			if (dev_cap & MIO_DEV_CAP_IN_WATCHED) mux->pd.pfd[idx].events |= POLLIN;
			if (dev_cap & MIO_DEV_CAP_OUT_WATCHED) mux->pd.pfd[idx].events |= POLLOUT;
			return 0;

		case MIO_SYS_MUX_CMD_DELETE:
			MIO_ASSERT (mio, mux->pd.dptr[idx] == dev);
			mux->map.ptr[hnd] = MUX_INDEX_INVALID;

			/* TODO: speed up deletion. allow a hole in the array.
			 *       delay array compaction if there is a hole.
			 *       set fd for the hole to -1 such that poll()
			 *       ignores it. compact the array if another deletion 
			 *       is requested when there is an existing hole. */
			idx++;
			while (idx < mux->pd.size)
			{
				int fd;

				mux->pd.pfd[idx - 1] = mux->pd.pfd[idx];
				mux->pd.dptr[idx - 1] = mux->pd.dptr[idx];

				fd = mux->pd.pfd[idx].fd;
				mux->map.ptr[fd] = idx - 1;

				idx++;
			}

			mux->pd.size--;

			return 0;

		default:
			mio_seterrnum (mio, MIO_EINVAL);
			return -1;
	}
#elif defined(USE_EPOLL)
	mio_sys_mux_t* mux = &mio->sysdep->mux;
	static int epoll_cmd[] = { EPOLL_CTL_ADD, EPOLL_CTL_MOD, EPOLL_CTL_DEL };
	struct epoll_event ev;

	MIO_ASSERT (mio, mio == dev->mio);

	ev.data.ptr = dev;
	ev.events = EPOLLHUP | EPOLLERR /*| EPOLLET*/;

	if (dev_cap & MIO_DEV_CAP_IN_WATCHED) 
	{
		ev.events |= EPOLLIN;
	#if defined(EPOLLRDHUP)
		ev.events |= EPOLLRDHUP;
	#endif
		if (dev_cap & MIO_DEV_CAP_PRI_WATCHED) ev.events |= EPOLLPRI;
	}

	if (dev_cap & MIO_DEV_CAP_OUT_WATCHED) ev.events |= EPOLLOUT;

	if (epoll_ctl(mux->hnd, epoll_cmd[cmd], dev->dev_mth->getsyshnd(dev), &ev) == -1)
	{
		mio_seterrwithsyserr (mio, 0, errno);
		return -1;
	}

	return 0;
#else
#	error NO SUPPORTED MULTIPLEXER
#endif
}

int mio_sys_waitmux (mio_t* mio, const mio_ntime_t* tmout, mio_sys_mux_evtcb_t event_handler)
{
#if defined(USE_POLL)
	int nentries;

	mux = (mio_sys_mux_t*)mio->sysdep->mux;

	nentries = poll(mux->pd.pfd, mux->pd.size, MIO_SECNSEC_TO_MSEC(tmout.sec, tmout.nsec));
	if (nentries == -1)
	{
		if (errno == EINTR) return 0;
		mio_seterrwithsyserr (mio, 0, errno);
		return -1;
	}

	for (i = 0; i < mux->pd.size; i++)
	{
		if (mux->pd.pfd[i].fd >= 0 && mux->pd.pfd[i].revents)
		{
			int events = 0;
			mio_dev_t* dev;

			dev = mux->pd.dptr[i];

			MIO_ASSERT (mio, !(mux->pd.pfd[i].revents & POLLNVAL));
			if (mux->pd.pfd[i].revents & POLLIN) events |= MIO_DEV_EVENT_IN;
			if (mux->pd.pfd[i].revents & POLLOUT) events |= MIO_DEV_EVENT_OUT;
			if (mux->pd.pfd[i].revents & POLLPRI) events |= MIO_DEV_EVENT_PRI;
			if (mux->pd.pfd[i].revents & POLLERR) events |= MIO_DEV_EVENT_ERR;
			if (mux->pd.pfd[i].revents & POLLHUP) events |= MIO_DEV_EVENT_HUP;

			handle_event (dev, events, 0);
		}
	}

#elif defined(USE_EPOLL)

	mio_sys_mux_t* mux = &mio->sysdep->mux;
	int nentries, i;

	nentries = epoll_wait(mux->hnd, mux->revs, MIO_COUNTOF(mux->revs), MIO_SECNSEC_TO_MSEC(tmout->sec, tmout->nsec));
	if (nentries == -1)
	{
		if (errno == EINTR) return 0; /* it's actually ok */
		/* other errors are critical - EBADF, EFAULT, EINVAL */
		mio_seterrwithsyserr (mio, 0, errno);
		return -1;
	}

	/* TODO: merge events??? for the same descriptor */
	
	for (i = 0; i < nentries; i++)
	{
		int events = 0, rdhup = 0;
		mio_dev_t* dev;

		dev = mux->revs[i].data.ptr;

		if (mux->revs[i].events & EPOLLIN) events |= MIO_DEV_EVENT_IN;
		if (mux->revs[i].events & EPOLLOUT) events |= MIO_DEV_EVENT_OUT;
		if (mux->revs[i].events & EPOLLPRI) events |= MIO_DEV_EVENT_PRI;
		if (mux->revs[i].events & EPOLLERR) events |= MIO_DEV_EVENT_ERR;
		if (mux->revs[i].events & EPOLLHUP) events |= MIO_DEV_EVENT_HUP;
	#if defined(EPOLLRDHUP)
		else if (mux->revs[i].events & EPOLLRDHUP) rdhup = 1;
	#endif

		event_handler (mio, dev, events, rdhup);
	}
#else

#	error NO SUPPORTED MULTIPLEXER
#endif

	return 0;
}
