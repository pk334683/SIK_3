#ifndef CLIENT_SESSION_H
#define CLIENT_SESSION_H

#include <string>
#include <algorithm>

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>
#include <boost/concept_check.hpp>
#include <boost/array.hpp>

using boost::asio::ip::tcp;

///                  TCP_CONNECTION             //
/// //////////////////////////////////////////////
class tcp_connection
: public boost::enable_shared_from_this<tcp_connection>
{
private:
  tcp::socket socket_;
  std::string message_;
  std::string address_;
  unsigned short port_tcp_;
  bool broken_;

public:
  typedef boost::shared_ptr<tcp_connection> pointer;


  static pointer create(boost::asio::io_service& io_service)
  {
    return pointer(new tcp_connection(io_service));
  }
  // zwraca gniazdko
  tcp::socket& socket();
  // to raczej do usuniecia
  void start(int id);
  // wysyla raport
  void send_raport(std::string raport);
  // true gdy polaczenie zepsute
  bool broken();
  // GETTERS
  std::string get_addres() { return address_; };
  unsigned short get_port_tcp() { return port_tcp_; }

private:
  tcp_connection(boost::asio::io_service& io_service);
  void handle_write(const boost::system::error_code& /*error*/,
                    size_t /*bytes_transferred*/);
  void stop();
};


///                  CLIENT_SESSION             //
/// //////////////////////////////////////////////

class client_session
: public boost::enable_shared_from_this<client_session>
{
public:
  enum queue_phase { ACTIVE, FILLING };

private:
  const size_t TROUBLESOME_SECONDS = 1;

  boost::shared_ptr<tcp_connection>  tcp_connection_;
  int id_;
  unsigned short port_udp_;
  bool troublesome_;
  std::vector<char> queue_;
  int queue_length_;
  queue_phase queue_phase_;
  size_t fifo_low_watermark_;
  size_t fifo_high_watermark_;
  size_t last_min_;
  size_t last_max_;
  int next_ack_;

  boost::asio::deadline_timer timer_troublesome_;
  bool broken_;


public:
  static boost::shared_ptr<client_session>
    create_client_session(boost::shared_ptr<tcp_connection> connection, int id,
      int queue_length, int fifo_low, int fifo_high)
  {
    return boost::shared_ptr<client_session>(
      new client_session(connection, id, queue_length, fifo_low, fifo_high));
  }



  void send_raport(std::string raport)
  {
    // TODO --->aktualizacja last_max_ i last_min_
    tcp_connection_->send_raport(raport);
    last_max_ = fifo_size();
    last_min_ = fifo_size();
  }

  void start(int id){ tcp_connection_->start(id); }

  // GETTERS
  int             get_id()      { return id_; }
  bool            broken()      { return tcp_connection_->broken() || broken_; }
  std::string     get_addres()  { return tcp_connection_->get_addres(); };
  unsigned short  get_port_tcp(){ return tcp_connection_->get_port_tcp(); }
  unsigned short& port_udp()    { return port_udp_; }
  unsigned int    get_window()  { return  queue_length_ - queue_.size(); }
  bool            is_active()   { return queue_phase_ == ACTIVE && !broken_; }
  char*           get_fifo()    { return queue_.data(); }
  size_t          fifo_size()   { return queue_.size(); }
  int&            next_ack()    { return next_ack_; }

  std::string     get_raport()
  {
    return std::string(get_addres() + ":" + std::to_string(get_port_tcp()) + " FIFO " +
      std::to_string(queue_.size()) + "/" + std::to_string(queue_length_) + " (min. " +
      std::to_string(last_min_) + ", max. " + std::to_string(last_max_) + ")\n");
  }

  // QUEUE MANIPULATION
  void add_data(const std::string &data)
  {
    // msg is too long!
    if( data.size() > get_window() )
    {
      std::cerr << "  \n\n======= ERROR:  Add data - not enough place in FIFO queue, we add as much as we can=======\n\n";
      for( size_t i = 0 ; i < get_window() ; i++ )
        queue_.push_back(data[i]);

      last_max_ = std::max(last_max_, fifo_size());
	  return;
    }

    for( char c : data )
      queue_.push_back(c);

    if( queue_.size() >= fifo_high_watermark_ )
      queue_phase_ = ACTIVE;

    last_max_ = std::max(last_max_, fifo_size());
  }

  // number of chars consumed
  void consume_data(size_t data_consumed)
  {
    if( data_consumed > queue_.size() )
    {
      std::cerr << " \n\n---- ERROR - consume_data - not enough elements to consume! We delete nothing\n\n";
      return;
    }
    if( data_consumed == 0 )
      return;

    auto new_begin = (queue_.begin() + data_consumed);
    std::rotate(queue_.begin(), new_begin, queue_.end());

    for( size_t i = 0 ; i < data_consumed ; i++ )
      queue_.pop_back();

    if(queue_.size() <= fifo_low_watermark_ )
      queue_phase_ = FILLING;

    last_min_ = std::min(last_min_, fifo_size());
  }

  void keep_alive()
  {
    timer_troublesome_.expires_from_now(boost::posix_time::seconds(TROUBLESOME_SECONDS));
  }


private:
  client_session(
    boost::shared_ptr<tcp_connection> connection,
    int id, int queue_length, int fifo_low, int fifo_high)
  :tcp_connection_(connection),
  id_(id),
  port_udp_(-1),
  troublesome_(false),
  queue_(),
  queue_length_(queue_length),
  queue_phase_(FILLING),
  fifo_low_watermark_(fifo_low),
  fifo_high_watermark_(fifo_high),
  last_min_(0),
  last_max_(0),
  next_ack_(0),
  timer_troublesome_(connection->socket().get_io_service(),
                     boost::posix_time::seconds(TROUBLESOME_SECONDS)),
  broken_(false)
  {
    tcp_connection_->start(id);
    timer_troublesome_.async_wait(
      boost::bind(&client_session::stop, this,
                  boost::asio::placeholders::error));
  }

  void stop(const boost::system::error_code& ec)
  {
    if( ec == boost::asio::error::operation_aborted )
    {
      timer_troublesome_.async_wait(
        boost::bind(&client_session::stop, this,
                    boost::asio::placeholders::error));
      return;
    }

    broken_ = true;
  }
};


#endif
