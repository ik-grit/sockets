#include "mbed.h"
#include "test_env.h"
#include "mbed-net-sockets/TCPStream.h"
#include "mbed-net-socket-abstract/test/ctest_env.h"
#include "mbed-net-lwip/lwipv4_init.h"
#include "EthernetInterface.h"
#include "minar/minar.h"

struct s_ip_address {
    int ip_1;
    int ip_2;
    int ip_3;
    int ip_4;
};

char out_buffer[] = "Hello World\n";
char buffer[256];

using namespace mbed::Sockets::v1;

EthernetInterface eth;

class TCPEchoClient;

class TCPEchoClient {
public:
    TCPEchoClient(socket_stack_t stack) :
        _stream(stack), _done(false), _disconnected(true)
        {
            _stream.setOnError(TCPStream::ErrorHandler_t(this, &TCPEchoClient::onError));
        }
    void onError(Socket *s, socket_error_t err) {
        (void) s;
        printf("MBED: Socket Error: %s (%d)\r\n", socket_strerror(err), err);
        _done = true;
        minar::Scheduler::stop();
    }
    void start_test(char * host_addr, uint16_t port)
    {
        _port = port;
        _done = false;
        _disconnected = true;
        socket_error_t err = _stream.resolve(host_addr,TCPStream::DNSHandler_t(this, &TCPEchoClient::onDNS));
        if (!TEST_EQ(err, SOCKET_ERROR_NONE)) {
            printf("MBED: TCPClient unable to connect to %s:%d" NL, buffer, port);
            onError(&_stream, err);
        }
    }
    void onDNS(Socket *s, struct socket_addr sa, const char* domain)
    {
        (void) s;
        _resolvedAddr.setAddr(&sa);
        /* TODO: add support for getting AF from addr */
        /* Open the socket */
        _resolvedAddr.fmtIPv6(buffer, sizeof(buffer));
        printf("MBED: Resolved %s to %s\r\n", domain, buffer);
        socket_error_t err = _stream.open(SOCKET_AF_INET4);
        TEST_EQ(err, SOCKET_ERROR_NONE);
        /* Register the read handler */
        _stream.setOnReadable(TCPStream::ReadableHandler_t(this, &TCPEchoClient::onRx));
        _stream.setOnSent(TCPStream::SentHandler_t(this, &TCPEchoClient::onSent));
        _stream.setOnDisconnect(TCPStream::DisconnectHandler_t(this, &TCPEchoClient::onDisconnect));
        /* Send the query packet to the remote host */
        err = _stream.connect(_resolvedAddr, _port, TCPStream::ConnectHandler_t(this,&TCPEchoClient::onConnect));
        if(!TEST_EQ(err, SOCKET_ERROR_NONE)) {
            printf("MBED: Expected %d, got %d (%s)\r\n", SOCKET_ERROR_NONE, err, socket_strerror(err));
            onError(&_stream, err);
        }
    }
    void onConnect(TCPStream *s)
    {
        (void) s;
        _disconnected = false;
        _unacked += sizeof(out_buffer) - 1;
        socket_error_t err = _stream.send(out_buffer, sizeof(out_buffer) - 1);

        TEST_EQ(err, SOCKET_ERROR_NONE);
    }
    void onRx(Socket* s)
    {
        (void) s;
        size_t n = sizeof(buffer)-1;
        socket_error_t err = _stream.recv(buffer, &n);
        TEST_EQ(err, SOCKET_ERROR_NONE);
        buffer[n] = 0;
        char out_success[] = "{{success}}\n{{end}}\n";
        char out_failure[] = "{{failure}}\n{{end}}\n";
        int rc;
        if (n > 0)
        {
            buffer[n] = '\0';
            printf("%s\r\n", buffer);
            rc = strncmp(out_buffer, buffer, sizeof(out_buffer) - 1);
            if (TEST_EQ(rc, 0)) {
                _unacked += sizeof(out_success) - 1;
                err = _stream.send(out_success, sizeof(out_success) - 1);
                _done = true;
                minar::Scheduler::stop();
                TEST_EQ(err, SOCKET_ERROR_NONE);
            }
        }
        if (!_done) {
            _unacked += sizeof(out_failure) - 1;
            err = _stream.send(out_failure, sizeof(out_failure) - 1);
            _done = true;
            TEST_EQ(err, SOCKET_ERROR_NONE);
        }
    }
    void onSent(Socket *s, uint16_t nbytes)
    {
        (void) s;
        _unacked -= nbytes;
        if (_done && (_unacked == 0)) {
            _stream.close();
        }
    }
    void onDisconnect(TCPStream *s)
    {
        (void) s;
        _disconnected = true;
    }
protected:
    TCPStream _stream;
    SocketAddr _resolvedAddr;
    uint16_t _port;
    volatile bool _done;
    volatile bool _disconnected;
    volatile size_t _unacked;
};

int main() {
    TCPEchoClient *client;
    char buffer[256];
    int port;

    MBED_HOSTTEST_TIMEOUT(20);
    MBED_HOSTTEST_SELECT(tcpecho_client_auto);
    MBED_HOSTTEST_DESCRIPTION(TCP echo client);
    MBED_HOSTTEST_START("NET_4");
    socket_error_t err = lwipv4_socket_init();
    TEST_EQ(err, SOCKET_ERROR_NONE);

    memset(buffer, 0, sizeof(buffer));
    port = 0;
    s_ip_address ip_addr = {0, 0, 0, 0};
    printf("TCPClient waiting for server IP and port..." NL);
    scanf("%d.%d.%d.%d:%d", &ip_addr.ip_1, &ip_addr.ip_2, &ip_addr.ip_3, &ip_addr.ip_4, &port);
    printf("Address received:%d.%d.%d.%d:%d" NL, ip_addr.ip_1, ip_addr.ip_2, ip_addr.ip_3, ip_addr.ip_4, port);

    eth.init(); //Use DHCP
    eth.connect();

    printf("TCPClient IP Address is %s" NL, eth.getIPAddress());
    sprintf(buffer, "%d.%d.%d.%d", ip_addr.ip_1, ip_addr.ip_2, ip_addr.ip_3, ip_addr.ip_4);
    client = new TCPEchoClient(SOCKET_STACK_LWIP_IPV4);

    {
        FunctionPointer2<void, char *, uint16_t> fp(client, &TCPEchoClient::start_test);
        minar::Scheduler::postCallback(fp.bind(buffer, port));
    }

    minar::Scheduler::start();

    delete client;
    eth.disconnect();
    MBED_HOSTTEST_RESULT(TEST_RESULT());

}
