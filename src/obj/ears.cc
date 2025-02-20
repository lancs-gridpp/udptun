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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netdb.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <system_error>

#include <functional>

#include "ears.hh"
#include "formatting.hh"
#include "destruction.hh"
#include "labels.hh"
#include "exits.hh"

void TCPEar::Listener::handle_fd(uint32_t)
{
  /* A connection has been requested.  Accept it to get the file
     descriptor, and make a Connection object out of it. */
  do {
    struct sockaddr addr;
    socklen_t addrlen = sizeof addr;
    int clsock = accept(sock, &addr, &addrlen);
    if (clsock < 0) {
      switch (errno) {
      case ENETDOWN:
      case EPROTO:
      case ENOPROTOOPT:
      case EHOSTDOWN:
      case ENONET:
      case EHOSTUNREACH:
      case EOPNOTSUPP:
      case ENETUNREACH:
      case EAGAIN:
	continue;

      default:
	close(sock);
	sock = -1;
	// TODO: Maybe throw something, or at least log.
	return;
      }
    }

    /* A connection was established.  Make sure we use it. */
    parent.conns.emplace_back(parent, clsock);

    /* Get ready to accept another connection. */
    fdev.set(sock, EPOLLIN);
  } while (true);
}

TCPEar::Listener::Listener(TCPEar &parent, int sock)
  : parent(parent), sock(sock),
    fdev(parent.sched,
	 std::bind(&Listener::handle_fd, this, std::placeholders::_1)) { }

TCPEar::Listener::~Listener()
{
  /* Cancel any outstanding expectation before closing the socket. */
  fdev.cancel();
  if (sock >= 0)
    close(sock);
}

TCPEar::Connection::Connection(TCPEar &parent, int sock)
  : parent(parent), sock(sock), len(0),
    fdev(parent.sched,
	 std::bind(&Connection::handle_fd, this, std::placeholders::_1))
{
  /* Get ready to receive immediately. */
  fdev.set(sock, EPOLLIN);
}

TCPEar::Connection::~Connection()
{
  /* Cancel any outstanding expectation before closing the socket. */
  fdev.cancel();
  if (sock >= 0)
    close(sock);
}

bool TCPEar::Connection::process()
{
  /* We must have enough bytes for the header. */
  if (len < 6) return false;

  /* If we have the header, we know how many additional bytes form the
     payload. */
  unsigned expected = (buf[4] << 8) | buf[5];
  if (len < 6 + expected) return false;

  /* Deliver the payload to any indicated exit. */
  for (unsigned lbl = 0; lbl < 32; lbl++) {
    /* Is the label present in the set? */
    if ((buf[lbl / 8] & (1ul << (lbl % 8))) == 0)
      continue;

    /* Is an exit defined for this label? */
    auto pos = parent.exits.find(lbl);
    if (pos == parent.exits.end())
      continue;

    pos->second->deliver(buf + 5, expected - 6);
  }

  /* Consume the header and payload. */
  auto amount = 6 + expected;
  memmove(buf, buf + amount, len - amount);
  len -= amount;
  return true;
}

void TCPEar::Connection::handle_fd(uint32_t)
{
  ssize_t rc = recv(sock, buf + len, sizeof buf - len, 0);
  if (rc <= 0) {
    /* The client has closed the connection, or it has timed out. */
    // TODO: Log the error if rc < 0.
    fdev.cancel();
    close(sock);
    sock = -1;

    /* Make sure this entry gets cleaned out. */
    parent.idev.set();
  } else {
    /* Be ready to receive more. */
    fdev.set(sock, EPOLLIN);
  }

  /* Deliver any complete payloads to the exits, and clear the data
     out. */
  while (process())
    ;
}

TCPEar::TCPEar(Scheduler &sched, const YAML::Node &cfg)
  : sched(sched),
    idev(sched, std::bind(&TCPEar::flush, this)),
    ipv4(cfg["ipv4"].as<bool>("true")),
    ipv6(cfg["ipv6"].as<bool>("true")),
    host(cfg["host"].as<std::string>("localhost")),
    srv(cfg["port"].as<std::string>()) { }

void TCPEar::flush()
{
  /* Go through all connections, deleting those which are closed. */
  conns.remove_if([](Connection &c) { return c.sock < 0; });
}

void TCPEar::channel(unsigned label, std::shared_ptr<Exit> dst)
{
  exits[label] = dst;
}

void TCPEar::activate()
{
  /* Restrict what we're looking for. */
  struct addrinfo hints;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = ipv4 ? ipv6 ? AF_UNSPEC : AF_INET : AF_INET6;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = 0;

  /* Resolve the host. */
  struct addrinfo *info = nullptr;
  LegacyDestructor infoDestr([&info]() { if (info) freeaddrinfo(info); });
  int rc = getaddrinfo(host.c_str(),
		       srv.empty() ? nullptr : srv.c_str(),
		       &hints, &info);
  if (rc < 0)
    throw std::system_error(errno, std::system_category(),
			    sformat("getaddrinfo(%s)", host.c_str()));

  for (auto iter = info; iter; iter = iter->ai_next) {
    int sock = socket(iter->ai_family, SOCK_STREAM, iter->ai_protocol);
    if (sock < 0)
      throw std::system_error(errno, std::system_category(),
			      sformat("socket(%d, SOCK_STREAM, %d)",
				      iter->ai_family,
				      iter->ai_protocol));

    if (bind(sock, iter->ai_addr, iter->ai_addrlen) != 0) {
      int ec = errno;
      close(sock);
      throw std::system_error(ec, std::system_category(), "bind");
    }

    if (listen(sock, 5) != 0) {
      int ec = errno;
      close(sock);
      throw std::system_error(ec, std::system_category(), "listen");
    }

    conns.emplace_back(*this, sock);
  }
}

Ear *make_ear(Scheduler &sched,
              const std::map<std::string, std::shared_ptr<Exit>> &refs,
              const YAML::Node &cfg)
{
  Ear *result = nullptr;
  if (cfg["tcp"]) {
    result = new TCPEar(sched, cfg["tcp"]);
  }
  if (result) {
    if (cfg["channel"]) {
      auto end = cfg["channel"].end();
      for (auto iter = cfg["channel"].begin(); iter != end; iter++) {
	auto label = (*iter)["label"].as<unsigned>();
	auto name = (*iter)["exit"].as<std::string>();
	auto pos = refs.find(name);
	if (pos == refs.end()) {
	  // TODO: Throw something.
	}
	result->channel(label, pos->second);
      }
    }
  }
  return result;
}
