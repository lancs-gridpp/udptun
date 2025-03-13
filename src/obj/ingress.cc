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
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>

#include <cassert>
#include <cstring>

#include <functional>
#include <system_error>

#include "ingress.hh"

TCPIngress::TCPIngress(Scheduler &sched,
                       AddressManager &addrmgr,
                       const std::string &host,
                       const std::string &srv)
  : host(host), srv(srv), sock(-1), connected(false),
    fdev(sched, [this](uint32_t evs) { descriptor_event(evs); }),
    rstev(sched, [this]() { restart_event(); }),
    addrev(addrmgr, [this](const struct addrinfo *p) { address_resolved(p); })
{ }

TCPIngress::~TCPIngress()
{
  rstev.cancel();
  fdev.cancel();
  if (sock >= 0)
    ::close(sock);
}

void TCPIngress::descriptor_event(uint32_t)
{
  /* The socket has become writable.  Is the connection operation just
     completing? */
  if (!connected) {
    int soerr;
    socklen_t soerrlen = sizeof soerr;
    int rc = getsockopt(sock, SOL_SOCKET, SO_ERROR, &soerr, &soerrlen);
    if (rc != 0)
      throw std::system_error(errno, std::system_category(),
                              "getsockopt(SOL_SOCKET, SO_ERROR)");
    if (soerr != 0) {
      // TODO: Log error.
      ainf = ainf->ai_next;
      try_connect();
      return;
    }

    /* Record that the connection is complete, and wait for another
       write event. */
    connected = true;
  }

  /* Try writing a packet. */
  // TODO
}

void TCPIngress::clear_socket()
{
  /* Close the socket, but first clear any expectation of an event, so
     we don't get any nasty errors from epoll.  Mark the socket as
     invalid, so we don't close it again (as it might belong to
     someone else by then). */
  fdev.cancel();
  ::close(sock), sock = -1;
}

void TCPIngress::restart_event()
{
  /* Try restarting.  Clear out any existing socket. */
  if (sock >= 0) clear_socket();

  /* Resolve the node and service. */
  ainf = nullptr;
  struct addrinfo hint;
  memset(&hint, 0, sizeof hint);
  hint.ai_family = AF_UNSPEC;
  hint.ai_socktype = SOCK_STREAM;
  hint.ai_flags = 0;
  hint.ai_protocol = 0;
  addrev.initiate(host, srv, &hint);
}

void TCPIngress::address_resolved(const struct addrinfo *p)
{
  /* Record the initial address to try. */
  ainf = p;
  connected = false;
  try_connect();
}

void TCPIngress::try_connect()
{
  do {
    /* Create the socket with the right parameters. */
    while (ainf) {
      sock = socket(ainf->ai_family, SOCK_STREAM, ainf->ai_protocol);
      if (sock >= 0)
        break;
      ainf = ainf->ai_next;
    }

    if (sock < 0) {
      /* We failed to open a socket.  Try again in a bit. */
      rstev.set(TimePeriod(30, TimePeriod::SECOND));
      return;
    }

    /* Make the socket non-blocking. */
    {
      int flags = fcntl(sock, F_GETFL, 0);
      if (flags < 0)
        throw std::system_error(errno, std::system_category(), "fcntl(GETFL)");
      flags |= O_NONBLOCK;
      int rc = fcntl(sock, F_SETFL, flags);
      if (rc < 0)
        throw std::system_error(errno, std::system_category(), "fcntl(GETFL)");
    }

    /* Perform a non-blocking connect. */
    int rc = connect(sock, ainf->ai_addr, ainf->ai_addrlen);
    if (rc < 0) {
      switch (errno) {
      default:
        // TODO: Log error.
        /* Close the socket, and try the next address entry
           immediately. */
        clear_socket();
        ainf = ainf->ai_next;
        continue;

      case EINPROGRESS:
      case EAGAIN:
        /* We have initiated a non-blocking connect.  Get notified when
           the connection can be resolved. */
        fdev.set(sock, EPOLLOUT);
        return;
      }
    }

    /* We're immediately connected, so record that, and check when we
       can actually write. */
    connected = true;
    fdev.set(sock, EPOLLOUT);
    return;
  } while (true);
}

void TCPIngress::submit(labelset_t, const void *data, std::size_t len)
{
  // TODO
}
