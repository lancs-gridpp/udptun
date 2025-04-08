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
#ifndef destinations_included
#define destinations_included

#include <sys/types.h>
#include <sys/socket.h>

#include <string>
#include <map>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "logger.hh"
#include "sockaddrs.hh"

struct addrinfo;

class Destination {
  const std::string name;
  Logger log;
  const bool ipv4, ipv6;
  const std::string host, srv;

  std::map<std::pair<int, int>, SocketAddress> options;

public:
  Destination(const std::string &name, const YAML::Node &cfg);
  void activate();

  /* Check whether a socket created using an address result could talk
     to this destination. */
  bool check(const struct addrinfo &);

  /* Match the address family and protocol of the given socket to a
     resolved socket address, and send the data.  Return ENOSYS if
     there is no matching family and protocol.  Returns 0 on
     success. */
  int send(int family, int protocol,
           int sockfd, const unsigned char *buf, size_t len, int flags);
};

#endif
