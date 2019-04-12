#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/endian/arithmetic.hpp>
#include <boost/shared_ptr.hpp>

#include <ctime>
#include <iostream>
#include <string>


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
    dcp_socket(boost::asio::io_context& io_context, std::uint16_t localPort, const udp::endpoint& remoteEndpoint)
        : socket_(io_context, udp::endpoint(udp::v4(), localPort))
        , remoteEndpoint_(remoteEndpoint)
    {
    }

    void send_pdu(const pdu& data, std::function<void(const boost::system::error_code&)> onSent)
    {
        onSent_ = onSent;
        sendBuffer_ = data;
        socket_.async_send_to(
            boost::asio::buffer(&sendBuffer_, sizeof(sendBuffer_)), remoteEndpoint_,
            boost::bind(&dcp_socket::handle_send, this,
                boost::asio::placeholders::error));
    }

    void receive_pdu(std::function<void(const boost::system::error_code&, const pdu&)> onReceive)
    {
        onReceive_ = onReceive;
        udp::endpoint remoteEndpoint;
        socket_.async_receive_from(
            boost::asio::buffer(&receiveBuffer_, sizeof(receiveBuffer_)), remoteEndpoint,
            boost::bind(&dcp_socket::handle_receive, this,
                boost::asio::placeholders::error));
    }

private:
    void handle_receive(const boost::system::error_code& error)
    {
        onReceive_(error, receiveBuffer_);
    }
    void handle_send(const boost::system::error_code& error)
    {
        onSent_(error);
    }

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
    dcp_server(boost::asio::io_context& io_context)
        : socket_(io_context, 13, udp::endpoint(boost::asio::ip::make_address_v4("127.0.0.1"), 14))
    {
        socket_.receive_pdu([this](const boost::system::error_code& ec, const pdu& p) {
            handle_receive(ec, p);
        });
    }

private:
    void handle_receive(
        const boost::system::error_code& error,
        const pdu& p)
    {
        if (!error) {
            std::cout << "Received message" << std::endl;
            if (p.type_id == 0x01) {
                std::cout << p.stc.pdu_seq_id << std::endl;
            }
        }
    }

    dcp_socket socket_;
};

class dcp_client
{

public:
    dcp_client(boost::asio::io_context& io_context)
        : socket_(io_context, 14, udp::endpoint(boost::asio::ip::make_address_v4("127.0.0.1"), 13))
    {
        std::cout << "constructor" << std::endl;
        start_send();
    }

private:
    void start_send()
    {
        pdu data;
        data.type_id = 0x01;
        data.stc.pdu_seq_id = 8;
        data.stc.receiver = 0x05;

        socket_.send_pdu(data, [this](const boost::system::error_code& ec) {
            handle_send(ec);
        });
    }

    void handle_send(const boost::system::error_code& /*error*/)
    {
        std::cout << "Sent" << std::endl;
    }

    dcp_socket socket_;
};

int main()
{
    try {
        boost::asio::io_context io_context;
        dcp_server server(io_context);
        dcp_client client(io_context);
        io_context.run();
    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
    }

    return 0;
}
