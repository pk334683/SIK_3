#include <string>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>
#include <boost/concept_check.hpp>

#include "client_session.h"

using boost::asio::ip::tcp;

///                 TCP_CONNECTION FUNCTIONS            //
/// //////////////////////////////////////////////////////

typedef boost::shared_ptr<tcp_connection> pointer;


tcp_connection::tcp_connection(boost::asio::io_service& io_service)
: socket_(io_service),
  message_(),
  broken_(false)
{

}


tcp::socket& tcp_connection::socket()
{
  return socket_;
}


void tcp_connection::start(int id)
{
  address_ = socket_.remote_endpoint().address().to_string();
  port_tcp_ = socket_.remote_endpoint().port();

  message_ = ("CLIENT " + std::to_string(id) + "\n");

  boost::asio::async_write(socket_, boost::asio::buffer(message_),
    boost::bind(&tcp_connection::handle_write,
      shared_from_this(),
      boost::asio::placeholders::error,
      boost::asio::placeholders::bytes_transferred));
}


void tcp_connection::send_raport(std::string raport)
{
  if( broken_ )
  {
    std::cerr << "Error send_raport - socket is broken: " << get_addres() << ":" << get_port_tcp() << "\n";
    return;
  }
  try
  {
    boost::asio::write(socket_, boost::asio::buffer(raport));
  }
  catch (std::exception& e)
  {
    stop();
  }
}


void tcp_connection::handle_write(const boost::system::error_code& er,
                  size_t /*bytes_transferred*/)
{
  if( er )
  {
    stop();
    std::cerr << er.message() << std::endl;
  }
}


void tcp_connection::stop()
{
  broken_ = true;
  socket_.close();
}


bool tcp_connection::broken()
{
  return broken_;
}
