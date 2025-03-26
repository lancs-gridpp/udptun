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

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/epoll.h>

#include <cstring>
#include <cassert>

#include "absorbers.hh"
#include "destruction.hh"
#include "formatting.hh"
#include "channels.hh"

Absorber::~Absorber() { }

UDPAbsorber::UDPAbsorber(const std::string &name,
                         Scheduler &sched, const YAML::Node &cfg,
                         const std::set<std::shared_ptr<Channel>> &channels)
  : name(name), log("udptun.ingress.absorber.udp", std::string("absorber:") + name),
    ipv4(cfg ? cfg["ipv4"].as<bool>("true") : true),
    ipv6(cfg ? cfg["ipv6"].as<bool>("true") : true),
    host(cfg ? cfg["host"].as<std::string>("localhost")
         : std::string("localhost")),
    srv(cfg ? cfg["port"].as<std::string>() : std::string()),
    channels(channels),
    sock(-1),
    sockev(sched, std::bind(&UDPAbsorber::sock_ready, this, std::placeholders::_1))
{
  sockev.name(sformat("absorber:%s:descriptor", name.c_str()));
}

UDPAbsorber::~UDPAbsorber()
{
  sockev.cancel();
  if (sock >= 0) ::close(sock);
}

void UDPAbsorber::activate()
{
  if (sock >= 0) return;

  /* Restrict what we're looking for. */
  struct addrinfo hints;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = ipv4 ? ipv6 ? AF_UNSPEC : AF_INET : AF_INET6;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = 0;

  /* Resolve the host. */
  struct addrinfo *info = nullptr;
  LegacyDestructor infoDestr([&info]() { if (info) freeaddrinfo(info); });
  int rc = getaddrinfo(host.c_str(),
                       srv.empty() ? nullptr : srv.c_str(),
                       &hints, &info);
  if (rc < 0)
    throw std::system_error(errno, std::system_category(),
                            sformat("getaddrinfo(%s)", host.c_str()));

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
    sockev.set(this->sock, EPOLLIN);

    for (auto &c : channels)
      c->activate();
    return;
  }

  /* We failed to create a socket, so gather the error messages
     together. */
  std::stringstream msg;
  msg << "bad emitter";
  for (auto ent : bad) {
    auto sai = ent.first;
    auto &prb = ent.second;
    msg << " [" << sai->ai_family << ", " << sai->ai_protocol << ", "
        << prb.first << ": " << ::strerror(prb.second) << "]";
  }
  throw std::runtime_error(msg.str());
}

void UDPAbsorber::sock_ready(uint32_t events)
{
  assert(sock >= 0);
  ssize_t rc = recvfrom(sock, buf, sizeof buf, 0, nullptr, nullptr);
  if (rc >= 0) {
    for (auto cp : channels)
      cp->submit(buf, rc);
  } else {
    // TODO
  }
  sockev.set(sock, EPOLLIN);
}

Absorber *make_absorber(Scheduler &sched,
                        const std::string &inst,
                        const YAML::Node &cfg,
                        channel_index_t channels)
{
  const auto &channels_root = cfg["channels"];
  if (!channels_root) return nullptr;

  std::set<std::shared_ptr<Channel>> channel_set;
  for (auto iter = channels_root.begin(); iter != channels_root.end(); iter++) {
    const auto &cname = iter->as<std::string>();
    auto pos = channels(cname);
    if (!pos)
      throw std::runtime_error(sformat("unknown channel %s for socket %s",
                                       cname.c_str(), inst.c_str()));
    channel_set.insert(pos);
  }
  if (channel_set.empty())
    return nullptr;

  if (cfg["udp"])
    return new UDPAbsorber(inst, sched, cfg["udp"], channel_set);

  return nullptr;
}
