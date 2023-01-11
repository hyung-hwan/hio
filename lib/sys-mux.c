/*
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
    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
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
#	define MUX_INDEX_INVALID HIO_TYPE_MAX(hio_oow_t)
#	define MUX_INDEX_SUSPENDED (MUX_INDEX_INVALID - 1)

static int secure_poll_map_slot_for_hnd (hio_t* hio, hio_syshnd_t hnd);
static int secure_poll_data_slot_for_insert (hio_t* hio);
#endif

int hio_sys_initmux (hio_t* hio)
{
	hio_sys_mux_t* mux = &hio->sysdep->mux;

	/* create a pipe for internal signalling -  interrupt the multiplexer wait */
#if defined(HAVE_PIPE2) && defined(O_CLOEXEC) && defined(O_NONBLOCK)
	if (pipe2(mux->ctrlp, O_CLOEXEC | O_NONBLOCK) <= -1)
	{
		mux->ctrlp[0] = HIO_SYSHND_INVALID;
		mux->ctrlp[1] = HIO_SYSHND_INVALID;
	}
#else
	if (pipe(mux->ctrlp) <= -1)
	{
		mux->ctrlp[0] = HIO_SYSHND_INVALID;
		mux->ctrlp[1] = HIO_SYSHND_INVALID;
	}
	else
	{
		hio_makesyshndasync(hio, mux->ctrlp[0]);
		hio_makesyshndcloexec(hio, mux->ctrlp[0]);
		hio_makesyshndasync(hio, mux->ctrlp[1]);
		hio_makesyshndcloexec(hio, mux->ctrlp[1]);
	}
#endif


#if defined(USE_POLL)
	if (secure_poll_map_slot_for_hnd(hio, mux->ctrlp[0]) <= -1 ||
	    secure_poll_data_slot_for_insert(hio) <= -1)
	{
		/* no control pipes if registration fails */
		close (mux->ctrlp[1]);
		mux->ctrlp[1] = HIO_SYSHND_INVALID;
		close (mux->ctrlp[0]);
		mux->ctrlp[0] = HIO_SYSHND_INVALID;
	}
	else
	{
		hio_oow_t idx;
		idx = mux->pd.size++;
		mux->pd.pfd[idx].fd = mux->ctrlp[0];
		mux->pd.pfd[idx].events = POLLIN;
		mux->pd.pfd[idx].revents = 0;
		mux->pd.dptr[idx] = HIO_NULL;
		mux->map.ptr[mux->ctrlp[0]] = idx;
	}

#elif defined(USE_KQUEUE)
	#if defined(HAVE_KQUEUE1) && defined(O_CLOEXEC)
	mux->kq = kqueue1(O_CLOEXEC);
	if (mux->kq <= -1)
	{
		hio_seterrwithsyserr (hio, 0, errno);
		return -1;
	}
	#else
	mux->kq = kqueue();
	if (mux->kq <= -1)
	{
		hio_seterrwithsyserr (hio, 0, errno);
		return -1;
	}
	else
	{
		#if defined(FD_CLOEXEC)
		int flags = fcntl(mux->kq, F_GETFD);
		if (flags >= 0) fcntl (mux->kq, F_SETFD, flags | FD_CLOEXEC);
		#endif
	}
	#endif

	/* register the control pipe */
	{
		struct kevent chlist;
		EV_SET(&chlist, mux->ctrlp[0], EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, 0);
		kevent(mux->kq, &chlist, 1, HIO_NULL, 0, HIO_NULL);
	}

#elif defined(USE_EPOLL)

#if defined(HAVE_EPOLL_CREATE1) && defined(EPOLL_CLOEXEC)
	mux->hnd = epoll_create1(EPOLL_CLOEXEC);
	if (mux->hnd == -1)
	{
		if (errno == ENOSYS) goto normal_epoll_create; /* kernel doesn't support it */
		hio_seterrwithsyserr (hio, 0, errno);
		return -1;
	}
	goto epoll_create_done;

