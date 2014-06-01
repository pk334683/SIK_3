#include <tuple>
#include <map>
#include <set>
#include <ctime>
#include <iostream>
#include <string>
#include <algorithm>

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>
#include <boost/concept_check.hpp>
#include <boost/array.hpp>
#include <boost/regex.hpp>
#include <boost/program_options.hpp>

#include "client_session.h"
#include "mixer.h"
#include "constans.h"


using boost::asio::ip::tcp;
using boost::asio::ip::udp;
namespace po = boost::program_options;


unsigned short local_port = PORT;
unsigned int local_fifo_size = FIFO_SIZE;
unsigned int local_low_watermark = FIFO_LOW_WATERMARK;
unsigned int local_high_watermark = FIFO_HIGH_WATERMARK;
unsigned int local_buf_len = BUF_LEN;
unsigned int local_tx_interval = TX_INTERVAL;
unsigned int local_retransmit_limit = RETRANSMIT_LIMIT;


// SERVER
class server
{

  // ZMIENNE
private:
  // id_ --> session
  std::map<int, boost::shared_ptr<client_session> > sessions_;
  tcp::acceptor acceptor_;

  boost::asio::ip::udp::socket udp_socket_;
  // IMPORTANT!
  // client_remote_endpoint_ is being used in only in 1 function! receive_udp()
  boost::asio::ip::udp::endpoint client_remote_endpoint_;
  boost::asio::deadline_timer timer_mix_;
  boost::asio::deadline_timer timer_raport_;
  // pairs( datagram_nr , DATAGRAM )
  std::set<std::pair<int, std::string> > last_datagrams_;

  int next_id_ = 0;
  int current_datagram_nr_ = 0;

public:

  server(boost::asio::io_service& io_service)
  : sessions_(),
  acceptor_(io_service, tcp::endpoint(tcp::v6(), local_port)),
  udp_socket_(io_service,
              udp::endpoint(udp::v6(), local_port)),
  timer_mix_(io_service, boost::posix_time::millisec(TX_INTERVAL)),
  timer_raport_(io_service, boost::posix_time::seconds(RAPORT_TIME)),
  last_datagrams_()
  {
    std::cerr << " port: " << local_port << "\n";
    timer_mix_.async_wait(boost::bind(&server::send_mixed_data, this));
    timer_raport_.async_wait(boost::bind(&server::send_raports, this));
    start_accept(); // TCP
    receive_udp();
  }

private:

  ///  ********************  TCP FUNCTIONS ***********************//
  /// //////////////////////////////////////////////////////////////

  void start_accept();
  void handle_accept(tcp_connection::pointer new_connection,
                     const boost::system::error_code& ec);
  void send_raports();

  ///  ********************  UDP FUNCTIONS ***********************//
  /// //////////////////////////////////////////////////////////////
  // mixes data from all active clients. pop_back consumed bytes in
  // clients fifos.
  // returns: mixed data ready to send
  std::string   mix_data();
  void send_mixed_data();
  void receive_udp();
  void handle_receive_udp(const boost::system::error_code& ec,
                          size_t len, buffer_ptr buf);

  ///  ********************  UDP PROTOCOL ***********************//
  /// /////////////////////////////////////////////////////////////

  void check_response(buffer_ptr buf, size_t len);
  void response_client(int client_id);
  void response_upload(int datagram_nr,  int client_id, const std::string &data);
  void response_keep_alive(int client_id);
  void response_retransmit(int client_id, int nr);

  ///  ********************  OTHERS  ***********************//
  /// ////////////////////////////////////////////////////////

  int           get_next_id();
  bool          client_already_exists_tcp(std::string address,
                                          unsigned short port_tcp);
  // returns clients id ( or -1 if it does not exist )
  int           client_already_exists_udp(std::string address,
                                          unsigned short port_udp);
  void          delete_broken_clients();
  void          delete_data_from_fifos(const std::vector<mixer_input> &inputs);
  // adds new datagram to the buffer. Also does :  current_datagram_nr_++
  void          add_new_datagram(std::string datagram);
  // returns endpoint to the client
  udp::endpoint get_client_endpoint(int client_id);
  std::string   create_upload_datagram(int datagram_nr, int ack, int window,
                                       std::string DATA);
}; // server

///  ********************  TCP FUNCTIONS ***********************//
/// //////////////////////////////////////////////////////////////

void server::start_accept()
{
  tcp_connection::pointer new_connection =
    tcp_connection::create(acceptor_.get_io_service());

  acceptor_.async_accept(new_connection->socket(),
      boost::bind(&server::handle_accept, this, new_connection,
        boost::asio::placeholders::error));
}

void server::handle_accept(tcp_connection::pointer new_connection,
    const boost::system::error_code& ec)
{
  if (!ec)
  {
    if( client_already_exists_tcp(
          new_connection->get_addres(), new_connection->get_port_tcp()) )
      return;

    std::cerr << "hadnle accept ...\n";
    int id = get_next_id();
    new_connection->start(id);
    sessions_[id] = client_session::create_client_session(
      new_connection, id, local_fifo_size, local_low_watermark,
      local_high_watermark);

  }
  start_accept();
}

