#include <list>

#include <iostream>
#include <boost/asio.hpp>
#include <boost/concept_check.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/array.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/regex.hpp>
#include <boost/program_options.hpp>


#include "constans.h"


using boost::asio::ip::tcp;
using boost::asio::ip::udp;

namespace posix = boost::asio::posix;
namespace po = boost::program_options;

// forward declaration
class tcp_client;
void start_new_connection();

unsigned int local_retransmit_limit = RETRANSMIT_LIMIT;


unsigned short global_port;
std::string global_server_name;

boost::asio::io_service global_io_service; // one io_service

udp::endpoint global_endpoint_udp; // servers adress
tcp::resolver::iterator global_endpoint_tcp;

class tcp_client
{
private:
  const int RECONNECT_TIME = 500; // miliseconds
  const int HEARTBEAT_TIME = 100; // miliseconds
  const int TROUBLESOME_TIME = 1; // seconds

  const int MAX_DATAGRAMS_WITHOUT_ACK = 2;
  const size_t LIST_LOW_WATERMARK = 2 * INPUT_BUFFER_LEN;

  bool stopped_;
  tcp::socket socket_tcp_;
  boost::shared_ptr<
    udp::socket> socket_udp_;
  boost::asio::ip::udp::endpoint server_remote_endpoint_;
  bool socket_udp_is_active_;

  posix::stream_descriptor input_;
  boost::asio::deadline_timer timer_reconnect_;
  boost::asio::deadline_timer timer_heartbeat_;
  boost::asio::deadline_timer timer_troublesome_;

  int datagram_to_server_nr_;
  int ack_nr_;
  std::string last_datagram_;
  // datagrams received witout confirmation(ack) of our last_datagram_
  int datagrams_without_ack_nr_;

  int max_seen_nr_;
  int expected_nr_;
  // here we store data from std::input
  std::list<char> input_data_list_;

public:
  tcp_client(boost::asio::io_service& io_service)
  : stopped_(false),
  socket_tcp_(io_service),
  socket_udp_is_active_(false),
  input_(io_service, ::dup(STDIN_FILENO)),
  timer_reconnect_(io_service, boost::posix_time::millisec(RECONNECT_TIME)),
  timer_heartbeat_(io_service, boost::posix_time::millisec(HEARTBEAT_TIME)),
  timer_troublesome_(io_service, boost::posix_time::seconds(TROUBLESOME_TIME)),
  datagram_to_server_nr_(0), ack_nr_(0), last_datagram_(),
  datagrams_without_ack_nr_(0), max_seen_nr_(-1), expected_nr_(-1),
  input_data_list_()
  {
    timer_reconnect_.async_wait(
      boost::bind(&tcp_client::handle_reconnect, this));
  }


  ///                  INIT_FUNCTIONS          ///
  /// ////////////////////////////////////////////
  void init_client();
  // connects to the server
  void connect();
private:
  // reconnect to the server if needed
  void handle_reconnect();
  void handle_connect(const boost::system::error_code& ec);
  // creates new udp socket ptr, starts timers, receiving raports and
  // datagrams
  void init_udp_connection();
  void handle_init_udp_connection(const boost::system::error_code& ec,
                                  size_t len, buffer_ptr buf);

  ///             UDP_FUNCTIONS                ///
  /// ////////////////////////////////////////////
  void send_heartbeat(const boost::system::error_code& ec);
  // sends one datagram of data
  void udp_send_data(size_t window);

  // receives one datagram of data from server
  void receive_from_server();
  void handle_receive_from_server(const boost::system::error_code& ec,
                                  size_t len, buffer_ptr buf);

  ///                       UDP PROTOCOL                         //
  /// /////////////////////////////////////////////////////////////
  void check_response(buffer_ptr buf, size_t len);
  void response_ack(int ack, size_t window);
  void response_data(int received_nr,
                       int ack, size_t window, const std::string &data);

  ///                OTHER FUNCTIONS              //
  /// //////////////////////////////////////////////
  // called when client is broken
  void stop();
  // stops the client when connection is troublesome
  void troublesome(const boost::system::error_code& ec);
  // keeps the connection alive
  void keep_alive();
  // accepts data from server
  void datagram_confirmed(const std::string &data);
  // checks whether we should send RETRANSMIT datagram,
  // reads data from message
  void data_and_ask_for_retransmission(int received_nr, const std::string &data);
  // retransmits (if neccessary) last datagram
  void ack_and_retransmit(int ack, size_t window);
  // reads bytes from std::in
  void read_data_from_stdin();
  void handle_read_data_from_stdin(const boost::system::error_code& ec,
                                   size_t len, buffer_ptr buf);
  std::string get_msg_from_list(size_t window);

  ///             TCP_FUNCTIONS                ///
  /// ////////////////////////////////////////////

