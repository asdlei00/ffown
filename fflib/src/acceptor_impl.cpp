#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <vector>
using namespace std;

#include "epoll_i.h"
#include "detail/acceptor_impl.h"
#include "detail/socket_impl.h"
#include "detail/socket_controller_impl.h"
#include "utility/strtool.h"
#include "utility/socket_op.h"

acceptor_impl_t::acceptor_impl_t(epoll_i* e_):
    m_listen_fd(-1),
    m_epoll(e_)
{
}

acceptor_impl_t::~acceptor_impl_t()
{
    this->close();
}

int acceptor_impl_t::open(const string& address_)
{
    //! example tcp://192.168.1.2:8080
    vector<string> vt;
    strtool::split(address_, vt, "://");
    assert(vt.size() == 2);

    vector<string> vt2;
    strtool::split(vt[1], vt2, ":");
    assert(vt2.size() == 2);

    struct addrinfo hints;
    bzero(&hints, sizeof(struct addrinfo) );
    hints.ai_family      = AF_UNSPEC;
    hints.ai_socktype  = SOCK_STREAM;
    hints.ai_flags        = AI_PASSIVE;

    int ret =0, yes = 1;
    struct addrinfo *res;

    const char* host = NULL;
    if (vt2[0] != "*") host = vt2[0].c_str();

    if ((ret = getaddrinfo(host, vt2[1].c_str(), &hints, &res)) != 0) 
    {
        fprintf(stderr, "acceptor_impl_t::open getaddrinfo: %s, address_=<%s>\n", gai_strerror(ret), address_.c_str());
        return -1;
    }

    if ((m_listen_fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol)) == -1)
    {
        perror("acceptor_impl_t::open when socket");
        return -1;
    }

    if (::setsockopt(m_listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
    {
        perror("acceptor_impl_t::open when setsockopt");
        return -1;
    }

    if (::bind(m_listen_fd, res->ai_addr, res->ai_addrlen) == -1)
    {
        fprintf(stderr, "acceptor_impl_t::open when bind: %s, address_=<%s>\n", strerror(errno), address_.c_str());
        return -1;
    }

    socket_op_t::set_nonblock(m_listen_fd);
    if (::listen(m_listen_fd, LISTEN_BACKLOG) == -1)
    {
        perror("acceptor_impl_t::open when listen");
        return -1;
    }

    ::freeaddrinfo(res);

    return m_epoll->register_fd(this);
}

void acceptor_impl_t::close()
{
    ::close(m_listen_fd);
}

int acceptor_impl_t::handle_epoll_read()
{
    struct sockaddr_storage addr;
    socklen_t addrlen = sizeof(addr);

    int new_fd = -1;
    do
    {
        if ((new_fd = ::accept(m_listen_fd, (struct sockaddr *)&addr, &addrlen)) == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }
            else if (errno == EWOULDBLOCK)
            {
                return 0;
            }
            perror("accept");
            return -1;
        }

        socket_i* socket = create_socket(new_fd);
        socket->open();
    } while (true);
    return 0;
}

socket_i* acceptor_impl_t::create_socket(int new_fd_)
{
    return new socket_impl_t(m_epoll, new socket_controller_impl_t(), new_fd_);
}