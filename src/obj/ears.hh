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

#ifndef ears_included
#define ears_included

#include <string>
#include <map>
#include <memory>
#include <vector>
#include <list>

#include <yaml-cpp/yaml.h>

#include "descriptor.hh"
#include "timed.hh"
#include "idle.hh"

class Exit;

struct Ear {
  virtual void activate() { };
  virtual void channel(unsigned, std::shared_ptr<Exit> dst) = 0;
  virtual ~Ear() = default;
};

class TCPEar : public Ear {
  class Listener {
    friend TCPEar;
    TCPEar &parent;
    int sock;
    void handle_fd(uint32_t);
    DescriptorEvent fdev;

  public:
    Listener(TCPEar &, int sock);
    ~Listener();
  };
  friend class Listener;
  std::list<Listener> listeners;

  class Connection {
    friend TCPEar;
    TCPEar &parent;
    int sock;
    void handle_fd(uint32_t);
    unsigned char buf[4 + 2 + 65507];
    std::size_t len;
    DescriptorEvent fdev;

    bool process();

  public:
    Connection(TCPEar &, int sock);
    ~Connection();
  };
  friend class Connection;
  std::list<Connection> conns;

  Scheduler &sched;
  IdleEvent idev;
  const bool ipv4, ipv6;
  const std::string host, srv;
  std::map<unsigned, std::shared_ptr<Exit>> exits;

  void flush();

public:
  TCPEar(Scheduler &sched, const YAML::Node &cfg);
  void activate();
  void channel(unsigned, std::shared_ptr<Exit> dst);
};

Ear *make_ear(Scheduler &,
              const std::map<std::string, std::shared_ptr<Exit>> &refs,
              const YAML::Node &);

#endif