  void receive_raports();
  void handle_receive_raports(const boost::system::error_code& ec,
                              size_t len, buffer_ptr buf);
};


///                  CLIENT FUNCTIONS         //

///                  INIT_FUNCTIONS           //
/// ////////////////////////////////////////////

void tcp_client::init_client()
{
  stopped_ = false;
  socket_tcp_ = tcp::socket(global_io_service);
  socket_udp_is_active_ = false;

  datagram_to_server_nr_ = 0;
  ack_nr_ = 0;
  last_datagram_ = "";
  datagrams_without_ack_nr_ = 0;

  connect();
}

void tcp_client::handle_reconnect()
{
  timer_reconnect_.expires_at(timer_reconnect_.expires_at() +
    boost::posix_time::millisec(RECONNECT_TIME));
  timer_reconnect_.async_wait(
    boost::bind(&tcp_client::handle_reconnect, this));

  if( stopped_ )
    init_client();
}

void tcp_client::connect()
{
  boost::asio::async_connect(socket_tcp_, global_endpoint_tcp,
                             //   socket_tcp_.async_connect(global_endpoint_tcp,
                             boost::bind(&tcp_client::handle_connect,
                                         this,
                                         boost::asio::placeholders::error));
}

void tcp_client::handle_connect(const boost::system::error_code& ec)
{
  if( ec == boost::asio::error::operation_aborted )
    return;

  if (ec)
  {
    std::cerr << "Connect error: " << ec.message() << "\n";
    stop();
    return;
  }
  init_udp_connection();
}

void tcp_client::init_udp_connection()
{
  buffer_ptr buf = buffer_ptr(new buffer_t());
  socket_tcp_.async_read_some(boost::asio::buffer(*buf),
                boost::bind(&tcp_client::handle_init_udp_connection,
                this,
                boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred,
                buf
                ));
}

void tcp_client::handle_init_udp_connection(const boost::system::error_code& ec,
                  size_t len, buffer_ptr buf)
{
  if( ec == boost::asio::error::operation_aborted )
    return;

  if (ec)
  {
    std::cerr << "Error on receive: " << ec.message() << "\n";
    stop();
    return;
  }
  try
  {
    // we create new udp socket ptr
    socket_udp_ = boost::shared_ptr<udp::socket>(
      new udp::socket( socket_tcp_.get_io_service() ));
    socket_udp_->open(udp::v6());
    socket_udp_is_active_ = true;

    // check validity of msg - it has to be like REGEX_CLIENT
    std::string msg(buf->data(), len);
    boost::smatch what;
    if(boost::regex_match(msg, what, REGEX_CLIENT))
    {
      socket_udp_->send_to(boost::asio::buffer(msg), global_endpoint_udp);

      // let's start receiving msgs and datagrams and stuff
      read_data_from_stdin();
      receive_from_server();
      receive_raports();
      udp_send_data(INPUT_BUFFER_WIHTOUT_HEADER_LEN);

      // Heartbeat timer
      timer_heartbeat_.expires_from_now(
        boost::posix_time::millisec(HEARTBEAT_TIME));
      timer_heartbeat_.async_wait(
        boost::bind(&tcp_client::send_heartbeat, this,
                    boost::asio::placeholders::error ));
      // Troublesome timer
      timer_troublesome_.expires_from_now(
        boost::posix_time::seconds(TROUBLESOME_TIME));
      timer_troublesome_.async_wait(
        boost::bind(&tcp_client::troublesome, this,
                    boost::asio::placeholders::error));
    }
    else
    {
      std::cerr << "regex_client - FAIL\n";
      stop();
    }
  }
  catch( std::exception& er )
  {
    stop();
    std::cerr << "EXCEPTION in handle_init_udp_connection" << er.what() << std::endl;
  }
}


///              UDP_FUNCTIONS - SENDING       //
/// /////////////////////////////////////////////

void tcp_client::send_heartbeat(const boost::system::error_code& ec)
{
  if( ec == boost::asio::error::operation_aborted )
  {
    std::cerr << "heartbeat aborted\n";
    return;
  }

  if( ec || !socket_udp_is_active_ )
  {
    std::cerr << "ERROR in send_heartbeat - " << ec.message() << std::endl;
    stop();
  }
  try
  {
    std::string msg("KEEPALIVE\n");
    socket_udp_->send_to(boost::asio::buffer(msg), global_endpoint_udp);

    timer_heartbeat_.expires_at(timer_heartbeat_.expires_at()
      + boost::posix_time::millisec(HEARTBEAT_TIME));

    timer_heartbeat_.async_wait(
      boost::bind(&tcp_client::send_heartbeat, this,
                  boost::asio::placeholders::error ));
  }
  catch( std::exception& ex )
  {
    std::cerr << "Error in send_heartbeat: " << ex.what() << std::endl;
  }
}

