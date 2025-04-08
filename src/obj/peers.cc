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

#include <sys/socket.h>
#include <netdb.h>

#include <cstring>

#include "peers.hh"
#include "destruction.hh"
#include "network.hh"
#include "formatting.hh"

void PeerTable::load(const YAML::Node &cfg)
{
  if (!cfg) return;
  for (auto iter = cfg.begin(); iter != cfg.end(); iter++) {
    /* This is the internal name we use to identify queues. */
    auto id = iter->first.as<std::string>();
    auto lst = iter->second;
    for (auto iter2 = lst.begin(); iter2 != lst.end(); iter2++) {
      /* This is the host string. */
      auto val = iter2->as<std::string>();

      /* Restrict what we're looking for. */
      struct addrinfo hints;
      memset(&hints, 0, sizeof hints);
      hints.ai_family = AF_UNSPEC;
      hints.ai_socktype = SOCK_STREAM;
      hints.ai_flags = 0;
      hints.ai_protocol = 0;

      /* Resolve the host into its many forms. */
      struct addrinfo *info = nullptr;
      LegacyDestructor infoDestr([&info]() { if (info) freeaddrinfo(info); });
      int rc = getaddrinfo(val.c_str(), nullptr, &hints, &info);
      int ec = errno;
      switch (rc) {
      case 0:
        break;

      case EAI_SYSTEM:
        throw std::system_error(ec, std::system_category(),
                                sformat("getaddrinfo(%s)",
                                        val.c_str()));

      default:
        throw std::runtime_error(sformat("getaddrinfo(%s) unk %d",
                                         val.c_str(), rc));

      case EAI_ADDRFAMILY:
        throw std::runtime_error(sformat("getaddrinfo(%s) AF %s unavailable",
                                         val.c_str(),
                                         af_to_str(hints.ai_family).c_str()));

      case EAI_AGAIN:
        throw std::runtime_error(sformat("getaddrinfo(%s) temp failure",
                                         val.c_str()));

      case EAI_BADFLAGS:
        throw std::runtime_error(sformat("getaddrinfo(%s) bad flags",
                                         val.c_str()));

      case EAI_FAIL:
        throw std::runtime_error(sformat("getaddrinfo(%s) failure",
                                         val.c_str()));

      case EAI_FAMILY:
        throw std::runtime_error(sformat("getaddrinfo(%s) no AF %s",
                                         val.c_str(),
                                         af_to_str(hints.ai_family).c_str()));

      case EAI_MEMORY:
        throw std::runtime_error(sformat("getaddrinfo(%s) out of memory",
                                         val.c_str()));

      case EAI_NODATA:
        throw std::runtime_error(sformat("getaddrinfo(%s) no addrs for host",
                                         val.c_str()));

      case EAI_NONAME:
        throw std::runtime_error(sformat("getaddrinfo(%s) unk name/service",
                                         val.c_str()));

      case EAI_SERVICE:
        throw std::runtime_error(sformat("getaddrinfo(%s) no serv"
                                         " for host/sock",
                                         val.c_str()));

      case EAI_SOCKTYPE:
        throw std::runtime_error(sformat("getaddrinfo(%s) no sock %s",
                                         val.c_str(),
                                         socktype_to_str(hints.ai_socktype)
                                         .c_str()));
      }

      /* For each result, convert the address back into host and
         service, and use the address family and the host as a key
         mapping to the id.  Discard the service. */
      for (auto iter = info; iter; iter = iter->ai_next) {
        char host[120], serv[120];
        int nirc = getnameinfo(iter->ai_addr, iter->ai_addrlen,
                               host, sizeof host,
                               serv, sizeof serv,
                               NI_NUMERICHOST | NI_NUMERICSERV);
        int niec = errno;
        switch (nirc) {
        case 0:
          break;

        default:
          throw std::runtime_error(sformat("getnameinfo(%s) unk %d",
                                           val.c_str(), rc));

        case EAI_SYSTEM:
          throw std::system_error(niec, std::system_category(),
                                  sformat("getnameinfo(%s)", val.c_str()));

        case EAI_AGAIN:
          throw std::runtime_error(sformat("getnameinfo(%s) temp failure",
                                           val.c_str()));

        case EAI_BADFLAGS:
          throw std::runtime_error(sformat("getnameinfo(%s) bad flags",
                                           val.c_str()));

        case EAI_FAIL:
          throw std::runtime_error(sformat("getnameinfo(%s) failure",
                                           val.c_str()));

        case EAI_FAMILY:
          throw std::runtime_error(sformat("getnameinfo(%s) no AF %s",
                                           val.c_str(),
                                           af_to_str(hints.ai_family).c_str()));

        case EAI_MEMORY:
          throw std::runtime_error(sformat("getnameinfo(%s) out of memory",
                                           val.c_str()));
        case EAI_NONAME:
          throw std::runtime_error(sformat("getnameinfo(%s) unk name/service",
                                           val.c_str()));

        case EAI_OVERFLOW:
          throw std::runtime_error(sformat("getnameinfo(%s) overflow",
                                           val.c_str()));
        }

        tab[std::make_pair(iter->ai_addr->sa_family, host)] = id;
      }
    }
  }
}

bool PeerTable::seek(std::string &name,
                     const struct sockaddr *addr, socklen_t addrlen)
{
  char host[120], serv[120];
  int rc = getnameinfo(addr, addrlen, host, sizeof host, serv, sizeof serv,
                       NI_NUMERICHOST | NI_NUMERICSERV);
  int ec = errno;
  switch (rc) {
  case 0:
    break;

  default:
    throw std::runtime_error(sformat("getnameinfo(%s) unk %d",
                                     to_str(addr, addrlen).c_str(), rc));

  case EAI_SYSTEM:
    throw std::system_error(ec, std::system_category(),
                            sformat("getnameinfo(%s)",
                                    to_str(addr, addrlen).c_str()));

  case EAI_AGAIN:
    throw std::runtime_error(sformat("getnameinfo(%s) temp failure",
                                     to_str(addr, addrlen).c_str()));

  case EAI_BADFLAGS:
    throw std::runtime_error(sformat("getnameinfo(%s) bad flags",
                                     to_str(addr, addrlen).c_str()));

  case EAI_FAIL:
    throw std::runtime_error(sformat("getnameinfo(%s) failure",
                                     to_str(addr, addrlen).c_str()));

  case EAI_FAMILY:
    throw std::runtime_error(sformat("getnameinfo(%s) no AF %s",
                                     to_str(addr, addrlen).c_str(),
                                     af_to_str(addr->sa_family).c_str()));

  case EAI_MEMORY:
    throw std::runtime_error(sformat("getnameinfo(%s) out of memory",
                                     to_str(addr, addrlen).c_str()));
  case EAI_NONAME:
    throw std::runtime_error(sformat("getnameinfo(%s) unk name/service",
                                     to_str(addr, addrlen).c_str()));

  case EAI_OVERFLOW:
    throw std::runtime_error(sformat("getnameinfo(%s) overflow",
                                     to_str(addr, addrlen).c_str()));
  }

  return seek_resolved(name, std::make_pair(addr->sa_family, host));
}

bool PeerTable::seek_resolved(std::string &name,
                              const std::pair<int, std::string> &key)
{
  auto pos = tab.find(key);
  if (pos == tab.end()) {
    if (!backup) return false;
    return backup->seek_resolved(name, key);
  }
  name = pos->second;
  return true;
}
