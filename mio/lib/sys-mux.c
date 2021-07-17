/*
 * $Id$
 *
    Copyright (c) 2016-2020 Chung, Hyung-Hwan. All rights reserved.

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

#include "sys-prv.h"

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

/* ========================================================================= */
#if defined(USE_POLL)
#	define MUX_INDEX_INVALID MIO_TYPE_MAX(mio_oow_t)
#	define MUX_INDEX_SUSPENDED (MUX_INDEX_INVALID - 1)
#endif

int mio_sys_initmux (mio_t* mio)
{
	mio_sys_mux_t* mux = &mio->sysdep->mux;

	/* create a pipe for internal signalling -  interrupt the multiplexer wait */
#if defined(HAVE_PIPE2) && defined(O_CLOEXEC) && defined(O_NONBLOCK)
	if (pipe2(mux->ctrlp, O_CLOEXEC | O_NONBLOCK) <= -1)
	{
		mux->ctrlp[0] = MIO_SYSHND_INVALID;
		mux->ctrlp[1] = MIO_SYSHND_INVALID;
	}
#else
	if (pipe(mux->ctrlp) <= -1)
	{
		mux->ctrlp[0] = MIO_SYSHND_INVALID;
		mux->ctrlp[1] = MIO_SYSHND_INVALID;
	}
	else
	{
		mio_makesyshndasync(mio, mux->ctrlp[0]);
		mio_makesyshndcloexec(mio, mux->ctrlp[0]);
		mio_makesyshndasync(mio, mux->ctrlp[1]);
		mio_makesyshndcloexec(mio, mux->ctrlp[1]);
	}
#endif



#if defined(USE_EPOLL)

#if defined(HAVE_EPOLL_CREATE1) && defined(EPOLL_CLOEXEC)
	mux->hnd = epoll_create1(EPOLL_CLOEXEC);
	if (mux->hnd == -1)
	{
		if (errno == ENOSYS) goto normal_epoll_create; /* kernel doesn't support it */
		mio_seterrwithsyserr (mio, 0, errno);
		return -1;
	}
	goto epoll_create_done;

normal_epoll_create:
#endif

	mux->hnd = epoll_create(16384); /* TODO: choose proper initial size? */
	if (mux->hnd == -1)
	{
		mio_seterrwithsyserr (mio, 0, errno);
		return -1;
	}
#if defined(FD_CLOEXEC)
	else
	{
		int flags = fcntl(mux->hnd, F_GETFD, 0);
		if (flags >= 0 && !(flags & FD_CLOEXEC)) fcntl(mux->hnd, F_SETFD, flags | FD_CLOEXEC);
	}
#endif /* FD_CLOEXEC */

epoll_create_done:
	if (mux->ctrlp[0] != MIO_SYSHND_INVALID)
	{
		struct epoll_event ev;
		ev.events = EPOLLIN | EPOLLHUP | EPOLLERR;
		ev.data.ptr = MIO_NULL;
		epoll_ctl(mux->hnd, EPOLL_CTL_ADD, mux->ctrlp[0], &ev);
	}
#endif /* USE_EPOLL */

	return 0;
}

void mio_sys_finimux (mio_t* mio)
{
	mio_sys_mux_t* mux = &mio->sysdep->mux;
#if defined(USE_POLL)
	if (mux->map.ptr) 
	{
		mio_freemem (mio, mux->map.ptr);
		mux->map.ptr = MIO_NULL;
		mux->map.capa = 0;
	}

	if (mux->pd.pfd) 
	{
		mio_freemem (mio, mux->pd.pfd);
		mux->pd.pfd = MIO_NULL;
	}
	if (mux->pd.dptr) 
	{
		mio_freemem (mio, mux->pd.dptr);
		mux->pd.dptr = MIO_NULL;
	}
	mux->pd.capa = 0;

#elif defined(USE_EPOLL)
	if (mux->ctrlp[0] != MIO_SYSHND_INVALID)
	{
		struct epoll_event ev;
		ev.events = EPOLLIN | EPOLLHUP | EPOLLERR;
		ev.data.ptr = MIO_NULL;
		epoll_ctl(mux->hnd, EPOLL_CTL_DEL, mux->ctrlp[0], &ev);
	}

	close (mux->hnd);
	mux->hnd = MIO_SYSHND_INVALID;
#endif

	if (mux->ctrlp[1] != MIO_SYSHND_INVALID)
	{
		close (mux->ctrlp[1]); 
		mux->ctrlp[1] = MIO_SYSHND_INVALID;
	}

	if (mux->ctrlp[0] != MIO_SYSHND_INVALID)
	{
		close (mux->ctrlp[0]);
		mux->ctrlp[0] = MIO_SYSHND_INVALID;
	}
}

void mio_sys_intrmux (mio_t* mio)
{
	/* for now, thie only use of the control pipe is to interrupt the multiplexer */
	mio_sys_mux_t* mux = &mio->sysdep->mux;
	if (mux->ctrlp[1] != MIO_SYSHND_INVALID) write (mux->ctrlp[1], "Q", 1);
}

