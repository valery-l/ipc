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

        , queue_(mem_.pointer(), buffer_size)
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

        while (!queue_.empty())
        {
            if (queue_.top().header().id == 0) // clearing, if all clients have read
                queue_.pop();
            else
                break;
        }
    }

    bool write(id_t id, bytes_ptr data) // false if no more place to write
    {
        return queue_.push(id, data);
    }
};

struct client2server_buffer
    : io_buffer
{
    typedef function<void (id_t, bytes_ptr)>  on_recv_f;

    client2server_buffer(string name, bool owns, named_mutex& mutex)
        : io_buffer(name, owns, mutex)
    {
    }

    void read(on_recv_f const& recv) // by server
    {
        while(!queue_.empty())
        {
            message& top = queue_.top();

            if (recv)
                recv(top.header().id, data2bytes(top.data(), top.header().size));

            queue_.pop();
        }
    }

    bool write(id_t id, bytes_ptr data) // false if no more place to write
    {
        return queue_.push(id, data);
    }
};


struct server2client
{
    typedef named_upgradable_mutex lock_type;

    server2client(bool owns)
        : mutex     ("simex.ipc.s2c_mutex"  , owns)
        , condvar   ("simex.ipc.s2c_condvar", owns)
        , buffer    ("simex.ipc.s2c_buffer" , owns, mutex)
    {
    }

public:
    sync_object<lock_type>          mutex;
    sync_object<named_condition>    condvar;
    server2client_buffer            buffer;
};

struct client2server
{
    typedef named_mutex lock_type;

    client2server(bool owns)
        : mutex     ("simex.ipc.s2c_mutex"  , owns)
        , condvar   ("simex.ipc.s2c_condvar", owns)
        , buffer    ("simex.ipc.c2s_buffer" , owns, mutex)
    {
    }

public:
    sync_object<lock_type>          mutex;
    sync_object<named_condition>    condvar;
    client2server_buffer            buffer;
};

}
