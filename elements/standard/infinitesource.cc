/*
 * infinitesource.{cc,hh} -- element generates configurable infinite stream
 * of packets
 * Eddie Kohler
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * Further elaboration of this license, including a DISCLAIMER OF ANY
 * WARRANTY, EXPRESS OR IMPLIED, is provided in the LICENSE file, which is
 * also accessible at http://www.pdos.lcs.mit.edu/click/license.html
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <click/config.h>
#include <click/package.hh>
#include "infinitesource.hh"
#include <click/confparse.hh>
#include <click/error.hh>
#include <click/router.hh>
#include "scheduleinfo.hh"
#include <click/glue.hh>

InfiniteSource::InfiniteSource()
  : Element(0, 1)
{
  MOD_INC_USE_COUNT;
  _packet = 0;
}

InfiniteSource::~InfiniteSource()
{
  MOD_DEC_USE_COUNT;
}

InfiniteSource *
InfiniteSource::clone() const
{
  return new InfiniteSource;
}

int
InfiniteSource::configure(const Vector<String> &conf, ErrorHandler *errh)
{
  String data = "Random bullshit in a packet, at least 64 bytes long. Well, now it is.";
  int limit = -1;
  int burstsize = 1;
  bool active = true, stop = false;
  
  if (cp_va_parse(conf, this, errh,
		  cpOptional,
		  cpString, "packet data", &data,
		  cpInteger, "total packet count", &limit,
		  cpInteger, "burst size (packets per scheduling)", &burstsize,
		  cpBool, "active?", &active,
		  cpKeywords,
		  "DATA", cpString, "packet data", &data,
		  "LIMIT", cpInteger, "total packet count", &limit,
		  "ACTIVE", cpBool, "active?", &active,
		  "STOP", cpBool, "stop driver when done?", &stop,
		  0) < 0)
    return -1;
  if (burstsize < 1)
    return errh->error("argument 3 (burst size) must be >= 1");

  _data = data;
  _limit = limit;
  _burstsize = burstsize;
  _count = 0;
  _active = active;
  _stop = stop;
  if (_packet) _packet->kill();
  _packet = Packet::make(_data.data(), _data.length());
  return 0;
}

int
InfiniteSource::initialize(ErrorHandler *errh)
{
  ScheduleInfo::join_scheduler(this, errh);
  return 0;
}

void
InfiniteSource::uninitialize()
{
  unschedule();
  _packet->kill();
  _packet = 0;
}

void
InfiniteSource::run_scheduled()
{
  if (!_active)
    return;
  int n = _burstsize;
  if (_limit >= 0 && _count + n >= _limit)
    n = _limit - _count;
  if (n > 0) {
    for (int i = 0; i < n; i++)
      output(0).push(_packet->clone());
    _count += n;
    reschedule();
  } else if (_stop)
    router()->please_stop_driver();
}

Packet *
InfiniteSource::pull(int)
{
  if (!_active)
    return 0;
  if (_limit >= 0 && _count + 1 >= _limit) {
    if (_stop)
      router()->please_stop_driver();
    return 0;
  }
  _count++;
  return _packet->clone();
}

String
InfiniteSource::read_param(Element *e, void *vparam)
{
  InfiniteSource *is = (InfiniteSource *)e;
  switch ((int)vparam) {
   case 0:			// data
    return is->_data;
   case 1:			// limit
    return String(is->_limit) + "\n";
   case 2:			// burstsize
    return String(is->_burstsize) + "\n";
   case 3:			// active
    return cp_unparse_bool(is->_active) + "\n";
   case 4:			// count
    return String(is->_count) + "\n";
   default:
    return "";
  }
}

int
InfiniteSource::change_param(const String &in_s, Element *e, void *vparam,
			     ErrorHandler *errh)
{
  InfiniteSource *is = (InfiniteSource *)e;
  String s = cp_uncomment(in_s);
  switch ((int)vparam) {

   case 1: {			// limit
     int limit;
     if (!cp_integer(s, &limit))
       return errh->error("limit parameter must be integer");
     is->_limit = limit;
     is->set_configuration_argument(1, s);
     break;
   }
   
   case 2: {			// burstsize
     int burstsize;
     if (!cp_integer(s, &burstsize) || burstsize < 1)
       return errh->error("burstsize parameter must be integer >= 1");
     is->_burstsize = burstsize;
     is->set_configuration_argument(2, s);
     break;
   }
   
   case 3: {			// active
     bool active;
     if (!cp_bool(s, &active))
       return errh->error("active parameter must be boolean");
     is->_active = active;
     if (!is->scheduled() && active)
       is->reschedule();
     break;
   }

   case 5: {			// reset
     is->_count = 0;
     if (!is->scheduled() && is->_active)
       is->reschedule();
     break;
   }

  }
  return 0;
}

void
InfiniteSource::add_handlers()
{
  add_read_handler("data", read_param, (void *)0);
  add_write_handler("data", reconfigure_write_handler, (void *)0);
  add_read_handler("limit", read_param, (void *)1);
  add_write_handler("limit", change_param, (void *)1);
  add_read_handler("burstsize", read_param, (void *)2);
  add_write_handler("burstsize", change_param, (void *)2);
  add_read_handler("active", read_param, (void *)3);
  add_write_handler("active", change_param, (void *)3);
  add_read_handler("count", read_param, (void *)4);
  add_write_handler("reset", change_param, (void *)5);
}

EXPORT_ELEMENT(InfiniteSource)
