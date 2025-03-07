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
#include <sys/signalfd.h>
#include <sys/epoll.h>

#include <cassert>
#include <csignal>
#include <cstring>

#include <system_error>
#include <functional>

#include "signaling.hh"
#include "sigevent.hh"
#include "formatting.hh"

SignalManager::SignalManager(Scheduler &sched)
  : fdev(sched,
         std::bind(&SignalManager::on_signal, this, std::placeholders::_1)),
    fd(-1)
{
  /* Create an empty set of signals to watch.  We'll modify as users
     are set and cancelled. */
  if (sigemptyset(&watching) != 0)
    throw std::system_error(errno, std::system_category(),
			    "sigemptyset(SignalManager)");

  /* Create the descriptor that will inform us of signals. */
  fd = signalfd(-1, &watching, SFD_NONBLOCK);
  if (fd < 0)
    throw std::system_error(errno, std::system_category(),
                            "signalfd(SignalManager)");
}

SignalManager::~SignalManager()
{
  /* Under proper use, the user list should be empty by the time we're
     destroyed, but to help avoid any mishaps, we'll cancel any
     callbacks on the users.  This might be counter-productive. */
  for (auto &ent : users)
    for (auto user : ent.second)
      user->cancel();

  /* Now stop receiving signals. */
  if (fd >= 0) ::close(fd);
}

void SignalManager::on_signal(uint32_t events)
{
  /* We should be able to read at least one signal from the
     descriptor. */
  assert(fd >= 0);
  assert(events & EPOLLIN);
  struct signalfd_siginfo buf;
  std::set<SignalEvent *> fired;
  do {
    ssize_t rc = read(fd, &buf, sizeof buf);
    if (rc < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        break;
      throw std::system_error(errno, std::system_category(), "signalfd/read");
    }
    int signo = buf.ssi_signo;

    /* Collect the users, and remove them from our permanent tables. */
    auto &ent = users[signo];
    for (auto user : ent) {
      fired.insert(user);
      user->signo = 0;
    }
    users.erase(signo);

    /* Make sure we're not watching for this signal again. */
    update(signo);
  } while (true);

  /* Now invoke all the users. */
  for (auto user : fired)
    user->user();
}

void SignalManager::update(int signo)
{
  /* Do we want to start watching this signal, or stop? */
  auto pos = users.find(signo);
  bool state = pos != users.end() && !pos->second.empty();
  if (!state) users.erase(signo);
    
  /* Make no changes if we're already doing the right thing about the
     signal. */
  if (sigismember(&watching, signo) == state) return;

  /* Modify a temporary copy of the set to reflect the new state. */
  sigset_t tmp = watching;
  if ((state ? sigaddset(&tmp, signo) : sigdelset(&tmp, signo)) < 0)
    throw std::system_error(errno, std::system_category(),
			    sformat("sig%sset(%s)",
                                    state ? "add" : "del",
                                    strsignal(signo)));

  /* Apply the changes. */
  int rc = signalfd(fd, &tmp, 0);
  if (rc < 0)
    throw std::system_error(errno, std::system_category(),
                            sformat("signalfd(%s%s)", state ? "+" : "-",
                                    strsignal(signo)));

  /* Commit the changes. */
  watching = tmp;

  if (state) {
    /* Make sure we get the event. */
    fdev.set(fd, EPOLLIN);
  } else if (users.empty()) {
    /* We're not watching anything any more. */
    fdev.cancel();
  }
}

void SignalEvent::reset()
{
  /* Tidy up our presence in the manager.  Don't bother clearing our
     signal number, as the caller will do it, or does not care (e.g.,
     the destructor). */
  assert(signo != 0);
  mgr.users[signo].erase(this);
  mgr.update(signo);
}

void SignalEvent::cancel()
{
  if (signo == 0) return;
  reset();
  signo = 0;
}

SignalEvent::SignalEvent(SignalManager &mgr, user_t user)
  : mgr(mgr), signo(0), user(user) { }

void SignalEvent::set(int signo)
{
  /* We will treat set(0) as a cancel(). */
  if (signo == 0) cancel();

  /* Do nothing if we're already set for this signal. */
  if (this->signo == signo) return;

  /* Clear any previous signal. */
  if (this->signo != 0) reset();

  /* Put us in the right place in the manager's table. */
  mgr.users[signo].insert(this);
  this->signo = signo;

  /* Make sure the manager is watching this signal. */
  mgr.update(signo);
}

SignalEvent::~SignalEvent()
{
  if (signo != 0) reset();
}
