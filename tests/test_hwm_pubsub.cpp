/*
    Copyright (c) 2007-2017 Contributors as noted in the AUTHORS file

    This file is part of libzmq, the ZeroMQ core engine in C++.

    libzmq is free software; you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License (LGPL) as published
    by the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    As a special exception, the Contributors give you permission to link
    this library with independent modules to produce an executable,
    regardless of the license terms of these independent modules, and to
    copy and distribute the resulting executable under terms of your choice,
    provided that you also meet, for each linked independent module, the
    terms and conditions of the license of that module. An independent
    module is a module which is not derived from or based on this library.
    If you modify this library, you must extend this exception to your
    version of the library.

    libzmq is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
    License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "testutil.hpp"
#include "testutil_unity.hpp"

#define SOCKET_STRING_LEN (MAX_SOCKET_STRING * 4)

void setUp ()
{
    setup_test_context ();
}

void tearDown ()
{
    teardown_test_context ();
}

int test_defaults (int send_hwm_, int msg_cnt_, const char *endpoint)
{
    size_t len = SOCKET_STRING_LEN;
    char pub_endpoint[SOCKET_STRING_LEN];

    // Set up and bind PUB socket
    void *pub_socket = test_context_socket (ZMQ_PUB);
    TEST_ASSERT_SUCCESS_ERRNO (zmq_bind (pub_socket, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_getsockopt (pub_socket, ZMQ_LAST_ENDPOINT, pub_endpoint, &len));

    // Set up and connect SUB socket
    void *sub_socket = test_context_socket (ZMQ_SUB);
    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (sub_socket, pub_endpoint));

    //set a hwm on publisher
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (pub_socket, ZMQ_SNDHWM, &send_hwm_, sizeof (send_hwm_)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (sub_socket, ZMQ_SUBSCRIBE, 0, 0));

    msleep (
      SETTLE_TIME); // give some time to background threads to perform PUB-SUB connection

    // Send until we reach "mute" state
    int send_count = 0;
    while (send_count < msg_cnt_
           && zmq_send (pub_socket, "test message", 13, ZMQ_DONTWAIT) == 13)
        ++send_count;

    TEST_ASSERT_EQUAL_INT (send_hwm_, send_count);
    msleep (SETTLE_TIME);

    // Now receive all sent messages
    int recv_count = 0;
    char dummybuff[64];
    while (13 == zmq_recv (sub_socket, &dummybuff, 64, ZMQ_DONTWAIT)) {
        ++recv_count;
    }

    TEST_ASSERT_EQUAL_INT (send_hwm_, recv_count);

    // Clean up
    test_context_socket_close (sub_socket);
    test_context_socket_close (pub_socket);

    return recv_count;
}

int receive (void *socket_)
{
    int recv_count = 0;
    // Now receive all sent messages
    while (0 == zmq_recv (socket_, NULL, 0, 0)) {
        ++recv_count;
    }

    return recv_count;
}

int test_blocking (int send_hwm_, int msg_cnt_, const char *endpoint)
{
    size_t len = SOCKET_STRING_LEN;
    char pub_endpoint[SOCKET_STRING_LEN];

    // Set up bind socket
    void *pub_socket = test_context_socket (ZMQ_PUB);
    TEST_ASSERT_SUCCESS_ERRNO (zmq_bind (pub_socket, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_getsockopt (pub_socket, ZMQ_LAST_ENDPOINT, pub_endpoint, &len));

    // Set up connect socket
    void *sub_socket = test_context_socket (ZMQ_SUB);
    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (sub_socket, pub_endpoint));

    //set a hwm on publisher
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (pub_socket, ZMQ_SNDHWM, &send_hwm_, sizeof (send_hwm_)));
    int wait = 1;
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (pub_socket, ZMQ_XPUB_NODROP, &wait, sizeof (wait)));
    int timeout_ms = 10;
    TEST_ASSERT_SUCCESS_ERRNO (zmq_setsockopt (
      sub_socket, ZMQ_RCVTIMEO, &timeout_ms, sizeof (timeout_ms)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (sub_socket, ZMQ_SUBSCRIBE, 0, 0));

    msleep (SETTLE_TIME);

    // Send until we block
    int send_count = 0;
    int recv_count = 0;
    while (send_count < msg_cnt_) {
        const int rc = zmq_send (pub_socket, NULL, 0, ZMQ_DONTWAIT);
        if (rc == 0) {
            ++send_count;
        } else if (-1 == rc) {
            // if the PUB socket blocks due to HWM, errno should be EAGAIN:
            TEST_ASSERT_EQUAL_INT (EAGAIN, errno);
            recv_count += receive (sub_socket);
        }
    }

    msleep (2 * SETTLE_TIME); // required for TCP transport
    recv_count += receive (sub_socket);
    TEST_ASSERT_EQUAL_INT (send_count, recv_count);

    // Clean up
    test_context_socket_close (sub_socket);
    test_context_socket_close (pub_socket);

    return recv_count;
}

// hwm should apply to the messages that have already been received
// with hwm 11024: send 9999 msg, receive 9999, send 1100, receive 1100
void test_reset_hwm ()
{
    const int first_count = 9999;
    const int second_count = 1100;
    int hwm = 11024;
    char my_endpoint[SOCKET_STRING_LEN];

    // Set up bind socket
    void *pub_socket = test_context_socket (ZMQ_PUB);
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (pub_socket, ZMQ_SNDHWM, &hwm, sizeof (hwm)));
    bind_loopback_ipv4 (pub_socket, my_endpoint, MAX_SOCKET_STRING);

    // Set up connect socket
    void *sub_socket = test_context_socket (ZMQ_SUB);
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (sub_socket, ZMQ_RCVHWM, &hwm, sizeof (hwm)));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (sub_socket, my_endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (sub_socket, ZMQ_SUBSCRIBE, 0, 0));

    msleep (SETTLE_TIME);

    // Send messages
    int send_count = 0;
    while (send_count < first_count
           && zmq_send (pub_socket, NULL, 0, ZMQ_DONTWAIT) == 0)
        ++send_count;
    TEST_ASSERT_EQUAL_INT (first_count, send_count);

    msleep (SETTLE_TIME);

    // Now receive all sent messages
    int recv_count = 0;
    while (0 == zmq_recv (sub_socket, NULL, 0, ZMQ_DONTWAIT)) {
        ++recv_count;
    }
    TEST_ASSERT_EQUAL_INT (first_count, recv_count);

    msleep (SETTLE_TIME);

    // Send messages
    send_count = 0;
    while (send_count < second_count
           && zmq_send (pub_socket, NULL, 0, ZMQ_DONTWAIT) == 0)
        ++send_count;
    TEST_ASSERT_EQUAL_INT (second_count, send_count);

    msleep (SETTLE_TIME);

    // Now receive all sent messages
    recv_count = 0;
    while (0 == zmq_recv (sub_socket, NULL, 0, ZMQ_DONTWAIT)) {
        ++recv_count;
    }
    TEST_ASSERT_EQUAL_INT (second_count, recv_count);

    // Clean up
    test_context_socket_close (sub_socket);
    test_context_socket_close (pub_socket);
}

void test_tcp ()
{
    // send 1000 msg on hwm 1000, receive 1000, on TCP transport
    TEST_ASSERT_EQUAL_INT (1000,
                           test_defaults (1000, 1000, "tcp://127.0.0.1:*"));

    // send 100 msg on hwm 100, receive 100
    TEST_ASSERT_EQUAL_INT (100, test_defaults (100, 100, "tcp://127.0.0.1:*"));

    // send 6000 msg on hwm 2000, drops above hwm, only receive hwm:
    TEST_ASSERT_EQUAL_INT (6000,
                           test_blocking (2000, 6000, "tcp://127.0.0.1:*"));
}

void test_inproc ()
{
    TEST_ASSERT_EQUAL_INT (1000, test_defaults (1000, 1000, "inproc://a"));
    TEST_ASSERT_EQUAL_INT (100, test_defaults (100, 100, "inproc://b"));
    TEST_ASSERT_EQUAL_INT (6000, test_blocking (2000, 6000, "inproc://c"));
}

#ifndef ZMQ_HAVE_WINDOWS

void test_ipc ()
{
    TEST_ASSERT_EQUAL_INT (1000, test_defaults (1000, 1000, "ipc://*"));
    TEST_ASSERT_EQUAL_INT (100, test_defaults (100, 100, "ipc://*"));
    TEST_ASSERT_EQUAL_INT (6000, test_blocking (2000, 6000, "ipc://*"));
}

#endif

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();

    // repeat the test for both TCP, INPROC and IPC transports:

    RUN_TEST (test_tcp);
    RUN_TEST (test_inproc);
#ifndef ZMQ_HAVE_WINDOWS
    RUN_TEST (test_ipc);
#endif
    RUN_TEST (test_reset_hwm);
    return UNITY_END ();
}
