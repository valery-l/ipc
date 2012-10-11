#pragma once
#include "ipc_common.h"
#include "cyclic_queue.h"
#include "common/data_wrapper.h"
#include "ipc_helpers.h"

namespace ipc
{

struct io_buffer
{
    typedef cyclic_queue::id_t id_t;

    template<class mutex_t>
    io_buffer(string name, bool owns, mutex_t& mutex)
        : mem_  (owns
                ? move(create_buffer(name, mutex))
                : move(open_buffer  (name)))

        , queue_(mem_.pointer())
    {
    }

    bool empty() const
    {
        return queue_.empty();
    }

    void clear()
    {
        queue_.clear();
    }

protected:
    shared_buffer mem_;
    cyclic_queue  queue_;

private:
    template<class mutex_t>
    shared_buffer create_buffer(string name, mutex_t& mutex)
    {
        scoped_lock<mutex_t> lock(mutex, time_after_ms(wait_timeout_ms));
        if (lock.owns())
        {
            shared_buffer sb(name, buffer_size, true);
            sb.clear();

            return sb;
        }
        else
            throw std::runtime_error("cannot lock newly created mutex");
    }

    static shared_buffer open_buffer(string name)
    {
        return shared_buffer(name, buffer_size, false);
    }
};

struct server2client_buffer
    : io_buffer
{
    typedef function<void (bytes_ptr)>  on_recv_f;

    server2client_buffer(string name, bool owns, named_upgradable_mutex& mutex)
        : io_buffer(name, owns, mutex)
    {
    }

    void read(id_t bit_mask, on_recv_f const& recv) // by client
    {
        for (cyclic_queue::iterator it = queue_.begin(); it != queue_.end(); ++it)
        {
            if ((it->header().id & bit_mask) == bit_mask)
            {
                if (recv)
                    recv(data2bytes(it->data(), it->header().size));

                it->header().id &= ~bit_mask;
            }
        }
    }

    bool write(id_t id, bytes_ptr data) // false if no more place to write
    {
        queue_.push(id, data);
    }
};

struct client2server_buffer
    : io_buffer
{
    typedef function<void (bytes_ptr, id_t)>  on_recv_f;

    client2server_buffer(string name, bool owns, named_mutex& mutex)
        : io_buffer(name, owns, mutex)
    {
    }

    void read(id_t bit_mask, on_recv_f const& recv) // by server
    {
        while(!queue_.empty())
        {
            message& top = queue_.top();

            if (recv)
                recv(data2bytes(top.data(), top.header().size), top.header().id);

            queue_.pop();
        }
    }

    bool write(id_t id, bytes_ptr data) // false if no more place to write
    {
        queue_.push(id, data);
    }
};

struct server2client
{
    typedef named_upgradable_mutex lock_type;

    server2client(bool owns)
        : mutex_rm  (owns ? "simex.ipc.s2c_mutex"   : "")
        , condvar_rm(owns ? "simex.ipc.s2c_condvar" : "")

        , mutex     ("simex.ipc.s2c_mutex"  , owns)
        , condvar   ("simex.ipc.s2c_condvar", owns)
        , buffer    ("simex.ipc.s2c_buffer" , owns, *mutex)
    {
    }

private:
    remover<lock_type>          mutex_rm;
    remover<named_condition>    condvar_rm;

public:
    lock_type                   mutex;
    named_condition             condvar;
    server2client_buffer        buffer;
};

struct client2server
{
    typedef named_mutex lock_type;

    client2server(bool owns)
        : mutex_rm  (owns ? "simex.ipc.c2s_mutex"   : "")
        , condvar_rm(owns ? "simex.ipc.c2s_condvar" : "")

        , mutex     ("simex.ipc.c2s_mutex"  , owns)
        , condvar   ("simex.ipc.c2s_condvar", owns)
        , buffer    ("simex.ipc.c2s_buffer" , owns, *mutex)
    {
    }

private:
    remover<lock_type>          mutex_rm;
    remover<named_condition>    condvar_rm;

public:
    lock_type               mutex;
    named_condition         condvar;
    client2server_buffer    buffer;
};

}
