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
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netdb.h>

#include <cassert>
#include <cstring>

#include "tcp_egress.hh"
#include "destruction.hh"
#include "exits.hh"
#include "network.hh"
#include "messages.hh"

void TCPEgress::Listener::handle_fd(uint32_t)
{
  /* A connection has been requested.  Accept it to get the file
     descriptor, and make a Connection object out of it. */
  do {
    struct sockaddr addr;
    socklen_t addrlen = sizeof addr;
    int clsock = ::accept(sock, &addr, &addrlen);
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
        continue;

      case ECONNABORTED:
      case EINTR:
      case ENOBUFS:
      case ENOMEM:
        break;

      default:
        fdev.cancel();
        close(sock);
        sock = -1;
        // TODO: Maybe throw something, or at least log.
        return;
      }
    }

    switch (errno) {
      case ECONNABORTED:
      case EINTR:
      case ENOBUFS:
      case ENOMEM:
        /* There's nothing to do in these cases.  Wait for another
           connection. */
        break;

    default:
      /* A connection was established.  Make sure we use it. */
      assert(clsock >= 0);
      parent.conns.emplace_back(parent, clsock);
      break;
    }

    /* Get ready to accept another connection. */
    assert(sock >= 0);
    fdev.set(sock, EPOLLIN);
    return;
  } while (true);
}

TCPEgress::Listener::Listener(TCPEgress &parent, int sock)
  : parent(parent), sock(sock),
    fdev(parent.sched,
         std::bind(&Listener::handle_fd, this, std::placeholders::_1))
{
  fdev.set(sock, EPOLLIN);
  fdev.name(sformat("egress:%s:listen:%d", parent.name.c_str(), sock));
}

TCPEgress::Listener::~Listener()
{
  /* Cancel any outstanding expectation before closing the socket. */
  fdev.cancel();
  if (sock >= 0)
    close(sock);
}

TCPEgress::Connection::Connection(TCPEgress &parent, int sock)
  : parent(parent), sock(sock), len(0),
    fdev(parent.sched,
         std::bind(&Connection::handle_fd, this, std::placeholders::_1))
{
  /* Get ready to receive immediately. */
  assert(sock >= 0);
  fdev.set(sock, EPOLLIN);
  fdev.name(sformat("egress:%s:connection:%d", parent.name.c_str(), sock));
}

TCPEgress::Connection::~Connection()
{
  /* Cancel any outstanding expectation before closing the socket. */
  fdev.cancel();
  if (sock >= 0)
    close(sock);
}

bool TCPEgress::Connection::process()
{
  /* Do we have a full packet? */
  labelset_t labels;
  std::size_t pktlen;
  const unsigned char *base = decode_message(labels, pktlen, buf, len);
  if (!base) return false; // Packet is incomplete.

  /* Get the union of all exits indicated by labels. */
  std::set<Exit *> chosen_exits;
  for (unsigned lbl = 0; lbl < MAX_LABELS; lbl++) {
    /* Is the label present in the set? */
    if ((labels & (UINT64_C(1) << lbl)) == 0)
      continue;
    /* Is an exit defined for this label? */
    auto pos = parent.exits.find(lbl);
    if (pos == parent.exits.end())
      continue;
    for (auto ptr : pos->second)
      chosen_exits.insert(ptr.get());
  }

  /* Pass the payload on to the union. */
  for (auto ptr : chosen_exits)
    ptr->deliver(base, pktlen);

  /* Consume the header and payload. */
  len = (buf + len) - (base + pktlen);
  memmove(buf, base + pktlen, len);
  return true;
}

void TCPEgress::Connection::handle_fd(uint32_t)
{
  assert(sock >= 0);
  ssize_t rc = recv(sock, buf + len, sizeof buf - len, 0);
  if (rc <= 0) {
    /* The client has closed the connection, or it has timed out. */
    // TODO: Log the error if rc < 0.
    fdev.cancel();
    close(sock);
    sock = -1;

    /* Make sure this entry gets cleaned out. */
    parent.idev.set();
    return;
  } else {
    /* Be ready to receive more. */
    fdev.set(sock, EPOLLIN);

    len += rc;

    /* Deliver any complete payloads to the exits, and clear the data
       out. */
    while (process())
      ;
  }
}

TCPEgress::TCPEgress(const std::string &name,
                     Scheduler &sched, const YAML::Node &cfg)
  : name(name),
    log("udptun.egress.tunnel.tcp", std::string("ingress:") + name),
    sched(sched),
    idev(sched, std::bind(&TCPEgress::flush, this)),
    ipv4(cfg["ipv4"].as<bool>("true")),
    ipv6(cfg["ipv6"].as<bool>("true")),
    host(cfg["host"].as<std::string>("localhost")),
    srv(cfg["port"].as<std::string>()) { }

void TCPEgress::flush()
{
  /* Go through all connections, deleting those which are closed. */
  conns.remove_if([](Connection &c) { return c.sock < 0; });
}

void TCPEgress::channel(unsigned label, std::shared_ptr<Exit> dst)
{
  exits[label].insert(dst);
}

void TCPEgress::activate()
{
  log.debug("activating");

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
    log.debug([this, &iter](std::ostream &out) {
      out << "creating " << af_to_str(iter->ai_family)
          << " socket proto " << proto_to_str(iter->ai_protocol)
          << " for " << to_str(iter->ai_addr, iter->ai_addrlen);
      if (iter->ai_flags & AI_V4MAPPED) out << " V4MAPPED";
      if (iter->ai_flags & AI_PASSIVE) out << " PASSIVE";
      if (iter->ai_flags & AI_NUMERICHOST) out << " NUMERICHOST";
      if (iter->ai_flags & AI_NUMERICSERV) out << " NUMERICSERV";
      if (iter->ai_flags & AI_ADDRCONFIG) out << " ADDRCONFIG";
      if (iter->ai_flags & AI_CANONNAME) out << " ADDRCONFIG";
      if (iter->ai_flags & AI_ALL) out << " ALL";
    });
    int sock = socket(iter->ai_family, SOCK_STREAM, iter->ai_protocol);
    if (sock < 0)
      throw std::system_error(errno, std::system_category(),
                              sformat("socket(%s, SOCK_STREAM, %s) for %s",
                                      af_to_str(iter->ai_family),
                                      proto_to_str(iter->ai_protocol),
                                      to_str(iter->ai_addr,
                                             iter->ai_addrlen).c_str()));

    log.detail("binding");
    if (bind(sock, iter->ai_addr, iter->ai_addrlen) != 0) {
      int ec = errno;
      close(sock);
      throw std::system_error(ec, std::system_category(),
                              sformat("bind(%s)",
                                      to_str(iter->ai_addr,
                                             iter->ai_addrlen).c_str()));
    }

    log.detail("listening");
    if (listen(sock, 5) != 0) {
      int ec = errno;
      close(sock);
      throw std::system_error(ec, std::system_category(),
                              sformat("listen %s %s %s",
                                      af_to_str(iter->ai_family),
                                      proto_to_str(iter->ai_protocol),
                                      to_str(iter->ai_addr,
                                             iter->ai_addrlen).c_str()));
    }

    assert(sock >= 0);
    listeners.emplace_back(*this, sock);
  }

  for (auto &ent : exits)
    for (auto &ptr : ent.second)
      ptr->activate();
}
