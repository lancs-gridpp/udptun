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
#include <string>

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
  Logger &log = parent.log;

  /* A connection has been requested.  Accept it to get the file
     descriptor, and make a Connection object out of it. */
  do {
    union {
      struct sockaddr addr;
      unsigned char buf[256];
    } space;
    socklen_t addrlen = sizeof space;
    int clsock = ::accept(sock, &space.addr, &addrlen);
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
      std::string pname;
      if (parent.peers.seek(pname, &space.addr, addrlen)) {
        parent.conns.emplace_back(parent, pname, clsock, &space.addr, addrlen);
        log.info([this, &space, addrlen](std::ostream &out) {
          out << sock << ": new peer " << to_str(&space.addr, addrlen);
        });
      } else {
        log.warn([this, &space, addrlen](auto &out) {
          out << sock << ": unknown peer " << to_str(&space.addr, addrlen);
        });
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

TCPEgress::Connection::Connection(TCPEgress &parent,
                                  const std::string &name, int sock,
                                  const struct sockaddr *addr, socklen_t addrlen)
  : parent(parent),
    name(name),
    salt(std::hash<std::string>{}(name)),
    sock(sock), len(0),
    fdev(parent.sched,
         std::bind(&Connection::handle_fd, this, std::placeholders::_1)),
    peeraddr(addr, addrlen)
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

TCPEgress::Connection::ClientState::ClientState(const std::string &ename,
                                                const std::string &pname,
                                                clid_t clid,
                                                unsigned salt,
                                                channelmap_t &chmap,
                                                DestinationBank &dbank,
                                                Scheduler &sched)
  : log("udptun.egress.tunnel.tcp.client",
        ename + ':' + pname + ':' + std::to_string(clid)),
    last_used(std::chrono::system_clock::now())
{
  /* Get a full set of destinations that we need to talk to, and
     ensure they are activated.  Also keep a reverse map from
     destination to name, for diagnostics. */
  std::set<std::shared_ptr<Destination>> required_dests;
  std::map<std::shared_ptr<Destination>, std::string> dest_names;
  std::map<std::string, std::shared_ptr<Destination>> name_dests;
  for (auto &ent : chmap) {
    auto &dnames = ent.second;
    for (auto &dname : dnames) {
      std::shared_ptr<Destination> dest = dbank.seek(dname, salt);
      required_dests.insert(dest);
      dest_names[dest] = dname;
      name_dests[dname] = dest;
      dest->activate();
    }
  }

  /* Erode the set of required destinations, populating dest_em with
     an emitter per destination.  Several destinations may use the
     same emitter. */
  std::map<std::shared_ptr<Destination>, std::shared_ptr<Emitter>> dest_em;
  while (!required_dests.empty()) {
    EmitterMaker mkr(required_dests);
    if (mkr.matched().empty())
      // TODO: Provide more context in the message.
      throw std::runtime_error("could not make emitters for all destinations");

    // TODO: Choose a name for the emitter.

    /* Create the emitter, map the matched destinations to it, and
       eliminate those destinations from the required set. */
    mkr.make("dummy", dest_em);
  }

  /* Populate the mapping from label to <destination, emitter> pair. */
  for (auto &ent : chmap) {
    label_t label = ent.first;
    auto &dnames = ent.second;
    for (auto &dname : dnames) {
      std::shared_ptr<Destination> dest = name_dests[dname];
      std::shared_ptr<Emitter> em = dest_em[dest];
      outlets[label][dest] = em;
    }
  }
}

void TCPEgress::Connection::ClientState::deliver(label_t label,
                                                 const unsigned char *base,
                                                 std::size_t len)
{
  /* Ignore unknown labels. */
  auto pos = outlets.find(label);
  if (pos == outlets.end()) return;

  /* Send the message to each destination, using an appropriate
     emitter. */
  log.detail([&pos](auto &out) {
    out << "sending to " << pos->second.size();
  });
  for (auto &ent : pos->second)
    ent.second->send(base, len, *ent.first.get(), 0);

  last_used = std::chrono::system_clock::now();
}

bool TCPEgress::Connection::process()
{
  /* Do we have a full packet? */
  clid_t clid;
  label_t label;
  payloadlen_t pktlen;
  parent.log.detail([this](auto &out) {
    out << name << ": buffer (" << len << "):";
    for (size_t i = 0; i < len; i++)
      out << ' ' << sformat("%02X", buf[i]);
  });
  const unsigned char *base = decode_message(clid, label, pktlen, buf, len);
  if (!base) return false; // Packet is incomplete.

  parent.log.detail([this, clid, label, pktlen, base](auto &out) {
    out << name << ": clid=" << clid << " label=" << label
        << " len=" << pktlen << " payload=";
    for (size_t i = 0; i < pktlen; i++)
      out << ' ' << sformat("%02X", base[i]);
  });
  auto [ pos, ins ] = clstats.try_emplace(clid,
                                          parent.name,
                                          name,
                                          clid,
                                          salt,
                                          parent.channels, parent.dbank,
                                          parent.sched);
  if (ins)
    parent.log.detail([this, clid](auto &out) {
      out << name << ": new entry for " << clid << '/' << salt;
    });
  auto &clstat = pos->second;
  clstat.deliver(label, base, pktlen);

  /* Consume the header and payload. */
  len = (buf + len) - (base + pktlen);
  memmove(buf, base + pktlen, len);
  return true;
}

void TCPEgress::Connection::handle_fd(uint32_t)
{
  Logger &log = parent.log;
  assert(sock >= 0);
  ssize_t rc = recv(sock, buf + len, sizeof buf - len, 0);
  if (rc <= 0) {
    /* The client has closed the connection, or it has timed out. */
    // TODO: Log the error if rc < 0.
    if (rc == 0) {
      log.trace([this](auto &out) {
        out << name << ": client closed";
      });
    } else {
      int ec = errno;
      log.trace([this, ec](auto &out) {
        out << name << ": client error: " << ec
            << " (" << ::strerror(ec) << ")";
      });
    }
    fdev.cancel();
    close(sock);
    sock = -1;

    /* Make sure this entry gets cleaned out. */
    parent.idev.set();
    return;
  } else {
    typeof(len) nl = len + rc;
    log.detail([rc, this, nl](auto &out) {
      out << name << ": " << rc
          << "=recv(" << sock << ", *, "<< len << ") now " << nl;
    });
    /* Be ready to receive more. */
    fdev.set(sock, EPOLLIN);

    len = nl;

    /* Deliver any complete payloads to the exits, and clear the data
       out. */
    while (process())
      ;
  }
}

TCPEgress::TCPEgress(const std::string &name,
                     Scheduler &sched,
                     DestinationBank &dbank,
                     PeerTable &peers,
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
    channels(channels), dbank(dbank),
    peers(&peers) { }

void TCPEgress::flush()
{
  /* Go through all connections, deleting those which are closed. */
  conns.remove_if([](Connection &c) { return c.sock < 0; });
}

void TCPEgress::activate()
{
  log.debug("activating");

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
