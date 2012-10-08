#pragma once
#include "ipc_common.h"
#include "cyclic_buffer.h"
#include "common/data_wrapper.h"
#include "sync_objects.h"

#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>

namespace ipc
{

struct shared_buffer
{
    typedef cyclic_queue::id_t id_t;

    shared_buffer(string name, bool auto_remove)
        : mem_  (name, auto_remove)
        , reg_  (*mem_, read_write)
        , queue_(reg_.get_address())
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
    auto_rm<shared_memory_object>   mem_;
    mapped_region                   reg_;
    cyclic_queue                    queue_;
};

struct server2client_buffer
    : shared_buffer
{
    typedef function<void (bytes_ptr)>  on_recv_f;

    server2client_buffer(string name, bool auto_remove)
        : shared_buffer(name, auto_remove)
    {
    }

    void read(id_t bit_mask, on_recv_f const& recv) // by client
    {
        for (cyclic_queue::iterator it = queue_.begin(); it != queue_.end(); ++it)
        {
            if ((it->header().id & bit_mask) == bit_mask)
            {
                if (recv)
                    recv(it->data());

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
    : shared_buffer
{
    typedef function<void (id_t, bytes_ptr)>  on_recv_f;

    server2client_buffer(string name, bool auto_remove)
        : shared_buffer(name, auto_remove)
    {
    }

    void read(id_t bit_mask, on_recv_f const& recv) // by server
    {
        while(!queue_.empty())
        {
            message& top = queue_.top();

            if (recv)
                recv(top.data(), top.header().id);

            queue_.pop();
        }
    }

    bool write(id_t id, bytes_ptr data) // false if no more place to write
    {
        queue_.push(id, data);
    }
};


}
