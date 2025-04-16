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

#ifndef tcp_ingress_included
#define tcp_ingress_included

#include <netdb.h>

#include <cstdint>

#include <string>
#include <vector>
#include <list>
#include <set>

#include <yaml-cpp/yaml.h>

#include "logger.hh"
#include "ingress.hh"
#include "descriptor.hh"
#include "timed.hh"
#include "addrevent.hh"

struct addrinfo;

class Streamer;

class TCPIngress : public Ingress {
  const std::string name;
  Logger log;
  const bool ipv4, ipv6;
  const std::string host, bind_host;
  const std::string srv, bind_srv;
  int sock;
  bool connected, upout_ready;

  DescriptorEvent fdev;
  void descriptor_ready(uint32_t);

  TimedEvent rstev;
  void initiate_lookup();

  const struct addrinfo *ainf;
  AddressEvent addrev;
  void address_resolved(const struct addrinfo *);
  void clear_socket();
  void try_connect();

  /* Try to get data from one of our sources (the head of
     'streamer_queue'), and send it.  Keep doing this until there's no
     more to send, or we get EWOULDBLOCK.  If 'upout_ready' is false
     on entry, set up a callbacl for when the socket is writable
     instead.  'upout_ready' gets set to false after any attempt to
     send a message.  */
  void try_send();

  /* We keep a queue of sources of data, and a set to prevent
     duplicates. */
  std::set<Streamer *> streamers;
  std::list<Streamer *> streamer_queue;

  /* We offer this to data sources when we're ready to send.  It is
     cleared before each use, but retains its allocation. */
  std::vector<struct iovec> iov;

public:
  TCPIngress(const std::string &name,
             Scheduler &sched, AddressManager &, const YAML::Node &);
  ~TCPIngress();
  void ready(Streamer &);
};

#endif
