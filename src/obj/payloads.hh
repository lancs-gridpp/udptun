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

#ifndef payloads_included
#define payloads_included

#include <vector>
#include <map>
#include <list>
#include <memory>
#include <functional>
#include <filesystem>
#include <fstream>

typedef std::shared_ptr<std::vector<unsigned char>> payload_t;

payload_t make_payload(const unsigned char *, std::size_t len);

struct iovec;

class Payload {
  unsigned char *base_;
  std::size_t len_;

public:
  Payload() : base_(nullptr), len_(0) { }
  Payload(const void *base, std::size_t len);
  Payload(Payload &&);
  Payload &operator =(Payload &&);
  void save(std::ofstream &out);
  bool load(std::ifstream &in);

  /* Check for contents. */
  operator bool() { return base_; }

  const unsigned char *base() { return base_; }
  std::size_t size() { return len_; }

  /* Get the payload as a vector-write structure, and return true;
     otherwise, return false. */
  bool get(struct iovec &);

  /* Discard the contents. */
  void clear();
  ~Payload();
};


struct PayloadQueue {
  typedef std::function<bool(Payload &&)> user_t;

private:
  const std::filesystem::path dir;
  const std::size_t max_mem;

  std::size_t sz_mem;

  std::list<Payload> queue;
  const user_t user;

  typedef unsigned long long index_t;

  std::map<index_t, std::filesystem::path> queue_fns;
  std::size_t sz_out;
  std::ofstream out;

  static index_t now_index();
  std::filesystem::path make_queue_file(index_t key);
  bool load1(std::ifstream &);

  /* Load the entries from the oldest file into the queue, delete the
     file, and return true; otherwise, return false. */
  bool load_head_file();

  bool user_ready;

  void attempt_delivery();

public:
  PayloadQueue(std::size_t max_mem,
               const std::filesystem::path &dir, user_t);

  /* Add another payload to the queue. */
  void push(const void *, std::size_t);

  /* Acknowledge that the user is ready to receive again. */
  void awaken();

  ~PayloadQueue();
};

#endif