normal_epoll_create:
#endif

	mux->hnd = epoll_create(16384); /* TODO: choose proper initial size? */
	if (mux->hnd == -1)
	{
		hio_seterrwithsyserr (hio, 0, errno);
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
	if (mux->ctrlp[0] != HIO_SYSHND_INVALID)
	{
		struct epoll_event ev;
		ev.events = EPOLLIN | EPOLLHUP | EPOLLERR;
		ev.data.ptr = HIO_NULL;
		if (epoll_ctl(mux->hnd, EPOLL_CTL_ADD, mux->ctrlp[0], &ev) == -1)
		{
			/* if ADD fails, close the control pipes and forget them */
			close (mux->ctrlp[1]);
			mux->ctrlp[1] = HIO_SYSHND_INVALID;
			close (mux->ctrlp[0]);
			mux->ctrlp[0] = HIO_SYSHND_INVALID;
		}
	}
#endif /* USE_EPOLL */

	return 0;
}

void hio_sys_finimux (hio_t* hio)
{
	hio_sys_mux_t* mux = &hio->sysdep->mux;
#if defined(USE_POLL)
	if (mux->map.ptr)
	{
		hio_freemem (hio, mux->map.ptr);
		mux->map.ptr = HIO_NULL;
		mux->map.capa = 0;
	}

	if (mux->pd.pfd)
	{
		hio_freemem (hio, mux->pd.pfd);
		mux->pd.pfd = HIO_NULL;
	}
	if (mux->pd.dptr)
	{
		hio_freemem (hio, mux->pd.dptr);
		mux->pd.dptr = HIO_NULL;
	}
	mux->pd.capa = 0;

#elif defined(USE_KQUEUE)
	if (mux->ctrlp[0] != HIO_SYSHND_INVALID)
	{
		struct kevent chlist;
		EV_SET(&chlist, mux->ctrlp[0], EVFILT_READ, EV_DELETE | EV_DISABLE, 0, 0, 0);
		kevent(mux->kq, &chlist, 1, HIO_NULL, 0, HIO_NULL);
	}

	close (mux->kq);
	mux->kq = HIO_SYSHND_INVALID;


#elif defined(USE_EPOLL)
	if (mux->ctrlp[0] != HIO_SYSHND_INVALID)
	{
		struct epoll_event ev;
		ev.events = EPOLLIN | EPOLLHUP | EPOLLERR;
		ev.data.ptr = HIO_NULL;
		epoll_ctl(mux->hnd, EPOLL_CTL_DEL, mux->ctrlp[0], &ev);
	}

	close (mux->hnd);
	mux->hnd = HIO_SYSHND_INVALID;
#endif

	if (mux->ctrlp[1] != HIO_SYSHND_INVALID)
	{
		close (mux->ctrlp[1]);
		mux->ctrlp[1] = HIO_SYSHND_INVALID;
	}

	if (mux->ctrlp[0] != HIO_SYSHND_INVALID)
	{
		close (mux->ctrlp[0]);
		mux->ctrlp[0] = HIO_SYSHND_INVALID;
	}
}

void hio_sys_intrmux (hio_t* hio)
{
	/* for now, thie only use of the control pipe is to interrupt the multiplexer */
	hio_sys_mux_t* mux = &hio->sysdep->mux;
	if (mux->ctrlp[1] != HIO_SYSHND_INVALID) write (mux->ctrlp[1], "Q", 1);
}

#if defined(USE_POLL)
static int secure_poll_map_slot_for_hnd (hio_t* hio, hio_syshnd_t hnd)
{
	hio_sys_mux_t* mux = &hio->sysdep->mux;

	if (hnd >= mux->map.capa)
	{
		hio_oow_t new_capa;
		hio_oow_t* tmp;
		hio_oow_t idx;

		new_capa = HIO_ALIGN_POW2((hnd + 1), 256);

		tmp = hio_reallocmem(hio, mux->map.ptr, new_capa * HIO_SIZEOF(*tmp));
		if (HIO_UNLIKELY(!tmp)) return -1;

		for (idx = mux->map.capa; idx < new_capa; idx++) tmp[idx] = MUX_INDEX_INVALID;

		mux->map.ptr = tmp;
		mux->map.capa = new_capa;
	}

	return 0;
}

