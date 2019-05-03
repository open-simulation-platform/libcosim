/**
 *  \file
 *  \brief DCP Slave.
 */
#ifndef CSECORE_DCP_SLAVE_HPP
#define CSECORE_DCP_SLAVE_HPP

#include <boost/asio.hpp>
#include <boost/endian/arithmetic.hpp>

namespace cse
{

using boost::asio::ip::udp;

#pragma pack(1)
struct pdu
{
    boost::endian::little_uint8_t type_id;
    union {
        struct
        {
            boost::endian::little_uint16_t pdu_seq_id;
            boost::endian::little_uint8_t receiver;
        } stc;
        struct
        {
            int foo;
        } dat;
    };
};

class dcp_socket
{
public:
    dcp_socket(boost::asio::io_context& io_context, std::uint16_t localPort, udp::endpoint remoteEndpoint);
    void send_pdu(const pdu& data, std::function<void(const boost::system::error_code&)> onSent);
    void receive_pdu(std::function<void(const boost::system::error_code&, const pdu&)> onReceive);

private:
    void handle_receive(const boost::system::error_code& error);
    void handle_send(const boost::system::error_code& error);

    udp::socket socket_;
    udp::endpoint remoteEndpoint_;
    std::function<void(const boost::system::error_code&)> onSent_;
    std::function<void(const boost::system::error_code&, const pdu&)> onReceive_;
    pdu receiveBuffer_;
    pdu sendBuffer_;
};


class dcp_server
{
public:
    dcp_server(boost::asio::io_context& io_context);

private:
    void handle_receive(const boost::system::error_code& error, const pdu& p);

    dcp_socket socket_;
};

class dcp_client
{

public:
    dcp_client(boost::asio::io_context& io_context);

private:
    void start_send();
    void handle_send(const boost::system::error_code& /*error*/);

    dcp_socket socket_;
};

} // namespace cse
#endif //CSECORE_DCP_SLAVE_HPP