void tcp_client::udp_send_data(size_t window)
{
  try{
    std::string temp("UPLOAD " + std::to_string(datagram_to_server_nr_) + "\n");
    std::string data(get_msg_from_list(window));

    datagram_to_server_nr_++;
    last_datagram_ = temp + data;

    socket_udp_->send_to(boost::asio::buffer(last_datagram_),
                         global_endpoint_udp);
  }
  catch( std::exception &ex )
  {
    stop();
    std::cerr << ex.what() << std::endl;
  }
}

///              RECEIVING DATAGRAMS FROM SERVER      //
/// ////////////////////////////////////////////////////

void tcp_client::receive_from_server()
{
  buffer_ptr buf = buffer_ptr(new buffer_t());

  socket_udp_->async_receive_from(
    boost::asio::buffer(*buf), server_remote_endpoint_,
      boost::bind(&tcp_client::handle_receive_from_server, this,
      boost::asio::placeholders::error,
      boost::asio::placeholders::bytes_transferred,
      buf));
}

void tcp_client::handle_receive_from_server(const boost::system::error_code& ec,
                                      size_t len, buffer_ptr buf)
{
  if( ec == boost::asio::error::operation_aborted )
    return;

  if( ec )
  {
    std::cerr << "ERROR in handle_receive_from_server: " << ec.message() << std::endl;
    stop();
  }
  else
  {
    check_response(buf, len);
    receive_from_server();
  }
}


///                       UDP PROTOCOL                         //
/// /////////////////////////////////////////////////////////////
void tcp_client::check_response(buffer_ptr buf, size_t len)
{
  std::string msg(buf->data(), len);
  boost::smatch what;

  if( boost::regex_match(msg, what, REGEX_ACK) )
    response_ack( std::stoi(what[1].str()), std::stoi(what[2].str()) );

  else if( boost::regex_match(msg, what, REGEX_DATA) )
    response_data( std::stoi(what[1].str()), std::stoi(what[2].str()),
                     std::stoi(what[3].str()), what[4] );

  else
     std::cerr << "nie rozpoznano datagramu:\n" << msg;
}

void tcp_client::response_ack(int ack, size_t window)
{
  keep_alive();
  ack_and_retransmit(ack, window);
}

void tcp_client::response_data(int received_nr,
                               int ack, size_t window, const std::string &data)
{
  keep_alive();
  ack_and_retransmit(ack, window);
  data_and_ask_for_retransmission(received_nr, data);
}


///                OTHER FUNCTIONS              //
/// //////////////////////////////////////////////

void tcp_client::stop()
{
  stopped_ = true;
  socket_tcp_.close();

  if( socket_udp_is_active_ )
    socket_udp_->close();

  datagram_to_server_nr_  = ack_nr_ = datagrams_without_ack_nr_ = 0;
  max_seen_nr_ = expected_nr_ = -1;
  last_datagram_ == "";

  timer_heartbeat_.cancel();
  timer_troublesome_.cancel();
}

void tcp_client::troublesome(const boost::system::error_code& ec)
{
  if( ec == boost::asio::error::operation_aborted )
  {
    timer_troublesome_.expires_from_now(
      boost::posix_time::seconds(TROUBLESOME_TIME));
    timer_troublesome_.async_wait(
      boost::bind(&tcp_client::troublesome, this,
                  boost::asio::placeholders::error));
    return;
  }
  std::cerr << "\n\n troublesome connection\n ------------------------ \n";
  stop();
}

void tcp_client::keep_alive()
{
  timer_troublesome_.cancel();
}

void tcp_client::datagram_confirmed(const std::string& data)
{
    std::cout << data;
    std::cout.flush();
}

void tcp_client::data_and_ask_for_retransmission(int received_nr, const std::string& data)
{
  if( received_nr == expected_nr_ || expected_nr_ == -1 )
  {
    expected_nr_ = received_nr + 1;
    datagram_confirmed(data);
  }
  else if( received_nr < expected_nr_ )
  {
    // this datagram sucks, let it die
  }
  else // received_nr > expected_nr_
  {
    int temp = received_nr - local_retransmit_limit;

    if( expected_nr_ >= temp )
    {
      if( received_nr > max_seen_nr_ )
      {
        std::string retransimit_msg(
          "RETRANSMIT " + std::to_string(expected_nr_) + "\n");

        socket_udp_->send_to(boost::asio::buffer(retransimit_msg),
          global_endpoint_udp);
      }
    }
    // we accept datagram - to many were lost
    else
    {
      std::cerr << "------------- We accept datagram - too many were lost -----------------\n";
      expected_nr_ = received_nr + 1;
      datagram_confirmed(data);
    }
  }

  max_seen_nr_ = std::max(received_nr, max_seen_nr_);
}