int mio_sys_ctrlmux (mio_t* mio, mio_sys_mux_cmd_t cmd, mio_dev_t* dev, int dev_cap)
{
#if defined(USE_POLL)
	mio_sys_mux_t* mux = &mio->sysdep->mux;
	mio_oow_t idx;
	mio_syshnd_t hnd;

	hnd = dev->dev_mth->getsyshnd(dev);

	if (hnd >= mux->map.capa)
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

	switch (cmd)
	{
		case MIO_SYS_MUX_CMD_INSERT:
			if (idx != MUX_INDEX_INVALID) /* not valid index and not MUX_INDEX_SUSPENDED */
			{
				mio_seterrnum (mio, MIO_EEXIST);
				return -1;
			}

		do_insert:
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
		{
			int events = 0;
			if (dev_cap & MIO_DEV_CAP_IN_WATCHED) events |= POLLIN;
			if (dev_cap & MIO_DEV_CAP_OUT_WATCHED) events |= POLLOUT;

			if (idx == MUX_INDEX_INVALID)
			{
				mio_seterrnum (mio, MIO_ENOENT);
				return -1;
			}
			else if (idx == MUX_INDEX_SUSPENDED) 
			{
				if (!events) return 0; /* no change. keep suspended */
				goto do_insert;
			}

			if (!events)
			{
				mux->map.ptr[hnd] = MUX_INDEX_SUSPENDED;
				goto do_delete;
			}

			MIO_ASSERT (mio, mux->pd.dptr[idx] == dev);
			mux->pd.pfd[idx].events = events;

			return 0;
		}

		case MIO_SYS_MUX_CMD_DELETE:
			if (idx == MUX_INDEX_INVALID)
			{
				mio_seterrnum (mio, MIO_ENOENT);
				return -1;
			}
			else if (idx == MUX_INDEX_SUSPENDED)
			{
				mux->map.ptr[hnd] = MUX_INDEX_INVALID;
				return 0;
			}

			MIO_ASSERT (mio, mux->pd.dptr[idx] == dev);
			mux->map.ptr[hnd] = MUX_INDEX_INVALID;

		do_delete:
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
	struct epoll_event ev;
	mio_syshnd_t hnd;
	mio_uint32_t events;
	int x;

	MIO_ASSERT (mio, mio == dev->mio);
	hnd = dev->dev_mth->getsyshnd(dev);

	events = 0;
	if (dev_cap & MIO_DEV_CAP_IN_WATCHED) 
	{
		events |= EPOLLIN;
	#if defined(EPOLLRDHUP)
		events |= EPOLLRDHUP;
	#endif
		if (dev_cap & MIO_DEV_CAP_PRI_WATCHED) events |= EPOLLPRI;
	}
	if (dev_cap & MIO_DEV_CAP_OUT_WATCHED) events |= EPOLLOUT;

	ev.events = events | EPOLLHUP | EPOLLERR /*| EPOLLET*/; /* TODO: ready to support edge-trigger? */
	ev.data.ptr = dev;

	switch (cmd)
	{
		case MIO_SYS_MUX_CMD_INSERT:
			if (MIO_UNLIKELY(dev->dev_cap & MIO_DEV_CAP_WATCH_SUSPENDED))
			{
				mio_seterrnum (mio, MIO_EEXIST);
				return -1;
			}

			x = epoll_ctl(mux->hnd, EPOLL_CTL_ADD, hnd, &ev);
			break;

		case MIO_SYS_MUX_CMD_UPDATE:
			if (MIO_UNLIKELY(!events))
			{
				if (dev->dev_cap & MIO_DEV_CAP_WATCH_SUSPENDED)
				{
					/* no change. keep suspended */
					return 0;
				}
				else
				{
					x = epoll_ctl(mux->hnd, EPOLL_CTL_DEL, hnd, &ev);
					if (x >= 0) dev->dev_cap |= MIO_DEV_CAP_WATCH_SUSPENDED;
				}
			}
			else
			{
				if (dev->dev_cap & MIO_DEV_CAP_WATCH_SUSPENDED)
				{
					x = epoll_ctl(mux->hnd, EPOLL_CTL_ADD, hnd, &ev);
					if (x >= 0) dev->dev_cap &= ~MIO_DEV_CAP_WATCH_SUSPENDED;
				}
				else
				{
					x = epoll_ctl(mux->hnd, EPOLL_CTL_MOD, hnd, &ev);
				}
			}
			break;

		case MIO_SYS_MUX_CMD_DELETE:
			if (dev->dev_cap & MIO_DEV_CAP_WATCH_SUSPENDED) 
			{
				/* clear the SUSPENDED bit because it's a normal deletion */
				dev->dev_cap &= ~MIO_DEV_CAP_WATCH_SUSPENDED;
				return 0;
			}

			x = epoll_ctl(mux->hnd, EPOLL_CTL_DEL, hnd, &ev);
			break;

		default:
			mio_seterrnum (mio, MIO_EINVAL);
			return -1;
	}

	if (x == -1)
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
	mio_sys_mux_t* mux = &mio->sysdep->mux;
	int nentries, i;

	nentries = poll(mux->pd.pfd, mux->pd.size, MIO_SECNSEC_TO_MSEC(tmout->sec, tmout->nsec));
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

			/*MIO_ASSERT (mio, !(mux->pd.pfd[i].revents & POLLNVAL));*/
			if (mux->pd.pfd[i].revents & POLLIN) events |= MIO_DEV_EVENT_IN;
			if (mux->pd.pfd[i].revents & POLLOUT) events |= MIO_DEV_EVENT_OUT;
			if (mux->pd.pfd[i].revents & POLLPRI) events |= MIO_DEV_EVENT_PRI;
			if (mux->pd.pfd[i].revents & POLLERR) events |= MIO_DEV_EVENT_ERR;
			if (mux->pd.pfd[i].revents & POLLHUP) events |= MIO_DEV_EVENT_HUP;

			event_handler (mio, dev, events, 0);
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
		if (MIO_LIKELY(dev))
		{
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
		else if (mux->ctrlp[0] != MIO_SYSHND_INVALID)
		{
			/* internal pipe for signaling */
			mio_uint8_t tmp[16];
			while (read(mux->ctrlp[0], tmp, MIO_SIZEOF(tmp)) > 0) ;
		}
	}
#else

#	error NO SUPPORTED MULTIPLEXER
#endif

	return 0;
}
