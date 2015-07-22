#include "mbed.h"
#include "test_env.h"
#include "mbed-net-socket-abstract/test/ctest_env.h"
#include <algorithm>
#include "mbed-net-sockets/UDPSocket.h"
#include "mbed-net-socket-abstract/socket_api.h"
#include "EthernetInterface.h"
#include "mbed-net-lwip/lwipv4_init.h"
#include "minar/minar.h"

#define CHECK(RC, STEP)       if (RC < 0) error(STEP": %d\r\n", RC)

using namespace mbed::Sockets::v1;

namespace {
    const int BUFFER_SIZE = 64;
    const int MAX_ECHO_LOOPS = 100;
    const char ASCII_MAX = '~' - ' ';

    struct s_ip_address {
        int ip_1;
        int ip_2;
        int ip_3;
        int ip_4;
    };
}

char char_rand() {
    return (rand() % ASCII_MAX) + ' ';
}

#ifndef min
#define min(A,B) \
    ((A)<(B)?(A):(B))
#endif

class UDPEchoClient {
public:
    UDPEchoClient(socket_stack_t stack) :
        _usock(stack)
    {
        _usock.setOnError(UDPSocket::ErrorHandler_t(this, &UDPEchoClient::onError));
    }
    void start_test(char * host_addr, uint16_t port)
    {
        loop_ctr = 0;
        _port = port;
        socket_error_t err = _usock.resolve(host_addr,UDPSocket::DNSHandler_t(this, &UDPEchoClient::onDNS));
        if(!TEST_EQ(err, SOCKET_ERROR_NONE)) {
            printf("MBED: Failed to resolve %s\r\n", host_addr);
            onError(&_usock, err);
        }
    }
    void onError(Socket *s, socket_error_t err) {
        (void) s;
        printf("MBED: Socket Error: %s (%d)\r\n", socket_strerror(err), err);
        minar::Scheduler::stop();
    }
    void onDNS(Socket *s, struct socket_addr sa, const char * domain)
    {
        (void) s;
        /* Extract the Socket event to read the resolved address */
        _resolvedAddr.setAddr(&sa);
        _resolvedAddr.fmtIPv6(out_buffer, sizeof(out_buffer));
        printf("MBED: Resolved %s to %s\r\n", domain, out_buffer);

        /* TODO: add support for getting AF from addr */
        socket_error_t err = _usock.open(SOCKET_AF_INET4);
        TEST_EQ(err, SOCKET_ERROR_NONE);
        /* Register the read handler */
        _usock.setOnReadable(UDPSocket::ReadableHandler_t(this, &UDPEchoClient::onRx));
        /* Send the query packet to the remote host */
        err = send_test();
        TEST_EQ(err, SOCKET_ERROR_NONE);
    }
    void onRx(Socket *s)
    {
        (void) s;
        unsigned int n = sizeof(buffer);
        socket_error_t err = _usock.recv(buffer, &n);
        TEST_EQ(err, SOCKET_ERROR_NONE);

        int rc = memcmp(buffer, out_buffer, min(BUFFER_SIZE,n));
        TEST_EQ(rc, 0);

        loop_ctr++;
        if (!rc && (loop_ctr < MAX_ECHO_LOOPS)) {
            err = send_test();
            TEST_EQ(err, SOCKET_ERROR_NONE);

        }
        if ((rc == 0) && (loop_ctr >= MAX_ECHO_LOOPS)) {
            if (notify_completion_str(TEST_RESULT(), buffer)) {
                _usock.send_to(buffer, strlen(buffer), &_resolvedAddr, _port);
                _usock.close();
                minar::Scheduler::stop();
            }
        }
    }
    bool isDone() {
        return done;
    }
    bool isError() {
        return !TEST_RESULT();
    }

protected:
    socket_error_t send_test() {
        std::generate(out_buffer, out_buffer + BUFFER_SIZE, char_rand);
        socket_error_t err = _usock.send_to(out_buffer, sizeof(BUFFER_SIZE), &_resolvedAddr, _port);
        return err;
    }
protected:
    UDPSocket _usock;
    SocketAddr _resolvedAddr;
    uint16_t _port;
    char out_buffer[BUFFER_SIZE];
    char buffer[BUFFER_SIZE];
    uint32_t loop_ctr;
    volatile bool done;
};


int main() {
    MBED_HOSTTEST_TIMEOUT(20);
    MBED_HOSTTEST_SELECT(udpecho_client_auto);
    MBED_HOSTTEST_DESCRIPTION(UDP echo client);
    MBED_HOSTTEST_START("NET_6");
    socket_error_t err = lwipv4_socket_init();
    TEST_EQ(err, SOCKET_ERROR_NONE);

    char buffer[BUFFER_SIZE] = {0};
    s_ip_address ip_addr = {0, 0, 0, 0};
    int port = 0;

    printf("MBED: UDPCllient waiting for server IP and port...\r\n");
    scanf("%d.%d.%d.%d:%d", &ip_addr.ip_1, &ip_addr.ip_2, &ip_addr.ip_3, &ip_addr.ip_4, &port);
    printf("MBED: Address received: %d.%d.%d.%d:%d\r\n", ip_addr.ip_1, ip_addr.ip_2, ip_addr.ip_3, ip_addr.ip_4, port);

    EthernetInterface eth;
    int rc = eth.init(); //Use DHCP
    CHECK(rc, "eth init");

    rc = eth.connect();
    CHECK(rc, "connect");
    printf("IP: %s\n", eth.getIPAddress());

    UDPEchoClient ec(SOCKET_STACK_LWIP_IPV4);

    printf("MBED: UDPClient IP Address is %s\r\n", eth.getIPAddress());
    sprintf(buffer, "%d.%d.%d.%d", ip_addr.ip_1, ip_addr.ip_2, ip_addr.ip_3, ip_addr.ip_4);

    {
        FunctionPointer2<void, char *, uint16_t> fp(&ec, &UDPEchoClient::start_test);
        minar::Scheduler::postCallback(fp.bind(buffer, port));
    }

    minar::Scheduler::start();

    eth.disconnect();
    MBED_HOSTTEST_RESULT(TEST_RESULT());
}
