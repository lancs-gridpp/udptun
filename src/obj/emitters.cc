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

#include "formatting.hh"
#include "network.hh"
#include "emitters.hh"
#include "destinations.hh"

EmitterMaker::EmitterMaker(destination_set_t &required)
  : required(required), info(nullptr), chosen(nullptr), sock(-1)
{
  /* Get address/socket configurations for sending out datagrams. */
  struct addrinfo hints;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE;
  hints.ai_protocol = 0;
  get_address_info(info, nullptr, "0", &hints, "emitter");

  /* Count how many of the required destinations are compatible with
     each entry. */
  std::map<struct addrinfo *, unsigned> counters;
  for (auto iter = info; iter; iter = iter->ai_next) {
    for (auto ptr : required) {
      if (ptr->check(iter->ai_family, iter->ai_protocol)) {
        auto [ pos, ins ] = counters.try_emplace(iter, 0);
        pos->second++;
      }
    }
  }
  if (counters.empty())
    return;

  /* Sort address entries by the count of matching destinations
     (descending). */
  std::vector<std::pair<struct addrinfo *, unsigned>>
    seq(counters.begin(), counters.end());
  std::sort(seq.begin(), seq.end(), [](const auto &lhs, const auto &rhs) {
    return lhs.second > rhs.second;
  });

  /* Create a socket and bind it, preferring entries with more
     matching destinations. */
  for ( ; !seq.empty(); seq.erase(seq.begin())) {
    const auto &best = *seq[0].first;
    int sock = ::socket(best.ai_family, SOCK_DGRAM, best.ai_protocol);
    if (sock < 0) continue;
    if (::bind(sock, best.ai_addr, best.ai_addrlen) != 0) {
      ::close(sock);
      continue;
    }

    this->sock = sock;
    this->chosen = &best;

    /* Make available a set of destinations which matched.  The user
       might use this to name the emitter. */
    for (auto diter = required.begin(); diter != required.end(); diter++)
      if ((*diter)->check(chosen->ai_family, chosen->ai_protocol))
        matched_.insert(*diter);

    return;
  }
}

EmitterMaker::~EmitterMaker()
{
  if (info) ::freeaddrinfo(info);
  if (sock >= 0) ::close(sock);
}

void EmitterMaker::make(const std::string &name,
                        destination_emitter_map_t &result)
{
  if (sock < 0) return;

  std::shared_ptr<Emitter> r =
    std::shared_ptr<Emitter>(new Emitter(name, sock,
                                         chosen->ai_family, chosen->ai_protocol));
  sock = -1;

  /* Remove the destinations matching this socket from 'required', and
     add map them to the result. */
  for (auto ptr : matched_) {
    required.erase(ptr);
    result[ptr] = r;
  }
}



Emitter::Emitter(const std::string &name,
                 int sock, int family, int protocol)
  : name(name),
    log("udptun.egress.emitter", std::string("emitter:") + name),
    sock(sock), family(family), protocol(protocol) { }

int Emitter::send(const unsigned char *buf, size_t len,
                  Destination &dst, int flags)
{
  if (sock < 0)
    return EBADF;

  /* Make this call non-blocking. */
  flags |= MSG_DONTWAIT;

  assert(sock >= 0);
  auto rc = dst.send(family, protocol, sock, buf, len, flags);

  /* Standardize the returned error code. */
  if (rc == EWOULDBLOCK || rc == EAGAIN)
    return EWOULDBLOCK;

  /* Pass other errors through. */
  if (rc != 0) return rc;

  return 0;
}

Emitter::~Emitter()
{
  if (sock >= 0)
    close(sock);
}