void server::send_raports()
{
  delete_broken_clients();

  std::string raport("\n");
  for( auto s : sessions_ )
    raport = raport + std::get<1>(s)->get_raport();

  for( auto s : sessions_ )
    std::get<1>(s)->send_raport(raport);

  timer_raport_.expires_at(timer_raport_.expires_at() + boost::posix_time::seconds(RAPORT_TIME));
  timer_raport_.async_wait(boost::bind(&server::send_raports, this));
}


///  ********************  UDP FUNCTIONS ***********************//
/// //////////////////////////////////////////////////////////////

std::string server::mix_data()
{
  size_t message_length = COUNT * local_tx_interval;

  std::vector<mixer_input> inputs;
  for( auto s : sessions_ )
    if( std::get<1>(s)->is_active() )
    {
      mixer_input inp;
      inp.data = s.second->get_fifo();
      inp.len  = s.second->fifo_size();
      inp.consumed = 0;
      inputs.push_back(inp);
    }

    size_t bytes_written = message_length;
    char mixed_data[message_length];

    mixer(inputs.data(), inputs.size(), mixed_data, &bytes_written, local_tx_interval);

    delete_data_from_fifos(inputs);

    std::string msg_to_send(mixed_data, bytes_written);

    return msg_to_send;
}

void server::send_mixed_data()
{
  try
  {
    timer_mix_.expires_at(timer_mix_.expires_at() + boost::posix_time::millisec(local_tx_interval));
    timer_mix_.async_wait(boost::bind(&server::send_mixed_data, this));

    delete_broken_clients();
    if( sessions_.empty() )
      return;

    std::string msg_to_send = mix_data();
    add_new_datagram(msg_to_send);

    for(auto s : sessions_)
    {
      std::string rdy_to_send =
        create_upload_datagram(current_datagram_nr_, std::get<1>(s)->next_ack(),
                              std::get<1>(s)->get_window(), msg_to_send);

      int client_id = std::get<1>(s)->get_id();

      udp_socket_.send_to(boost::asio::buffer(rdy_to_send),
                          get_client_endpoint(client_id));
    }
  }
  catch( std::exception &ex)
  {
    std::cerr << ex.what() << std::endl;
  }

}

void server::receive_udp()
{
  buffer_ptr buf = buffer_ptr(new buffer_t());
  udp_socket_.async_receive_from(
    boost::asio::buffer(*buf), client_remote_endpoint_,
    boost::bind(&server::handle_receive_udp, this,
      boost::asio::placeholders::error,
      boost::asio::placeholders::bytes_transferred,
      buf));
}

void server::handle_receive_udp(const boost::system::error_code& ec,
                                size_t len, buffer_ptr buf)
{
  if(ec)
  {
    std::cerr << "Blad w async_receive_from w receive_UDP\n" << ec.message() << std::endl;
    receive_udp();
    return;
  }
  try
  {
    check_response(buf, len);
    receive_udp();
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception w handle_receive_udp: " << e.what() << std::endl;
    receive_udp();
  }
}

///  ********************  UDP PROTOCOL ***********************//
/// /////////////////////////////////////////////////////////////

void server::check_response(buffer_ptr buf, size_t len)
{
  std::string msg(buf->data(), len);

  boost::smatch what;
  if(boost::regex_match(msg, what, REGEX_CLIENT))
  {
    response_client(std::stoi(what[1].str()));
    return;
  }
  // does client exist?
  int id =  client_already_exists_udp(
      client_remote_endpoint_.address().to_string(), client_remote_endpoint_.port());

  if( id == -1 )
    return;

  // HERE WE KNOW THAT CLIENT DOES EXIST!
  if( sessions_[id]->broken() )
    return; // client is broken, let him die

  if(boost::regex_match(msg, what, REGEX_UPLOAD))
    response_upload(std::stoi(what[1].str()), id, what[2]);

  else if(boost::regex_match(msg, what, REGEX_KEEP_ALIVE))
    response_keep_alive(id);

  else if(boost::regex_match(msg, what, REGEX_RETRANSMIT))
    response_retransmit(id, std::stoi(what[1].str()));

  else
    std::cerr << "incorrect datagram\n";
}

void server::response_client(int client_id)
{
  // no such client
  if( sessions_.find(client_id) == sessions_.end() )
    return;
  // wrong address
  if( sessions_[client_id]->get_addres() !=
    client_remote_endpoint_.address().to_string() )
    return;

  sessions_[client_id]->keep_alive();

  std::cerr << "client ok! id: " << client_id << "\n";

  sessions_[client_id]->port_udp() =
    client_remote_endpoint_.port();
}

