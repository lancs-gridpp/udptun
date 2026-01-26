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

#include "sockaddrs.hh"
#include "destinations.hh"
#include "destruction.hh"
#include "formatting.hh"
#include "network.hh"
#include "payloads.hh"
#include "jsonout.hh"

Destination::Destination(const std::string &name, const YAML::Node &cfg)
  : name(name),
    log("udptun.egress.destination", std::string("destination:") + name),
    ipv4(cfg ? cfg["ipv4"].as<bool>("true") : true),
    ipv6(cfg ? cfg["ipv6"].as<bool>("true") : true),
    host(cfg ? cfg["host"].as<std::string>("localhost")
         : std::string("localhost")),
    srv(cfg ? cfg["port"].as<std::string>() : std::string()),
    activated(false) { }

std::string Destination::describe()
{
  std::stringstream r;
  r << '{';
  json_maplet(r, "name", name, true);
  json_maplet(r, "host", host);
  json_maplet(r, "srv", srv);
  json_maplet(r, "ipv4", ipv4);
  json_maplet(r, "ipv6", ipv6);
  json_maplet(r, "active", activated);
  r << '}';
  return r.str();
}

void Destination::activate()
{
  if (activated) return;
  log.debug([this](auto &out) {
    out << "activating on " << host << ":" << srv;
    if (ipv4) out << ";ipv4";
    if (ipv6) out << ";ipv6";
  });

  /* Restrict what we're looking for. */
  struct addrinfo hints;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = ipv4 ? ipv6 ? AF_UNSPEC : AF_INET : AF_INET6;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = 0;

  struct addrinfo *info = nullptr;
  LegacyDestructor infoDestr([&info]() { if (info) freeaddrinfo(info); });
  get_address_info(info, host.c_str(), srv.empty() ? nullptr : srv.c_str(),
                   &hints, sformat("destination:%s", name.c_str()).c_str());

  /* Store each result indexed by address family and protocol. */
  for (auto iter = info; iter; iter = iter->ai_next) {
    options.try_emplace(std::make_pair(iter->ai_family, iter->ai_protocol),
                        iter->ai_addr, iter->ai_addrlen);
  }

  activated = true;
}

bool Destination::check(const struct addrinfo &ai) const
{
  return check(ai.ai_family, ai.ai_protocol);
}

bool Destination::check(int family, int protocol) const
{
  if (!activated)
    throw std::runtime_error(sformat("too soon to check against dest %s",
                                     name.c_str()));
  auto pos = options.find(std::make_pair(family, protocol));
  return pos != options.end();
}

const SocketAddress *Destination::peer(int family, int protocol) const
{
  if (!activated)
    throw std::runtime_error(sformat("too soon to check against dest %s",
                                     name.c_str()));
  auto pos = options.find(std::make_pair(family, protocol));
  if (pos == options.end()) return nullptr;
  return &pos->second;
}


int Destination::send(int family, int protocol,
                      int sockfd, const unsigned char *buf,
                      size_t len, int flags) const
{
  if (!activated)
    throw std::runtime_error(sformat("too soon to send to dest %s",
                                     name.c_str()));
  assert(sockfd >= 0);
  auto pos = options.find(std::make_pair(family, protocol));
  if (pos == options.end()) {
    log.detail([len, buf, pos](std::ostream &out) {
      out << "sent " << len << ":";
      Payload::describe(out, buf, len);
      out << " to " << to_str(pos->second.addr(), pos->second.len());
    });
    return ENOSYS;
  }
  int rc = ::sendto(sockfd, buf, len, flags,
                    pos->second.addr(), pos->second.len());
  if (rc < 0) {
    int ec = errno;
    log.debug([len, buf, pos, ec](std::ostream &out) {
      out << "failed to send " << len << ":";
      Payload::describe(out, buf, len);
      out << " to " << to_str(pos->second.addr(), pos->second.len())
          << " for " << ec;
#ifdef WITH_STRERROR_NP
      out << ":" << strerrorname_np(ec);
#endif
      out << " (" << ::strerror(ec) << ")]";
    });
    return ec;
  }
  if ((typeof(len)) rc == len) {
    log.detail([len, buf, pos](std::ostream &out) {
      out << "sent " << len << ":";
      Payload::describe(out, buf, len);
      out << " to " << to_str(pos->second.addr(), pos->second.len());
    });
    return 0;
  }
  assert(rc >= 0);
  log.detail([rc, len, buf, pos](std::ostream &out) {
    out << "sent " << rc << "<" << len << ":";
      Payload::describe(out, buf, len);
      out << " to " << to_str(pos->second.addr(), pos->second.len());
  });
  /* TODO: What to do here?  Shouldn't be reachable. */
  return 0;
}