void tcp_client::ack_and_retransmit(int ack, size_t window)
{
  if( ack == datagram_to_server_nr_ )
  {
    datagrams_without_ack_nr_ = 0;
    if( window > 0 )
      udp_send_data(window);
  }
  else
  {
    if (ack > datagram_to_server_nr_)
      std::cerr << "ack i greater than datagram_to_server_nr_! This is evil!\n";
    // here we check whether we should retransmit last_datagram
    datagrams_without_ack_nr_++;
    if( datagrams_without_ack_nr_ >= MAX_DATAGRAMS_WITHOUT_ACK )
    {
      socket_udp_->send_to(boost::asio::buffer(last_datagram_),
                           global_endpoint_udp);
      datagrams_without_ack_nr_ = 0;
    }
  }
}

void tcp_client::read_data_from_stdin()
{
  buffer_ptr buf =
    buffer_ptr( new buffer_t() );

  boost::asio::async_read(input_, boost::asio::buffer(*buf),
    boost::bind(&tcp_client::handle_read_data_from_stdin,
                this,
                boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred,
                buf));
}

void tcp_client::handle_read_data_from_stdin(
  const boost::system::error_code& ec, size_t len, buffer_ptr buf)
{
  if( ec == boost::asio::error::operation_aborted )
    return;

  if( ec == boost::asio::error::eof )
    return;

  if( ec )
    std::cerr << "ERROR handle_read_data_from_stdin: " <<  ec.message() << std::endl;
  else
  {
    std::string data(buf->data(), len);

    for( char c : data )
      input_data_list_.push_back(c);

    if( input_data_list_.size() < LIST_LOW_WATERMARK )
      read_data_from_stdin();
  }
}

std::string tcp_client::get_msg_from_list(size_t window)
{
  size_t len = std::min(INPUT_BUFFER_WIHTOUT_HEADER_LEN, window);
  len = std::min(len, input_data_list_.size());

  std::string result = "";
  for(size_t i = 0; i < len ; i++)
  {
    result.push_back(input_data_list_.front());
    input_data_list_.pop_front();
  }

  if( input_data_list_.size() < LIST_LOW_WATERMARK )
    read_data_from_stdin();

  return result;
}


///             TCP_FUNCTIONS                 //
/// ////////////////////////////////////////////

void tcp_client::receive_raports()
{
  buffer_ptr buf = buffer_ptr(new buffer_t());

  socket_tcp_.async_read_some(boost::asio::buffer(*buf),
                              boost::bind(
                                &tcp_client::handle_receive_raports,
                                this,
                                boost::asio::placeholders::error,
                                boost::asio::placeholders::bytes_transferred,
                                buf));
}

void tcp_client::handle_receive_raports(const boost::system::error_code& ec,
                                        size_t len, buffer_ptr buf)
{
  if( ec == boost::asio::error::operation_aborted )
    return;

  if( ec )
  {
    std::cerr << "\nERROR - receive_raports\n";
    std::cerr << ec.message() << std::endl;
    stop();
  }
  else
  {
    std::cerr.write(buf->data(), len);
    receive_raports();
  }
}


///                     FREE FUNCTIONS         //
/// /////////////////////////////////////////////
void set_global_endpoint()
{
  // UDP endpoint
  udp::resolver resolver(global_io_service);
  udp::resolver::query query(global_server_name, std::to_string(global_port));
  global_endpoint_udp = *resolver.resolve(query);

  std::cerr << "SERWER: " << global_endpoint_udp.address().to_string() << ":" << global_endpoint_udp.port() << std::endl;

  tcp::resolver r(global_io_service);
  tcp::resolver::query q(global_server_name, std::to_string(global_port));
  global_endpoint_tcp = r.resolve(q);
}

void program_options(int argc, char* argv[])
{
  /// PROGRAM_OPTIONS
  // Declare the supported options.
  po::options_description desc("Allowed options");
  desc.add_options()
  ("help", "produce help message")
  ("port,p", po::value<unsigned short>(&global_port)->default_value(PORT),
   "servers port")
  ("server-name,s", po::value<std::string>(&global_server_name),
   "hostname")
  ("retransmit-limit,X", po::value<unsigned int>(
    &local_retransmit_limit)->default_value(RETRANSMIT_LIMIT),
   "...")
  ;

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);
  if(vm.count("help"))
  {
    std::cerr << desc << "\n";
    exit(1);
  }
  if(!vm.count("server-name"))
  {
    std::cerr << "Usage: -s <hostname>\n";
    exit(1);
  }
}




int main(int argc, char* argv[])
{
  try
  {
    program_options(argc, argv);
    set_global_endpoint();

    tcp_client global_client(global_io_service);
    global_client.connect();
    global_io_service.run();
  }
  catch (std::exception& e)
  {
    std::cerr << e.what() << std::endl;
  }

  return 0;
}