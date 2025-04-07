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
#include <cerrno>
#include <cstring>

#include <list>
#include <string>
#include <sstream>
#include <algorithm>
#include <functional>

#include "destruction.hh"
#include "formatting.hh"
#include "network.hh"
#include "emitters.hh"

void Emitter::prime_all()
{
  assert(ready);
  /* Prime each user to be able to send. */
  for (auto pos = users.begin(); pos != users.end(); pos = users.begin()) {
    auto ptr = *pos;
    users.erase(pos);
    (*ptr)();
  }
}

void Emitter::handle_fd(uint32_t events)
{
  ready = true;
  prime_all();
}

Emitter::Emitter(const std::string &name,
                 Scheduler &sched, const YAML::Node &cfg)
  : name(name),
    log("udptun.egress.emitter", std::string("emitter:") + name),
    ipv4(cfg ? cfg["ipv4"].as<bool>("true") : true),
    ipv6(cfg ? cfg["ipv6"].as<bool>("true") : true),
    host(cfg ? cfg["host"].as<std::string>("localhost")
         : std::string("localhost")),
    srv(cfg ? cfg["port"].as<std::string>() : std::string()),
    sock(-1), ready(false),
    fdev(sched, std::bind(&Emitter::handle_fd, this, std::placeholders::_1))
{
  fdev.name(std::string("emitter:") + name + ":descriptor");
}

void Emitter::activate()
{
  if (sock >= 0) return;
  log.debug("activating");

  /* Restrict what we're looking for. */
  struct addrinfo hints;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = ipv4 ? ipv6 ? AF_UNSPEC : AF_INET : AF_INET6;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE;
  hints.ai_protocol = 0;

  /* Resolve the host. */
  struct addrinfo *info = nullptr;
  LegacyDestructor infoDestr([&info]() { if (info) freeaddrinfo(info); });
  int rc = getaddrinfo(host.c_str(),
                       srv.empty() ? nullptr : srv.c_str(),
                       &hints, &info);
  int ec = errno;
  switch (rc) {
  case 0:
    break;

  case EAI_SYSTEM:
    throw std::system_error(ec, std::system_category(),
                            sformat("getaddrinfo(%s:%s)",
                                    host.c_str(), srv.c_str()));

  default:
    throw std::runtime_error(sformat("getaddrinfo(%s:%s) unk %d",
                                     host.c_str(), srv.c_str(), rc));

  case EAI_ADDRFAMILY:
    throw std::runtime_error(sformat("getaddrinfo(%s:%s) AF %s unavailable",
                                     host.c_str(), srv.c_str(),
                                     af_to_str(hints.ai_family).c_str()));

  case EAI_AGAIN:
    throw std::runtime_error(sformat("getaddrinfo(%s:%s) temp failure",
                                     host.c_str(), srv.c_str()));

  case EAI_BADFLAGS:
    throw std::runtime_error(sformat("getaddrinfo(%s:%s) bad flags",
                                     host.c_str(), srv.c_str()));

  case EAI_FAIL:
    throw std::runtime_error(sformat("getaddrinfo(%s:%s) failure",
                                     host.c_str(), srv.c_str()));

  case EAI_FAMILY:
    throw std::runtime_error(sformat("getaddrinfo(%s:%s) no AF %s",
                                     host.c_str(), srv.c_str(),
                                     af_to_str(hints.ai_family).c_str()));

  case EAI_MEMORY:
    throw std::runtime_error(sformat("getaddrinfo(%s:%s) out of memory",
                                     host.c_str(), srv.c_str()));

  case EAI_NODATA:
    throw std::runtime_error(sformat("getaddrinfo(%s:%s) no addrs for host",
                                     host.c_str(), srv.c_str()));

  case EAI_NONAME:
    throw std::runtime_error(sformat("getaddrinfo(%s:%s) unk name/service",
                                     host.c_str(), srv.c_str()));

  case EAI_SERVICE:
    throw std::runtime_error(sformat("getaddrinfo(%s:%s) no serv for host/sock",
                                     host.c_str(), srv.c_str()));

  case EAI_SOCKTYPE:
    throw std::runtime_error(sformat("getaddrinfo(%s:%s) no sock %s",
                                     host.c_str(), srv.c_str(),
                                     socktype_to_str(hints.ai_socktype)
                                     .c_str()));
  }

  /* Try to make a socket out of each offered entry, until one works.
     Store up the errors of the others, but only throw an exception if
     all fail. */
  std::map<struct addrinfo *, std::pair<const char *, int>> bad;
  for (auto iter = info; iter; iter = iter->ai_next) {
    int sock = socket(iter->ai_family, SOCK_DGRAM, iter->ai_protocol);
    if (sock < 0) {
      bad.try_emplace(iter, "socket", errno);
      continue;
    }

    if (bind(sock, iter->ai_addr, iter->ai_addrlen) != 0) {
      bad.try_emplace(iter, "bind", errno);
      continue;
    }

    this->sock = sock;
    this->family = iter->ai_family;
    this->protocol = iter->ai_protocol;
    fdev.set(sock, EPOLLOUT);
    return;
  }

  /* We failed to create a socket, so gather the error messages
     together. */
  std::stringstream msg;
  msg << "bad emitter";
  for (auto ent : bad) {
    auto sai = ent.first;
    auto &prb = ent.second;
    msg << " [" << af_to_str(sai->ai_family) << ", "
        << proto_to_str(sai->ai_protocol) << ", "
        << to_str(sai->ai_addr, sai->ai_addrlen) << ", "
        << prb.first;
#ifdef WITH_STRERROR_NP
    msg << ":" << strerrorname_np(prb.second);
#endif
    msg << " (" << ::strerror(prb.second) << ")]";
  }
  throw std::runtime_error(msg.str());
}

int Emitter::send(const void *buf, size_t len, Destination &dst, int flags)
{
  if (sock < 0)
    return EBADF;

  /* Don't bother calling again if we're already blocked. */
  if (!ready)
    return EWOULDBLOCK;

  /* Make this call non-blocking. */
  flags |= MSG_DONTWAIT;

  assert(sock >= 0);
  auto rc = dst.send(family, protocol, sock, buf, len, flags);

  if (rc < 0) {
    /* If we'd block (not that it's likely), ask the scheduler to tell
       us when we wouldn't, and record that there's no point in trying
       again until we can.  Also standardize the returned error
       code. */
    if (errno == EWOULDBLOCK || errno == EAGAIN) {
      fdev.set(sock, EPOLLOUT);
      ready = false;
      return EWOULDBLOCK;
    }
    return errno;
  }

  return 0;
}

Emitter::~Emitter()
{
  /* We shouldn't have any users by now. */
  assert(users.empty());

  /* Make sure we receive no more descriptor events, before closing
     the socket. */
  fdev.cancel();
  if (sock >= 0)
    close(sock);
}

void Emitter::notify(const user_t &user)
{
  users.insert(&user);
  if (sock < 0) return;
  if (ready)
    prime_all();
  else
    fdev.set(sock, EPOLLOUT);
}

void Emitter::forget(const user_t &user)
{
  users.erase(&user);
  if (users.empty())
    fdev.cancel();
}
