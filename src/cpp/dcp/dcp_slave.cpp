#include "cse/dcp/dcp_slave.hpp"

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>

#include <ctime>
#include <iostream>
#include <string>
#include <utility>

namespace cse
{

using boost::asio::ip::udp;


dcp_socket::dcp_socket(boost::asio::io_context& io_context, std::uint16_t localPort, udp::endpoint remoteEndpoint)
    : socket_(io_context, udp::v4())
    , remoteEndpoint_(std::move(remoteEndpoint))
{
    try {
        socket_.bind(udp::endpoint(udp::v4(), localPort));
    } catch (std::exception) {
        std::cout << "Error binding!!!" << std::endl;
    }
}


void dcp_socket::send_pdu(const pdu& data, std::function<void(const boost::system::error_code&)> onSent)
{
    onSent_ = onSent;
    sendBuffer_ = data;
    socket_.async_send_to(
        boost::asio::buffer(&sendBuffer_, sizeof(sendBuffer_)), remoteEndpoint_,
        boost::bind(&dcp_socket::handle_send, this,
            boost::asio::placeholders::error));
}

void dcp_socket::receive_pdu(std::function<void(const boost::system::error_code&, const pdu&)> onReceive)
{
    onReceive_ = onReceive;
    udp::endpoint remoteEndpoint;
    socket_.async_receive_from(
        boost::asio::buffer(&receiveBuffer_, sizeof(receiveBuffer_)), remoteEndpoint,
        boost::bind(&dcp_socket::handle_receive, this,
            boost::asio::placeholders::error));
}

void dcp_socket::handle_receive(const boost::system::error_code& error)
{
    onReceive_(error, receiveBuffer_);
}
void dcp_socket::handle_send(const boost::system::error_code& error)
{
    onSent_(error);
}


dcp_server::dcp_server(boost::asio::io_context& io_context)
    : socket_(io_context, 1024, udp::endpoint(boost::asio::ip::make_address_v4("127.0.0.1"), 1025))
{
    std::cout << "dcp_server constructor" << std::endl;
    socket_.receive_pdu([this](const boost::system::error_code& ec, const pdu& p) {
        handle_receive(ec, p);
    });
}

void dcp_server::handle_receive(
    const boost::system::error_code& error,
    const pdu& p)
{
    if (!error) {
        std::cout << "Received message" << std::endl;
        if (p.type_id == 0x01) {
            std::cout << p.stc.pdu_seq_id << std::endl;
        }
    } else {
        std::cout << "Error when receiving!" << std::endl;
    }
}


dcp_client::dcp_client(boost::asio::io_context& io_context)
    : socket_(io_context, 1025, udp::endpoint(boost::asio::ip::make_address_v4("127.0.0.1"), 1024))
{
    std::cout << "dcp_client constructor" << std::endl;
    start_send();
}

void dcp_client::start_send()
{
    std::cout << "Starting to send pdu" << std::endl;
    pdu data;
    data.type_id = 0x01;
    data.stc.pdu_seq_id = 8;
    data.stc.receiver = 0x05;

    socket_.send_pdu(data, [this](const boost::system::error_code& ec) {
        handle_send(ec);
    });
}

void dcp_client::handle_send(const boost::system::error_code& error)
{
    if (error) {
        std::cout << "Error sending!" << error.message() << std::endl;
    }
    std::cout << "Sent" << std::endl;
}


} // namespace cse