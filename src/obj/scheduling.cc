// -*- c-basic-offset: 2; indent-tabs-mode: nil -*-

/*
 * Copyright (c) 2025, Lancaster University
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <unistd.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <strings.h>

#include <cstring>
#include <csignal>
#include <climits>
#include <cassert>

#include <system_error>

#include "events.hh"
#include "descriptor.hh"
#include "timed.hh"
#include "idle.hh"
#include "signaling.hh"
#include "scheduling.hh"
#include "formatting.hh"

void Scheduler::enqueue(Event *mom)
{
  if (mom->prio_appl != 0) return;
  prio_t p = mom->prio();
  queues[p].insert(mom);
  mom->prio_appl = p;
}

void Scheduler::dequeue(Event *mom)
{
  if (mom->prio_appl != 0) {
    queues[mom->prio_appl].erase(mom);
    mom->prio_appl = 0;
  }
}

int Scheduler::timeout()
{
  /* We'll use a zero timeout if we have anything queued. */
  for (auto &qelem : queues) {
    if (qelem.second.empty()) continue;
    return 0;
  }

  /* We'll use a zero timeout if we have idle events. */
  if (!idleness.empty()) return 0;

  /* We'll use an indefinite timeout if we have no timed events. */
  if (table.empty()) return -1;

  /* Eliminate empty leading table elements. */
  for (auto iter = table.begin(); iter != table.end() && iter->second.empty();
       iter = table.begin())
    table.erase(iter);

  /* How long is it in milliseconds until the earliest timed event?
     This will be used as the timeout. */
  auto now = std::chrono::system_clock::now();
  const auto &first = table.begin()->first;
  auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(first - now).count();

  if (delay < 0)
    /* The first timed event is already due, so don't wait for it. */
    return 0;
  else if (delay > INT_MAX)
    /* The first event is too far in the future to represent, so wait
       as log as we're allowed.  Another call to poll() will be
       necessary. */
    return INT_MAX;
  else
    /* Return the actual delay in milliseconds. */
    return delay;
}

void Scheduler::update_signal(int signo)
{
  /* Do we want to start watching this signal, or stop? */
  auto pos = signal_handlers.find(signo);
  bool state = pos != signal_handlers.end() && !pos->second.empty();
  if (!state) signal_handlers.erase(signo);

  /* Make no changes if we're already doing the right thing about the
     signal. */
  if (sigismember(&watching, signo) == state) return;

  /* Modify the state that we watch for.  Use a temporary copy of the
     signal set, and commit if the changes work. */
  auto tmp = watching;
  if ((state ? sigaddset(&tmp, signo) : sigdelset(&tmp, signo)) < 0)
    throw std::system_error(errno, std::system_category(),
			    sformat("sig%sset("
#if defined WITH_SIGNAMES || _POSIX_C_SOURCE >= 200809L
                                    "%s"
#else
                                    "%d"
#endif
                                    ")",
                                    state ? "add" : "del",
#ifdef WITH_SIGNAMES
                                    sigabbrev_np(signo)
#elif _POSIX_C_SOURCE >= 200809L
                                    strsignal(signo)
#else
                                    signo
#endif
                                    ));
  int rc = signalfd(sigfd, &tmp, 0);
  if (rc < 0)
    throw std::system_error(errno, std::system_category(),
                            sformat("signalfd(%s"
#if defined WITH_SIGNAMES || _POSIX_C_SOURCE >= 200809L
                                    "%s"
#else
                                    "%d"
#endif
                                    ")",
                                    state ? "+" : "-",
#ifdef WITH_SIGNAMES
                                    sigabbrev_np(signo)
#elif _POSIX_C_SOURCE >= 200809L
                                    strsignal(signo)
#else
                                    signo
#endif
                                    ));
  watching = tmp;
}

