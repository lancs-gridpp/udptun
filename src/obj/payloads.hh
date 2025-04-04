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

#include <fstream>

struct iovec;

class PayloadQueue;
class Chunk;

class Payload {
  std::size_t len_;
  unsigned char *base_;

  static void save(std::ofstream &out,
                   const void *base, std::size_t len);
  static void save(std::ofstream &out,
                   const Chunk *arr, std::size_t arrlen);
  friend class PayloadQueue;

public:
  Payload() : len_(0), base_(nullptr) { }
  Payload(const void *base, std::size_t len);
  Payload(const Chunk *base, std::size_t len);
  Payload(Payload &&);
  Payload &operator =(Payload &&);
  void save(std::ofstream &out);
  bool load(std::ifstream &in, std::size_t &sum);
  static void describe(std::ostream &,
                       const unsigned char *base, std::size_t len);
  void describe(std::ostream &out) { describe(out, base_, len_); }

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

#endif