static int secure_poll_data_slot_for_insert (hio_t* hio)
{
	hio_sys_mux_t* mux = &hio->sysdep->mux;

	if (mux->pd.size >= mux->pd.capa)
	{
		hio_oow_t new_capa;
		struct pollfd* tmp1;
		hio_dev_t** tmp2;

		new_capa = HIO_ALIGN_POW2(mux->pd.size + 1, 256);

		tmp1 = hio_reallocmem(hio, mux->pd.pfd, new_capa * HIO_SIZEOF(*tmp1));
		if (HIO_UNLIKELY(!tmp1)) return -1;

		tmp2 = hio_reallocmem(hio, mux->pd.dptr, new_capa * HIO_SIZEOF(*tmp2));
		if (HIO_UNLIKELY(!tmp2))
		{
			hio_freemem (hio, tmp1);
			return -1;
		}

		mux->pd.pfd = tmp1;
		mux->pd.dptr = tmp2;
		mux->pd.capa = new_capa;
	}

	return 0;
}
#endif

int hio_sys_ctrlmux (hio_t* hio, hio_sys_mux_cmd_t cmd, hio_dev_t* dev, int dev_cap)
{
#if defined(USE_POLL)
	hio_sys_mux_t* mux = &hio->sysdep->mux;
	hio_oow_t idx;
	hio_syshnd_t hnd;

	hnd = dev->dev_mth->getsyshnd(dev);

	if (cmd == HIO_SYS_MUX_CMD_INSERT)
	{
		if (secure_poll_map_slot_for_hnd(hio, hnd) <=  -1) return -1;
	}
	else
	{
		if (hnd >= mux->map.capa)
		{
			hio_seterrnum (hio, HIO_ENOENT);
			return -1;
		}
	}
	idx = mux->map.ptr[hnd];

	switch (cmd)
	{
		case HIO_SYS_MUX_CMD_INSERT:
			if (idx != MUX_INDEX_INVALID) /* not valid index and not MUX_INDEX_SUSPENDED */
			{
				hio_seterrnum (hio, HIO_EEXIST);
				return -1;
			}

		do_insert:
			if (HIO_UNLIKELY(secure_poll_data_slot_for_insert(hio) <=  -1)) return -1;
			idx = mux->pd.size++;

			mux->pd.pfd[idx].fd = hnd;
			mux->pd.pfd[idx].events = 0;
			if (dev_cap & HIO_DEV_CAP_IN_WATCHED) mux->pd.pfd[idx].events |= POLLIN;
			if (dev_cap & HIO_DEV_CAP_OUT_WATCHED) mux->pd.pfd[idx].events |= POLLOUT;
			mux->pd.pfd[idx].revents = 0;
			mux->pd.dptr[idx] = dev;

			mux->map.ptr[hnd] = idx;

			return 0;

		case HIO_SYS_MUX_CMD_UPDATE:
		{
			int events = 0;
			if (dev_cap & HIO_DEV_CAP_IN_WATCHED) events |= POLLIN;
			if (dev_cap & HIO_DEV_CAP_OUT_WATCHED) events |= POLLOUT;

			if (idx == MUX_INDEX_INVALID)
			{
				hio_seterrnum (hio, HIO_ENOENT);
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

			HIO_ASSERT (hio, mux->pd.dptr[idx] == dev);
			mux->pd.pfd[idx].events = events;

			return 0;
		}

		case HIO_SYS_MUX_CMD_DELETE:
			if (idx == MUX_INDEX_INVALID)
			{
				hio_seterrnum (hio, HIO_ENOENT);
				return -1;
			}
			else if (idx == MUX_INDEX_SUSPENDED)
			{
				mux->map.ptr[hnd] = MUX_INDEX_INVALID;
				return 0;
			}

			HIO_ASSERT (hio, mux->pd.dptr[idx] == dev);
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
			hio_seterrnum (hio, HIO_EINVAL);
			return -1;
	}

#elif defined(USE_KQUEUE)

	hio_sys_mux_t* mux = &hio->sysdep->mux;
	hio_syshnd_t hnd;
	struct kevent chlist[2];
	int x;

	HIO_ASSERT (hio, hio == dev->hio);

	/* no operation over a broken(closed) handle to prevent multiplexer from failing.
	 * close of the handle leads to auto-deletion from the kqueue multiplexer.
	 * the closed handle must not be fed to the multiplexer */
	if (dev->dev_mth->issyshndbroken && dev->dev_mth->issyshndbroken(dev)) return 0;
	hnd = dev->dev_mth->getsyshnd(dev);

	switch (cmd)
	{
		case HIO_SYS_MUX_CMD_INSERT:
		{
			int i_flag, o_flag;
			if (HIO_UNLIKELY(dev->dev_cap & HIO_DEV_CAP_WATCH_SUSPENDED))
			{
				hio_seterrnum (hio, HIO_EEXIST);
				return -1;
			}

			i_flag = (dev_cap & HIO_DEV_CAP_IN_WATCHED)? EV_ENABLE: EV_DISABLE;
			o_flag = (dev_cap & HIO_DEV_CAP_OUT_WATCHED)? EV_ENABLE: EV_DISABLE;

			EV_SET (&chlist[1], hnd, EVFILT_READ, EV_ADD | i_flag, 0, 0, dev);
			EV_SET (&chlist[0], hnd, EVFILT_WRITE, EV_ADD | o_flag, 0, 0, dev);

			x = kevent(mux->kq, chlist, 2, HIO_NULL, 0, HIO_NULL);
			if (x >= 0) dev->dev_cap |= HIO_DEV_CAP_WATCH_REREG_REQUIRED; /* ugly hack for the listening sockets in NetBSD */

			/* the CMD_INSERT comes with at MIO_DEV_CAP_IN_WATCHD set.
			 * skip checking to set WATCH_SUSPENDED */

			break;
		}

		case HIO_SYS_MUX_CMD_UPDATE:
		{
			int i_flag, o_flag;
			i_flag = (dev_cap & HIO_DEV_CAP_IN_WATCHED)? EV_ENABLE: EV_DISABLE;
			o_flag = (dev_cap & HIO_DEV_CAP_OUT_WATCHED)? EV_ENABLE: EV_DISABLE;

			EV_SET (&chlist[0], hnd, EVFILT_READ, EV_ADD | i_flag, 0, 0, dev);
			EV_SET (&chlist[1], hnd, EVFILT_WRITE, EV_ADD | o_flag, 0, 0, dev);

			x = kevent(mux->kq, chlist, 2, HIO_NULL, 0, HIO_NULL);
			if (x >= 0)
			{
				if (i_flag == EV_DISABLE && o_flag == EV_DISABLE)
					dev->dev_cap &= ~HIO_DEV_CAP_WATCH_SUSPENDED;
				else
					dev->dev_cap |= HIO_DEV_CAP_WATCH_SUSPENDED;
			}
			break;
		}

		case HIO_SYS_MUX_CMD_DELETE:
			EV_SET (&chlist[0], hnd, EVFILT_READ, EV_DELETE | EV_DISABLE, 0, 0, dev);
			EV_SET (&chlist[1], hnd, EVFILT_WRITE, EV_DELETE | EV_DISABLE, 0, 0, dev);
			x = kevent(mux->kq, chlist, 2, HIO_NULL, 0, HIO_NULL);
			if (x >= 0) dev->dev_cap &= ~HIO_DEV_CAP_WATCH_SUSPENDED; /* just clear this */
			break;

		default:
			hio_seterrnum (hio, HIO_EINVAL);
			return -1;
	}

	if (x <= -1)
	{
		hio_seterrwithsyserr (hio, 0, errno);
		return -1;
	}

	return 0;

#elif defined(USE_EPOLL)
	hio_sys_mux_t* mux = &hio->sysdep->mux;
	struct epoll_event ev;
	hio_syshnd_t hnd;
	hio_uint32_t events;
	int x;

	HIO_ASSERT (hio, hio == dev->hio);

	/* no operation over a broken(closed) handle to prevent multiplexer from failing.
	 * close() of the handle leads to auto-deletion from the epoll multiplexer.
	 * the closed handle must not be fed to the multiplexer */
	if (dev->dev_mth->issyshndbroken && dev->dev_mth->issyshndbroken(dev)) return 0;
	hnd = dev->dev_mth->getsyshnd(dev);

	events = 0;
	if (dev_cap & HIO_DEV_CAP_IN_WATCHED)
	{
		events |= EPOLLIN;
	#if defined(EPOLLRDHUP)
		events |= EPOLLRDHUP;
	#endif
		if (dev_cap & HIO_DEV_CAP_PRI_WATCHED) events |= EPOLLPRI;
	}
	if (dev_cap & HIO_DEV_CAP_OUT_WATCHED) events |= EPOLLOUT;

	ev.events = events | EPOLLHUP | EPOLLERR /*| EPOLLET*/; /* TODO: ready to support edge-trigger? */
	ev.data.ptr = dev;

	switch (cmd)
	{
		case HIO_SYS_MUX_CMD_INSERT:
			if (HIO_UNLIKELY(dev->dev_cap & HIO_DEV_CAP_WATCH_SUSPENDED))
			{
				hio_seterrnum (hio, HIO_EEXIST);
				return -1;
			}

			x = epoll_ctl(mux->hnd, EPOLL_CTL_ADD, hnd, &ev);
			break;

		case HIO_SYS_MUX_CMD_UPDATE:
			if (HIO_UNLIKELY(!events))
			{
				if (dev->dev_cap & HIO_DEV_CAP_WATCH_SUSPENDED)
				{
					/* no change. keep suspended */
					return 0;
				}
				else
				{
					x = epoll_ctl(mux->hnd, EPOLL_CTL_DEL, hnd, &ev);
					if (x >= 0) dev->dev_cap |= HIO_DEV_CAP_WATCH_SUSPENDED;
				}
			}
			else
			{
				if (dev->dev_cap & HIO_DEV_CAP_WATCH_SUSPENDED)
				{
					x = epoll_ctl(mux->hnd, EPOLL_CTL_ADD, hnd, &ev);
					if (x >= 0) dev->dev_cap &= ~HIO_DEV_CAP_WATCH_SUSPENDED;
				}
				else
				{
					x = epoll_ctl(mux->hnd, EPOLL_CTL_MOD, hnd, &ev);
				}
			}
			break;

		case HIO_SYS_MUX_CMD_DELETE:
			if (dev->dev_cap & HIO_DEV_CAP_WATCH_SUSPENDED)
			{
				/* clear the SUSPENDED bit because it's a normal deletion */
				dev->dev_cap &= ~HIO_DEV_CAP_WATCH_SUSPENDED;
				return 0;
			}

			x = epoll_ctl(mux->hnd, EPOLL_CTL_DEL, hnd, &ev);
			break;

		default:
			hio_seterrnum (hio, HIO_EINVAL);
			return -1;
	}

	if (x == -1)
	{
		hio_seterrwithsyserr (hio, 0, errno);
		return -1;
	}

	return 0;
#else
#	error NO SUPPORTED MULTIPLEXER
#endif
}

int hio_sys_waitmux (hio_t* hio, const hio_ntime_t* tmout, hio_sys_mux_evtcb_t event_handler)
{
#if defined(USE_POLL)
	hio_sys_mux_t* mux = &hio->sysdep->mux;
	int nentries, i;

	nentries = poll(mux->pd.pfd, mux->pd.size, HIO_SECNSEC_TO_MSEC(tmout->sec, tmout->nsec));
	if (nentries == -1)
	{
		if (errno == EINTR) return 0;
		hio_seterrwithsyserr (hio, 0, errno);
		return -1;
	}

	for (i = 0; i < mux->pd.size; i++)
	{
		if (HIO_UNLIKELY(mux->ctrlp[0] != HIO_SYSHND_INVALID && mux->pd.pfd[i].fd == mux->ctrlp[0]))
		{
			/* internal pipe for signaling */

			/* mux->pd.dptr[i] must be HIO_NULL */
			hio_uint8_t tmp[16];
			while (read(mux->ctrlp[0], tmp, HIO_SIZEOF(tmp)) > 0) ;
		}
		else if (mux->pd.pfd[i].fd >= 0 && mux->pd.pfd[i].revents)
		{
			int events = 0;
			hio_dev_t* dev;

			dev = mux->pd.dptr[i];

			/*HIO_ASSERT (hio, !(mux->pd.pfd[i].revents & POLLNVAL));*/
			if (mux->pd.pfd[i].revents & POLLIN) events |= HIO_DEV_EVENT_IN;
			if (mux->pd.pfd[i].revents & POLLOUT) events |= HIO_DEV_EVENT_OUT;
			if (mux->pd.pfd[i].revents & POLLPRI) events |= HIO_DEV_EVENT_PRI;
			if (mux->pd.pfd[i].revents & POLLERR) events |= HIO_DEV_EVENT_ERR;
			if (mux->pd.pfd[i].revents & POLLHUP) events |= HIO_DEV_EVENT_HUP;

			event_handler (hio, dev, events, 0);
		}
	}
#elif defined(USE_KQUEUE)

	hio_sys_mux_t* mux = &hio->sysdep->mux;
	struct timespec ts;
	int nentries, i;

	ts.tv_sec = tmout->sec;
	ts.tv_nsec = tmout->nsec;

	nentries = kevent(mux->kq, HIO_NULL, 0, mux->revs, HIO_COUNTOF(mux->revs), &ts);
	if (nentries <= -1)
	{
		if (errno == EINTR) return 0; /* it's actually ok */
		/* other errors are critical - EBADF, EFAULT, EINVAL */
		hio_seterrwithsyserr (hio, 0, errno);
		return -1;
	}

	for (i = 0; i < nentries; i++)
	{
		int events = 0;
		hio_dev_t* dev;

		dev = mux->revs[i].udata;

		if (HIO_LIKELY(dev))
		{
			HIO_ASSERT (hio, mux->revs[i].ident == dev->dev_mth->getsyshnd(dev));

			if (mux->revs[i].flags & EV_ERROR) events |= HIO_DEV_EVENT_ERR;
			if (mux->revs[i].flags & EV_EOF) events |= HIO_DEV_EVENT_HUP;

			if (mux->revs[i].filter == EVFILT_READ) events |= HIO_DEV_EVENT_IN;
			else if (mux->revs[i].filter == EVFILT_WRITE) events |= HIO_DEV_EVENT_OUT;

			if (HIO_LIKELY(events)) event_handler (hio, dev, events, 0);
		}
		else if (mux->ctrlp[0] != HIO_SYSHND_INVALID)
		{
			/* internal pipe for signaling */
			hio_uint8_t tmp[16];

			HIO_ASSERT (hio, mux->revs[i].ident == mux->ctrlp[0]);
			while (read(mux->ctrlp[0], tmp, HIO_SIZEOF(tmp)) > 0) ;
		}
	}

#elif defined(USE_EPOLL)

	hio_sys_mux_t* mux = &hio->sysdep->mux;
	int nentries, i;

	nentries = epoll_wait(mux->hnd, mux->revs, HIO_COUNTOF(mux->revs), HIO_SECNSEC_TO_MSEC(tmout->sec, tmout->nsec));
	if (nentries == -1)
	{
		if (errno == EINTR) return 0; /* it's actually ok */
		/* other errors are critical - EBADF, EFAULT, EINVAL */
		hio_seterrwithsyserr (hio, 0, errno);
		return -1;
	}

	/* TODO: merge events??? for the same descriptor */

	for (i = 0; i < nentries; i++)
	{
		int events = 0, rdhup = 0;
		hio_dev_t* dev;

		dev = mux->revs[i].data.ptr;
		if (HIO_LIKELY(dev))
		{
			if (mux->revs[i].events & EPOLLIN) events |= HIO_DEV_EVENT_IN;
			if (mux->revs[i].events & EPOLLOUT) events |= HIO_DEV_EVENT_OUT;
			if (mux->revs[i].events & EPOLLPRI) events |= HIO_DEV_EVENT_PRI;
			if (mux->revs[i].events & EPOLLERR) events |= HIO_DEV_EVENT_ERR;
			if (mux->revs[i].events & EPOLLHUP) events |= HIO_DEV_EVENT_HUP;
		#if defined(EPOLLRDHUP)
			else if (mux->revs[i].events & EPOLLRDHUP) rdhup = 1;
		#endif

			event_handler (hio, dev, events, rdhup);
		}
		else if (mux->ctrlp[0] != HIO_SYSHND_INVALID)
		{
			/* internal pipe for signaling */
			hio_uint8_t tmp[16];
			while (read(mux->ctrlp[0], tmp, HIO_SIZEOF(tmp)) > 0) ;
		}
	}
#else

#	error NO SUPPORTED MULTIPLEXER
#endif

	return 0;
}