void Scheduler::poll()
{
  struct epoll_event events[20];
  assert(epfd >= 0);

  /* Poll for events, and allow ourselves to be interrupted by
     signals. */
  int nfds = epoll_pwait(epfd, events, sizeof events / sizeof events[0],
                         timeout(), &sigmsk);
  if (nfds < 0) {
    switch (errno) {
    default:
      throw std::system_error(errno, std::system_category(), "epoll_pwait");

    case EINTR:
      return;
    }
  }

  /* Place triggered descriptor events in queues, storing the event
     set in the event object. */
  for (int i = 0; i < nfds; i++) {
    if (events[i].data.ptr == this) {
      /* Deal with a signal on our special descriptor. */
      assert(events[i].events & EPOLLIN);
      for ( ; ; ) {
        struct signalfd_siginfo data;
        ssize_t nb = read(sigfd, &data, sizeof data);
        if (nb < 0) {
          if (errno == EWOULDBLOCK || errno == EAGAIN)
            break;
          throw std::system_error(errno, std::system_category(), "read(sigfd)");
        }
        int signo = data.ssi_signo;

        /* Enqueue each of the handlers for this signal. */
        auto tpos = signal_handlers.find(signo);
        if (tpos == signal_handlers.end()) continue;
        for (auto ptr : tpos->second) {
          ptr->signo = 0;
          enqueue(ptr);
        }

        /* Discard that signal. */
        signal_handlers.erase(tpos);
        update_signal(signo);
      }
      continue;
    }
    auto ptr = static_cast<DescriptorEvent *>(events[i].data.ptr);
    ptr->update(events[i].events);
    enqueue(ptr);
  }

  /* Collect timed events. */
  auto now = std::chrono::system_clock::now();
  for (auto iter = table.begin(); iter != table.upper_bound(now); iter++)
    for (auto miter = iter->second.begin();
         miter != iter->second.end(); miter++) {
      auto ptr = *miter;
      ptr->set_ = false;
      enqueue(ptr);
    }
  table.erase(table.begin(), table.upper_bound(now));

  /* Collect idle events. */
  for (auto ptr : idleness)
    enqueue(ptr);
  idleness.clear();

  /* Notify events of the highest non-empty priority. */
  for (auto &item : queues) {
    auto &queue = item.second;
    if (queue.empty()) continue;

    /* Remove events from this queue and invoke them. */
    for (auto iter = queue.begin(); iter != queue.end();
         iter = queue.begin()) {
      auto ptr = *iter;
      queue.erase(iter);
      ptr->prio_appl = 0;
      ptr->notify();
    }
    break;
  }
}

void Scheduler::signal_mask(const sigset_t &sigmsk)
{
  this->sigmsk = sigmsk;
}

Scheduler::Scheduler() : log("scheduler", "scheduler"), epfd(-1), sigfd(-1)
{
  if (sigemptyset(&sigmsk) != 0)
    throw std::system_error(errno, std::system_category(), "sigemptyset(sigmsk)");

  if (sigemptyset(&watching) != 0)
    throw std::system_error(errno, std::system_category(), "sigemptyset(watching)");

  int epfd = epoll_create1(0);
  if (epfd < 0)
    throw std::system_error(errno, std::system_category(), "epoll_create1");

  int sigfd = signalfd(-1, &watching, SFD_NONBLOCK);
  if (sigfd < 0) {
    int ec = errno;
    ::close(epfd);
    throw std::system_error(ec, std::system_category(), "signalfd");
  }

  /* Make sure we can receive signals. */
  struct epoll_event evdat;
  evdat.events = EPOLLIN;
  evdat.data.ptr = this;
  if (epoll_ctl(epfd, EPOLL_CTL_ADD, sigfd, &evdat) != 0) {
    int ec = errno;
    ::close(epfd);
    ::close(sigfd);
    throw std::system_error(ec, std::system_category(), "epoll_ctl(sigs)");
  }

  this->epfd = epfd;
  this->sigfd = sigfd;
}

Scheduler::~Scheduler()
{
  if (epfd >= 0)
    ::close(epfd);
  if (sigfd >= 0)
    ::close(sigfd);
}
