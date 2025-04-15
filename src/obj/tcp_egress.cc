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
#include <cstring>

#include <sstream>

#include "tcp_egress.hh"
#include "destruction.hh"
#include "emitters.hh"
#include "exits.hh"
#include "destinations.hh"
#include "network.hh"
#include "marshaling.hh"
#include "addrent.hh"
#include "destbank.hh"

void TCPEgress::Listener::handle_fd(uint32_t)
{
  /* A connection has been requested.  Accept it to get the file
     descriptor, and make a Connection object out of it. */
  do {
    struct sockaddr addr;
    socklen_t addrlen = sizeof addr;
    int clsock = ::accept(sock, &addr, &addrlen);
    if (clsock < 0) {
      switch (errno) {
      case ENETDOWN:
      case EPROTO:
      case ENOPROTOOPT:
      case EHOSTDOWN:
      case ENONET:
      case EHOSTUNREACH:
      case EOPNOTSUPP:
      case ENETUNREACH:
        continue;

      case ECONNABORTED:
      case EINTR:
      case ENOBUFS:
      case ENOMEM:
        break;

      default:
        fdev.cancel();
        close(sock);
        sock = -1;
        // TODO: Maybe throw something, or at least log.
        return;
      }
    }

    switch (errno) {
      case ECONNABORTED:
      case EINTR:
      case ENOBUFS:
      case ENOMEM:
        /* There's nothing to do in these cases.  Wait for another
           connection. */
        break;

    default:
      /* A connection was established.  Make sure we use it. */
      assert(clsock >= 0);
      std::string peer;
      if (parent.peers.seek(peer, &addr, addrlen)) {
        auto pos = parent.exits.find(peer);
        if (pos == parent.exits.end()) {
          ::close(clsock);
        } else {
          exitmap_t &exits = pos->second;
          parent.conns.emplace_back(parent, clsock, exits);
        }
      } else {
        ::close(clsock);
      }
      break;
    }

    /* Get ready to accept another connection. */
    assert(sock >= 0);
    fdev.set(sock, EPOLLIN);
    return;
  } while (true);
}

TCPEgress::Listener::Listener(TCPEgress &parent, int sock)
  : parent(parent), sock(sock),
    fdev(parent.sched,
         std::bind(&Listener::handle_fd, this, std::placeholders::_1))
{
  fdev.set(sock, EPOLLIN);
  fdev.name(sformat("egress:%s:listen:%d", parent.name.c_str(), sock));
}

TCPEgress::Listener::~Listener()
{
  /* Cancel any outstanding expectation before closing the socket. */
  fdev.cancel();
  if (sock >= 0)
    close(sock);
}

TCPEgress::Connection::Connection(TCPEgress &parent, int sock,
                                  exitmap_t &exits)
  : parent(parent), sock(sock), exits(exits), len(0),
    fdev(parent.sched,
         std::bind(&Connection::handle_fd, this, std::placeholders::_1))
{
  /* Get ready to receive immediately. */
  assert(sock >= 0);
  fdev.name(sformat("egress:%s:connection:%d", parent.name.c_str(), sock));
  fdev.set(sock, EPOLLIN);
}

TCPEgress::Connection::~Connection()
{
  /* Cancel any outstanding expectation before closing the socket. */
  fdev.cancel();
  if (sock >= 0)
    close(sock);
}

bool TCPEgress::Connection::process()
{
  /* Do we have a full packet? */
  label_t label;
  payloadlen_t pktlen;
  const unsigned char *base = decode_message(label, pktlen, buf, len);
  if (!base) return false; // Packet is incomplete.

  /* If this label is defined, deliver to each of the corresponding
     exits. */
  auto lpos = exits.find(label);
  if (lpos != exits.end())
    for (auto &ex : lpos->second)
      ex.deliver(base, pktlen);

  /* Consume the header and payload. */
  len = (buf + len) - (base + pktlen);
  memmove(buf, base + pktlen, len);
  return true;
}

void TCPEgress::Connection::handle_fd(uint32_t)
{
  assert(sock >= 0);
  ssize_t rc = recv(sock, buf + len, sizeof buf - len, 0);
  if (rc <= 0) {
    /* The client has closed the connection, or it has timed out. */
    // TODO: Log the error if rc < 0.
    fdev.cancel();
    close(sock);
    sock = -1;

    /* Make sure this entry gets cleaned out. */
    parent.idev.set();
    return;
  } else {
    /* Be ready to receive more. */
    fdev.set(sock, EPOLLIN);

    len += rc;

    /* Deliver any complete payloads to the exits, and clear the data
       out. */
    while (process())
      ;
  }
}

