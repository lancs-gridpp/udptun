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

#include <sys/uio.h>

#include <cassert>
#include <cstring>

#include <algorithm>

#include "marshaling.hh"

void push_onto(std::vector<struct iovec> &vec, unsigned char *base,
               std::size_t len)
{
  vec.resize(vec.size() + 1);
  ::memset(&vec.back(), 0, sizeof vec.back());
  vec.back().iov_base = base;
  vec.back().iov_len = len;
}

void push_onto(std::vector<struct iovec> &vec, const unsigned char *base,
               std::size_t len)
{
  vec.resize(vec.size() + 1);
  ::memset(&vec.back(), 0, sizeof vec.back());
  vec.back().iov_base = (void *) base;
  vec.back().iov_len = len;
}

bool labels_to_bytes(labelset_t labels, unsigned char *buf,
                     std::size_t done, std::size_t pos,
                     std::vector<struct iovec> &vec)
{
  if (done >= pos + MAX_LABEL_BYTES) return false;
  auto m = done > pos ? pos + MAX_LABEL_BYTES - done : MAX_LABEL_BYTES;
  assert(m >= 1);
  for (unsigned i = MAX_LABEL_BYTES - m; i < MAX_LABEL_BYTES; i++)
    buf[i] = (labels >> (i * 8)) & 0xffu;
  push_onto(vec, buf + (MAX_LABEL_BYTES - m), m);
  return true;
}

bool length_to_bytes(payloadlen_t len, unsigned char *buf,
                     std::size_t done, std::size_t pos,
                     std::vector<struct iovec> &vec)
{
  if (done >= pos + MAX_LENGTH_BYTES) return false;
  auto m = done > pos ? pos + MAX_LENGTH_BYTES - done : MAX_LENGTH_BYTES;
  assert(m >= 1);
  for (unsigned i = MAX_LENGTH_BYTES - m; i < MAX_LENGTH_BYTES; i++)
    buf[i] = (len >> ((MAX_LENGTH_BYTES - 1 - i) * 8)) & 0xffu;
  push_onto(vec, buf + (MAX_LENGTH_BYTES - m), m);
  return true;
}

const unsigned char *decode_message(labelset_t &labels, payloadlen_t &pktlen,
                                    const unsigned char *base, std::size_t got)
{
  /* Get the datagram length, if available. */
  if (got < MAX_LABEL_BYTES + MAX_LENGTH_BYTES) return nullptr;
  pktlen = base[MAX_LABEL_BYTES];
  pktlen <<= 8;
  pktlen |= base[MAX_LABEL_BYTES + 1];

  /* Do we have a complete packet? */
  if (got < MAX_LABEL_BYTES + MAX_LENGTH_BYTES + pktlen) return nullptr;

  /* Extract the labels. */
  labels = 0;
  for (unsigned i = 0; i < MAX_LABEL_BYTES; i++)
    labels |= base[i] << (8 * i);

  /* Return the start of the payload. */
  return base + (MAX_LABEL_BYTES + MAX_LENGTH_BYTES);
}
