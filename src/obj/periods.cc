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

#include <sys/time.h>

#include <cmath>

#include <stdexcept>

#include "periods.hh"
#include "formatting.hh"

unsigned long long TimePeriod::to_milliseconds() const
{
  switch (unit) {
  default:
    throw std::invalid_argument(sformat("bad unit %d", int(unit)));

  case NANOSECOND:
    return amount / 1e6;
  case MICROSECOND:
    return amount / 1e3;
  case MILLISECOND:
    return amount;
  case SECOND:
    return amount * 1e3;
  case MINUTE:
    return amount * (60.0 * 1e3);
  case HOUR:
    return amount * (60.0 * 60.0 * 1e3);
  case DAY:
    return amount * (24.0 * 60.0 * 60.0 * 1e3);
  }
}

unsigned long long TimePeriod::to_microseconds() const
{
  switch (unit) {
  default:
    throw std::invalid_argument(sformat("bad unit %d", int(unit)));

  case NANOSECOND:
    return amount / 1e3;
  case MICROSECOND:
    return amount;
  case MILLISECOND:
    return amount * 1e3;
  case SECOND:
    return amount * 1e6;
  case MINUTE:
    return amount * (60.0 * 1e6);
  case HOUR:
    return amount * (60.0 * 60.0 * 1e6);
  case DAY:
    return amount * (24.0 * 60.0 * 60.0 * 1e6);
  }
}

unsigned long long TimePeriod::to_nanoseconds() const
{
  switch (unit) {
  default:
    throw std::invalid_argument(sformat("bad unit %d", int(unit)));

  case NANOSECOND:
    return amount;
  case MICROSECOND:
    return amount * 1e3;
  case MILLISECOND:
    return amount * 1e6;
  case SECOND:
    return amount * 1e9;
  case MINUTE:
    return amount * (60.0 * 1e9);
  case HOUR:
    return amount * (60.0 * 60.0 * 1e9);
  case DAY:
    return amount * (24.0 * 60.0 * 60.0 * 1e9);
  }
}

TimePeriod::TimePeriod(double amount, unit_t unit)
  : unit(unit), amount(amount) {
  switch (unit) {
  default:
    throw std::invalid_argument(sformat("bad unit %d", int(unit)));

  case NANOSECOND:
  case MICROSECOND:
  case MILLISECOND:
  case SECOND:
  case MINUTE:
  case HOUR:
  case DAY:
    break;
  }
}

TimePeriod::operator timeval() const
{
  struct timeval r;
  switch (unit) {
  case NANOSECOND:
    r.tv_sec = amount / 1e9;
    r.tv_usec = ::fmod(amount, 1e9) / 1000u;
    break;

  case MICROSECOND:
    r.tv_sec = amount / 1e6;
    r.tv_usec = ::fmod(amount, 1e6);
    break;

  case MILLISECOND:
    r.tv_sec = amount / 1e3;
    r.tv_usec = ::fmod(amount, 1e3) * 1000u;
    break;

  case SECOND:
    r.tv_sec = amount;
    r.tv_usec = ::fmod(amount, 1) * 1000000u;
    break;

  case MINUTE:
    r.tv_sec = amount * 60.0;
    r.tv_usec = ::fmod(amount * 60.0, 1) * 1000000u;
    break;

  case HOUR:
    r.tv_sec = amount * (60.0 * 60.0);
    r.tv_usec = ::fmod(amount * (60.0 * 60.0), 1) * 1000000u;
    break;

  case DAY:
    r.tv_sec = amount * (60.0 * 60.0 * 24.0);
    r.tv_usec = ::fmod(amount * (60.0 * 60.0 * 24.0), 1) * 1000000u;
    break;
  }
  return r;
}

void TimePeriod::clamp_nonnegative()
{
  if (amount < 0.0) amount = 0.0;
}

TimePeriod::operator timespec() const
{
  struct timespec r;
  switch (unit) {
  case NANOSECOND:
    r.tv_sec = amount / 1e9;
    r.tv_nsec = ::fmod(amount, 1e9);
    break;

  case MICROSECOND:
    r.tv_sec = amount / 1e6;
    r.tv_nsec = ::fmod(amount, 1e6) * 1000u;
    break;

  case MILLISECOND:
    r.tv_sec = amount / 1e3;
    r.tv_nsec = ::fmod(amount, 1e3) * 1000000u;
    break;

  case SECOND:
    r.tv_sec = amount;
    r.tv_nsec = ::fmod(amount, 1) * 1000000000u;
    break;

  case MINUTE:
    r.tv_sec = amount * 60.0;
    r.tv_nsec = ::fmod(amount * 60.0, 1) * 1000000000u;
    break;

  case HOUR:
    r.tv_sec = amount * (60.0 * 60.0);
    r.tv_nsec = ::fmod(amount * (60.0 * 60.0), 1) * 1000000000u;
    break;

  case DAY:
    r.tv_sec = amount * (60.0 * 60.0 * 24.0);
    r.tv_nsec = ::fmod(amount * (60.0 * 60.0 * 24.0), 1) * 1000000000u;
    break;
  }
  return r;
}
