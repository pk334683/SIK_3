#ifndef CONSTANS_H
#define CONSTANS_H

#include <boost/shared_ptr.hpp>
#include <boost/array.hpp>
#include <boost/regex.hpp>

const unsigned short PORT = 10000 + (334683 % 10000);
const size_t TX_INTERVAL = 5;
const size_t RETRANSMIT_LIMIT = 10;

const size_t RECONNECT_TIME = 500; // miliseconds
const size_t KEEP_ALIVE_TIME = 100; // in miliseconds
const size_t RAPORT_TIME = 1; // in seconds

const size_t FIFO_SIZE = 10560;
const size_t FIFO_LOW_WATERMARK = 0;
const size_t FIFO_HIGH_WATERMARK = FIFO_SIZE;
const size_t BUF_LEN = 10;

const size_t MAX_HEADER_LEN = 50;
const size_t INPUT_BUFFER_LEN = 4096;
const size_t INPUT_BUFFER_WIHTOUT_HEADER_LEN =
  INPUT_BUFFER_LEN - MAX_HEADER_LEN;

const size_t COUNT = 176;


typedef boost::array<char, INPUT_BUFFER_WIHTOUT_HEADER_LEN> buffer_without_header_t;
typedef boost::shared_ptr<buffer_without_header_t> buffer_without_header_ptr;

typedef boost::array<char, INPUT_BUFFER_LEN> buffer_t;
typedef boost::shared_ptr<buffer_t> buffer_ptr;


///                   REGEXES            ///
/// ////////////////////////////////////////
// CLIENT clientid\n
const boost::regex  REGEX_CLIENT("CLIENT (0|[1-9][0-9]*)\n");

// UPLOAD nr\n
// [dane]
const boost::regex REGEX_UPLOAD("UPLOAD (0|[1-9][0-9]*)\n(.*)");

// DATA nr ack win\n
// [dane]
const boost::regex REGEX_DATA(
  "DATA (0|[1-9][0-9]*) (0|[1-9][0-9]*) (0|[1-9][0-9]*)\n(.*)");

// ACK ack win\n
const boost::regex  REGEX_ACK("ACK (0|[1-9][0-9]*) (0|[1-9][0-9]*)\n");

// RETRANSMIT nr\n
const boost::regex  REGEX_RETRANSMIT("RETRANSMIT (0|[1-9][0-9]*)\n");

// KEEPALIVE\n
const boost::regex  REGEX_KEEP_ALIVE("KEEPALIVE\n");

#endif