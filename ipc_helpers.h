#pragma once

#include <boost/interprocess/sync/named_mutex.hpp>
#include <boost/interprocess/sync/named_upgradable_mutex.hpp>
#include <boost/interprocess/sync/sharable_lock.hpp>

#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>

#include "ipc_common.h"

namespace ipc
{

template<class resource_t>
struct remover
{
    // cause if someone crashes with locked mutex, they will be cleaned by server on its startup
    explicit remover(string name) : name(name) { apply(); }
    ~remover()                                 { apply(); }

    remover(remover&& other)      : name(move(other.name)) {}

    void apply()
    {
        if (!name.empty())
            resource_t::remove(name.c_str());
    }

private:
    string name;
};

template<class resource_t>
struct sync_object
{
    sync_object(string name, bool owns)
        : rm_(owns ? name : "")
    {
        if (owns)
            res_ = in_place(create_only, name.c_str());
        else
            res_ = in_place(open_only  , name.c_str());
    }

    operator resource_t&()
    {
        return *res_;
    }

    resource_t& get()
    {
        return *res_;
    }

private:
    remover <resource_t> rm_;
    optional<resource_t> res_;
};

struct shared_buffer
    : noncopyable
{
    shared_buffer(string name, size_t size, bool owns)
        : remover_  (owns ? name : "")
        , size_     (size)
    {
        if (owns)
            create_mem(name);
        else
            open_mem  (name);
    }

    shared_buffer(shared_buffer&& other)
        : remover_(move(other.remover_))
        , mem_    (move(other.mem_    ))
        , reg_    (move(other.reg_    ))
    {
    }

    void* pointer() const
    {
        return reg_.get_address();
    }

    void clear()
    {
        memset(pointer(), 0, size_);
    }

private:
    void create_mem(string name)
    {
        shared_memory_object mem(create_only, name.c_str(), read_write);
        mem.truncate(size_);

        mapped_region reg(mem, read_write);
        memset(reg.get_address(), 0, size_);

        mem_ = move(mem);
        reg_ = move(reg);
    }

    void open_mem(string name)
    {
        shared_memory_object mem(open_only, name.c_str(), read_write);
        mapped_region reg(mem, read_write);

        mem_ = move(mem);
        reg_ = move(reg);
    }

private:
    remover<shared_memory_object>   remover_;
    size_t                          size_;
    shared_memory_object            mem_;
    mapped_region                   reg_;
};


template<class resource_t>
resource_t create_sync(string name, bool owns)
{
    if (owns)
        return resource_t(create_only, name.c_str());
    else
        return resource_t(open_only  , name.c_str());
}

template<class resource_t>
bool create(optional<resource_t>& result)
{
    try
    {
        result = in_place(true);
        return true;
    }
    catch(interprocess_exception const&)
    {
        return false;
    }
}

template<class resource_t>
bool open(optional<resource_t>& result)
{
    try
    {
        result = in_place(false);
        return true;
    }
    catch(interprocess_exception const&)
    {
        return false;
    }
}

} // namespace ipc
