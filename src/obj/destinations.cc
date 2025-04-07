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

#include <netdb.h>

#include <cassert>
#include <cstring>
#include <cerrno>

#include <string>
#include <system_error>

#include "destinations.hh"
#include "destruction.hh"
#include "formatting.hh"
#include "network.hh"

Destination::Destination(const std::string &name, const YAML::Node &cfg)
  : name(name),
    log("udptun.egress.destination", std::string("destination:") + name),
    ipv4(cfg ? cfg["ipv4"].as<bool>("true") : true),
    ipv6(cfg ? cfg["ipv6"].as<bool>("true") : true),
    host(cfg ? cfg["host"].as<std::string>("localhost")
         : std::string("localhost")),
    srv(cfg ? cfg["port"].as<std::string>() : std::string()) { }

void Destination::activate()
{
  log.debug("activating");

  /* Restrict what we're looking for. */
  struct addrinfo hints;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = ipv4 ? ipv6 ? AF_UNSPEC : AF_INET : AF_INET6;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = 0;

  struct addrinfo *info = nullptr;
  LegacyDestructor infoDestr([&info]() { if (info) freeaddrinfo(info); });
  int rc = getaddrinfo(host.c_str(),
                       srv.empty() ? nullptr : srv.c_str(),
                       &hints, &info);
  if (rc < 0)
    throw std::system_error(errno, std::system_category(),
                            sformat("getaddrinfo(%s)", host.c_str()));

  /* Store each result indexed by address family and protocol. */
  for (auto iter = info; iter; iter = iter->ai_next) {
    Key key(iter->ai_family, iter->ai_protocol);
    options.try_emplace(key, iter->ai_addr, iter->ai_addrlen);
  }
}

Destination::Value::Value(const struct sockaddr *addr, socklen_t len)
  : buf(len)
{
  assert(buf.size() == len);
  std::memcpy(buf.data(), addr, len);
}


int Destination::send(int family, int protocol,
                      int sockfd, const unsigned char *buf,
                      size_t len, int flags)
{
  Key key(family, protocol);
  auto pos = options.find(key);
  if (pos == options.end()) {
    log.detail([len, pos](std::ostream &out) {
      out << "sent " << len << " to "
          << to_str(pos->second.addr(), pos->second.addrlen());
    });
    return ENOSYS;
  }
  int rc = ::sendto(sockfd, buf, len, flags,
                    pos->second.addr(), pos->second.addrlen());
  if (rc == len) {
    log.detail([len, pos](std::ostream &out) {
      out << "sent " << len << " to "
          << to_str(pos->second.addr(), pos->second.addrlen());
    });
    return 0;
  }
  if (rc > 0) {
    log.detail([rc, len, pos](std::ostream &out) {
      out << "sent " << rc << "<" << len << " to "
          << to_str(pos->second.addr(), pos->second.addrlen());
    });
    /* TODO: What to do here?  Shouldn't be reachable. */
    return 0;
  }
  int ec = errno;
  log.debug([len, pos, ec](std::ostream &out) {
    out << "failed to send " << len
        << " to " << to_str(pos->second.addr(), pos->second.addrlen())
        << " for " << ec;
#ifdef WITH_STRERROR_NP
    out << ":" << strerrorname_np(ec);
#endif
    out << " (" << ::strerror(ec) << ")]";
  });
  return ec;
}