void server::response_upload(int datagram_nr, int client_id, const std::string &data)
{
  sessions_[client_id]->keep_alive();

  if( datagram_nr== sessions_[client_id]->next_ack() )
  {
    sessions_[client_id]->next_ack()++;
    sessions_[client_id]->add_data(data);

    std::string window = std::to_string(sessions_[client_id]->get_window());

    // datagram + 1 == sessions_[client_id]->next_ack(), it's just shorter
    std::string ack = std::to_string(datagram_nr + 1);
    std::string msg("ACK " + ack + " " + window + "\n");

    udp_socket_.send_to(boost::asio::buffer(msg), client_remote_endpoint_);
  }
}

void server::response_keep_alive(int client_id)
{
  sessions_[client_id]->keep_alive();
}

void server::response_retransmit(int client_id, int nr)
{
  sessions_[client_id]->keep_alive();
  udp::endpoint endpoint = get_client_endpoint(client_id);

  for( auto d : last_datagrams_ )
  {
    int datagram_number = std::get<0>(d);
    if(datagram_number <= nr)
    {
      std::string data = std::get<1>(d);
      // we send datagram with window = 0
      std::string datagram(create_upload_datagram(datagram_number,
        sessions_[client_id]->next_ack(), 0, //sessions_[client_id]->get_window(),
        data));

      udp_socket_.send_to(
        boost::asio::buffer(datagram),
        endpoint);
    }
  }
}

///  ********************  OTHERS  ***********************//
/// ////////////////////////////////////////////////////////

int server::get_next_id()
{
  return next_id_++;
}

bool server::client_already_exists_tcp(std::string address, unsigned short port_tcp)
{
  for( auto s : sessions_ )
  {
    if( std::get<1>(s)->get_addres() == address &&
        std::get<1>(s)->get_port_tcp() == port_tcp )
      return true;
  }
  return false;
}

int server::client_already_exists_udp(std::string address, unsigned short port_udp)
{
  for(auto s : sessions_)
  {
    if( std::get<1>(s)->get_addres() == address &&
        std::get<1>(s)->port_udp() == port_udp )
      return std::get<0>(s);
  }
  return -1;
}

void server::delete_broken_clients()
{
  bool ok = true;
  while(ok)
  {
    ok = false;
    for(auto s : sessions_)
    {

      if( std::get<1>(s)->broken() )
      {
        ok = true;
        sessions_.erase(sessions_.find(std::get<0>(s)));
        break;
      }
    }
  }
}

void server::delete_data_from_fifos(const std::vector<mixer_input> &inputs)
{
  size_t  i = 0;
  for( auto s : sessions_ )
  {
    if( i >= inputs.size() )
      return;

    if( std::get<1>(s)->is_active() )
    {
      std::get<1>(s)->consume_data(inputs[i].consumed);
      i++;
    }
  }
}

void server::add_new_datagram(std::string datagram)
{
  current_datagram_nr_++;

  last_datagrams_.insert(std::pair<int, std::string>(
    current_datagram_nr_, datagram));

  if( last_datagrams_.size() > local_retransmit_limit )
    last_datagrams_.erase(last_datagrams_.begin());
}

udp::endpoint server::get_client_endpoint(int client_id)
{
  udp::endpoint result;

  result.port((sessions_[client_id])->port_udp());
  result.address(boost::asio::ip::address::from_string(
      sessions_[client_id]->get_addres()));

  return result;
}

std::string server::create_upload_datagram(int datagram_nr, int ack, int window,
                                   std::string DATA)
{
  return std::string("DATA " +
  std::to_string(datagram_nr) + " " +
  std::to_string(ack) + " " +
  std::to_string(window) +
  "\n" + DATA);
}


///  ********************  FREE FUNCTION ( S )  **********//
/// ////////////////////////////////////////////////////////

void program_options(int argc, char* argv[])
{
  /// PROGRAM_OPTIONS
  // Declare the supported options.
  po::options_description desc("Allowed options");
  desc.add_options()
  ("help", "produce help message")
  ("port,p", po::value<unsigned short>(&local_port)->default_value(PORT),
   "port to listen")
  ("fifo-size,F", po::value<unsigned int>(&local_fifo_size)->default_value(FIFO_SIZE),
   "size of the fifo queue")
  ("fifo-low,L", po::value<unsigned int>(&local_low_watermark)->default_value(FIFO_LOW_WATERMARK),
   "...")
  ("fifo-high,H", po::value<unsigned int>(&local_high_watermark)->default_value(FIFO_HIGH_WATERMARK),
   "...")
  ("buf-len,X", po::value<unsigned int>(&local_buf_len)->default_value(BUF_LEN),
   "...")
  ("tx-interval,i", po::value<unsigned int>(&local_tx_interval)->default_value(TX_INTERVAL),
   "...")
  ;

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);
  if (vm.count("help")) {
    std::cout << desc << "\n";
    exit(1);
  }
}


int main(int argc, char* argv[])
{
  try
  {
    program_options(argc, argv);
    /// let's roll!
    boost::asio::io_service io_service;
    server server(io_service);
    io_service.run();
  }
  catch (std::exception& e)
  {
    std::cerr << e.what() << std::endl;
  }
  return 0;
}