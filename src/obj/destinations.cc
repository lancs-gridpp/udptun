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

#include <cstring>

#include <string>
#include <system_error>

#include "destinations.hh"
#include "destruction.hh"
#include "formatting.hh"

Destination::Destination(const YAML::Node &cfg)
{
  const bool ipv4 = cfg["ipv4"].as<bool>("true");
  const bool ipv6 = cfg["ipv6"].as<bool>("true");
  const std::string host = cfg["host"].as<std::string>("localhost");
  const std::string srv = cfg["port"].as<std::string>("");

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
    memset(&options[key].addr, 0, sizeof options[key].addr);
    memcpy(&options[key].addr, iter->ai_addr, iter->ai_addrlen);
    options[key].addrlen = iter->ai_addrlen;
  }
}

ssize_t Destination::send(int family, int protocol,
                          int sockfd, const void *buf, size_t len, int flags)
{
  Key key(family, protocol);
  auto pos = options.find(key);
  if (pos == options.end())
    return ENOSYS;
  return ::sendto(sockfd, buf, len, flags,
                  &pos->second.addr, pos->second.addrlen);
}
