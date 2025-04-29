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

#ifndef emitters_included
#define emitters_included

#include <cstdint>
#include <functional>
#include <set>
#include <map>

#include <yaml-cpp/yaml.h>

#include "logger.hh"
#include "descriptor.hh"
#include "sockaddrs.hh"

class Destination;

class Emitter;

typedef std::set<std::shared_ptr<Destination>> destination_set_t;
typedef std::map<std::shared_ptr<Destination>, std::shared_ptr<Emitter>>
destination_emitter_map_t;

class EmitterMaker {
  destination_set_t &required;
  struct addrinfo *info;
  const struct addrinfo *chosen;
  int sock;
  destination_set_t matched_;

public:
  EmitterMaker(destination_set_t &required);
  ~EmitterMaker();
  const destination_set_t &matched() { return matched_; }
  void make(const std::string &name,
            destination_emitter_map_t &result);
};

struct Emitter {
  friend class EmitterMaker;
  typedef std::function<void()> user_t;

private:
  const std::string name;
  Logger log;
  int sock;
  const int family, protocol;
  SocketAddress addr;

  Emitter(const std::string &name, int sock, int family, int protocol);

public:
  int send(const unsigned char *buf, size_t len, Destination &, int flags);
  ~Emitter();
};

#endif
