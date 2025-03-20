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

#include <netdb.h>

#include <cassert>
#include <csignal>
#include <cstring>

#include <system_error>
#include <functional>

#include "addressing.hh"
#include "formatting.hh"
#include "sigevent.hh"
#include "addrevent.hh"

void AddressManager::check()
{
  /* We must go through all users to see if any requests have been
     fulfilled.  If they have, they will schedule their idle events,
     so the users will be informed on the next event iteration. */
  for (auto user : users)
    user->check();

  /* Always be ready for a signal. */
  sigev.set(signo);
}

AddressEvent::AddressEvent(AddressManager &mgr, user_t user)
  : mgr(mgr), user(user), on(false)
{
  data.ar_result = nullptr;
}

AddressEvent::~AddressEvent()
{
  /* Make sure we're not getting any more results. */
  if (on) gai_cancel(&data);

  /* Clear the results we've got. */
  if (data.ar_result) freeaddrinfo(data.ar_result);

  /* Stop the manager from notifying us. */
  mgr.users.erase(this);
}

void AddressEvent::initiate(const std::string &node,
			    const std::string &srv,
			    const struct addrinfo *hint)
{
  /* Make sure there is no previous request running. */
  cancel();

  /* Prepare a fresh request.  We'll defensively keep the strings
     around in case they're used to identify the request (not just the
     structure address).  We'll keep a copy of the hint too, and point
     at the copy, if provided.  The result pointer doesn't need to be
     null for the call, but we need it to be null to know whether
     there's anything to free. */
  memset(&data, 0, sizeof data);
  data.ar_name = (this->node = node).c_str();
  data.ar_service = (this->srv = srv).c_str();
  if (hint) {
    this->hint = *hint;
    data.ar_request = &this->hint;
  } else {
    data.ar_request = nullptr;
  }
  assert(data.ar_result == nullptr);

  /* We need a list of one pointer to our request. */
  struct gaicb *lst[] = { &data };

  /* We need to specify how we'll know of the result.  We'll use the
     specified signal. */
  struct sigevent se;
  memset(&se, 0, sizeof se);
  se.sigev_notify = SIGEV_SIGNAL;
  se.sigev_signo = mgr.signo;
  se.sigev_value.sival_ptr = this; // not really used

  int rc = getaddrinfo_a(GAI_NOWAIT, lst, sizeof lst / sizeof lst[0], &se);
  switch (rc) {
  case 0:
    on = true;
    mgr.users.insert(this);
    /* Now we await a call to check(), which has to be triggered by a
       signal. */
    break;

  case EAI_AGAIN:
    throw std::system_error(EAGAIN, std::system_category(), "getaddrinfo_a");

  case EAI_MEMORY:
    throw std::system_error(ENOMEM, std::system_category(), "getaddrinfo_a");

  case EAI_SYSTEM:
    throw std::system_error(ENOSYS, std::system_category(), "getaddrinfo_a");

  default:
    throw std::runtime_error(sformat("unreachable %s:%d", __FILE__, __LINE__));
  }
}

void AddressEvent::cancel()
{
  while (on) {
    int rc = gai_cancel(&data);
    switch (rc) {
    case EAI_CANCELED:
    case EAI_ALLDONE:
      on = false;
      break;

    case EAI_NOTCANCELED:
      /* We'll just have to go round in a tight loop!  If the request
	 is currently being processed (as this code implies?), it
	 shouldn't be long anyway.  TODO: Any better strategy? */
      break;
    }
  }

  if (data.ar_result)
    freeaddrinfo(data.ar_result), data.ar_result = nullptr;
}

void AddressEvent::check()
{
  if (!on) return;
  struct gaicb *lst[] = { &data };

  do {
    struct timespec to;
    to.tv_sec = 0, to.tv_nsec = 0;
    int src = gai_suspend(lst, sizeof lst / sizeof lst[0], &to);
    switch (src) {
    case EAI_INTR:
      /* We were interrupted, so try again.  Perhaps we were
	 interrupted because we are closing down, but we should be
	 able to quickly clear things up anyway. */
      continue;

    case EAI_AGAIN:
    case EAI_ALLDONE:
      break;
    }
  } while (true);

  int rc = gai_error(&data);
  switch (rc) {
  case 0:
    on = false;
    user(data.ar_result);
    break;

  case EAI_ALLDONE:
    return;
  }
}

AddressManager::AddressManager(SignalManager &sigmgr,
			       int signo)
  : signo(signo), sigev(sigmgr, std::bind(&AddressManager::check, this))
{
  sigev.set(signo);
}

AddressManager::~AddressManager()
{
  /* Create a local copy of our user set, and cancel all members. */
  std::set<AddressEvent *> tmp(users);
  for (auto user : tmp)
    user->cancel();
}