TCPEgress::TCPEgress(const std::string &name,
                     Scheduler &sched,
                     Quota &quota,
                     std::filesystem::path qdir,
                     PeerTable *peers_backup,
                     DestinationBank &dests,
                     const channelmap_t &channels,
                     const YAML::Node &cfg)
  : name(name),
    log("udptun.egress.tunnel.tcp", std::string("egress:") + name),
    sched(sched),
    idev(sched, std::bind(&TCPEgress::flush, this)),
    ipv4(cfg["ipv4"].as<bool>("true")),
    ipv6(cfg["ipv6"].as<bool>("true")),
    host(cfg["host"].as<std::string>("localhost")),
    srv(cfg["port"].as<std::string>()),
    quota(quota), qdir(qdir), peers(peers_backup), channels(channels)
{
  for (const std::string &peer : peers.names()) {
    unsigned salt = std::hash<std::string>{}(peer);

    for (auto &ent : channels)
      for (auto dname : ent.second.dests) {
        /* Map the destination name to a shared pointer to the
           object. */
        auto ptr = dests.seek(dname, salt);
        /* Populate a reverse mapping back to destination name, so we
           can meaningfully name the emitter that references this
           destination. */
        requirement[peer][ptr] = dname;
      }
  }
}

void TCPEgress::flush()
{
  /* Go through all connections, deleting those which are closed. */
  conns.remove_if([](Connection &c) { return c.sock < 0; });
}

void TCPEgress::activate()
{
  log.debug("activating");

  /* Using the template in this->channels, create a graph of exits,
     emitters and destinations for each peer.  The destinations
     already exist, and can be looked up through this->dests.  A
     sufficient number of emitters is created to account for all
     destinations, and an exit is created for each destination. */
  for (auto &peer_req : requirement) {
    const std::string &peer = peer_req.first;
    auto &dest_names = peer_req.second;

    /* Get a set of all destinations our channels contact, and
       activate them. */
    std::set<std::shared_ptr<Destination>> required;
    std::map<std::string, std::shared_ptr<Destination>> rmap;
    for (const auto &req : dest_names) {
      required.insert(req.first);
      rmap[req.second] = req.first;
      req.first->activate();
    }

    /* Create a minimal set of emitters to talk to each destination. */
    std::map<std::shared_ptr<Destination>, std::shared_ptr<Emitter>> desems;
    while (!required.empty()) {
      /* Prepare to create one emitter accounting for as many
         destinations as possible. */
      EmitterMaker mkr(required);
      if (mkr.matched().empty())
        throw std::runtime_error("could not make emitters for all destinations");

      /* Choose a name for the emitter.  Combine this egress's name,
         the name of the peer, and all the destinations' names. */
      std::stringstream exname;
      exname << name << ':' << peer;
      for (auto ptr : mkr.matched())
        exname << ':' << dest_names[ptr];

      /* Create the emitter, and map the compatible destinations to it. */
      mkr.make(exname.str(), sched, desems);
    }

    /* Create an exit for each destination, using the emitter created
       (non-exclusively) for it. */
    for (auto &ent : channels) {
      auto label = ent.first;
      const auto &labelname = ent.second.name;
      for (auto dname : ent.second.dests) {
        auto dst = rmap[dname];

        /* Which emitter is to be used with this destination? */
        auto em = desems[dst];

        /* Create an exit for this emitter and destination, and index
           under the label. */
        exits[peer][label].emplace_back(name + ':' + peer + ':' +
                                        labelname + ':' + dname,
                                        sched, quota,
                                        qdir / peer / labelname / dname,
                                        em, dst);
      }
    }
  }
  requirement.clear();

  /* For our own sockets, restrict what we're looking for. */
  struct addrinfo hints;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = ipv4 ? ipv6 ? AF_UNSPEC : AF_INET : AF_INET6;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  hints.ai_protocol = 0;

  /* Resolve the host. */
  struct addrinfo *info = nullptr;
  get_address_info(info, host.c_str(), srv.empty() ? nullptr : srv.c_str(),
                   &hints, sformat("egress:%s", name.c_str()).c_str());

  std::set<AddressEntry> addrs;
  for (auto iter = info; iter; iter = iter->ai_next) {
    auto f = addrs.emplace(*iter);
    log.detail([&f](std::ostream &out) {
      if (f.second) out << "new ";
      out << "gai result: " << std::string(*f.first);
    });
  }

  for (auto iter = addrs.begin(); iter != addrs.end(); iter++) {
    log.debug([this, &iter](std::ostream &out) {
      out << "creating " << std::string(*iter);
    });
    int sock = socket(iter->family, SOCK_STREAM, iter->protocol);
    if (sock < 0)
      throw std::system_error(errno, std::system_category(),
                              sformat("socket(%s, SOCK_STREAM, %s) for %s",
                                      af_to_str(iter->family),
                                      proto_to_str(iter->protocol),
                                      to_str(iter->addr(),
                                             iter->addrlen).c_str()));

    log.detail("binding");
    if (bind(sock, iter->addr(), iter->addrlen) != 0) {
      int ec = errno;
      close(sock);
      throw std::system_error(ec, std::system_category(),
                              sformat("bind(%s)",
                                      to_str(iter->addr(),
                                             iter->addrlen).c_str()));
    }

    log.detail("listening");
    if (listen(sock, 5) != 0) {
      int ec = errno;
      close(sock);
      throw std::system_error(ec, std::system_category(),
                              sformat("listen %s %s %s",
                                      af_to_str(iter->family),
                                      proto_to_str(iter->protocol),
                                      to_str(iter->addr(),
                                             iter->addrlen).c_str()));
    }

    assert(sock >= 0);
    listeners.emplace_back(*this, sock);
  }
}
