#pragma once

#include <boost/interprocess/sync/named_mutex.hpp>
#include <boost/interprocess/sync/named_upgradable_mutex.hpp>
#include <boost/interprocess/sync/sharable_lock.hpp>

namespace ipc
{

static const size_t wait_timeout_ms = 300; // in milliseconds

template<class resource_t>
struct auto_rm
{
    auto_rm(const char* name, bool auto_remove)
        : remover_  (auto_remove ? name : "")
        , resource_ (open_or_create, name)
    {
    }

    resource_t& get()       { return resource_; }
    resource_t& operator* (){ return get();     }
    resource_t* operator->(){ return &resource_;}

    struct remover
    {
        remover(string name) : name(name)   { apply(); }
       ~remover()                           { apply(); }

        void apply() { if (!name.empty()) resource_t::remove(name.c_str()); }
        string name;
    };

private:
    remover     remover_ ; // should be invoked before resource constructor, and after its destructor
    resource_t  resource_;
};

struct table_sync
{
    typedef named_mutex lock_type;

    table_sync(bool auto_remove)
        : mutex("simex.ipc.table_mutex", auto_remove)
    {
    }

    auto_rm<lock_type> mutex;
};

struct server2client
{
    typedef named_upgradable_mutex lock_type;

    server2client(bool auto_remove)
        : mutex     ("simex.ipc.s2c_mutex"  , auto_remove)
        , condvar   ("simex.ipc.s2c_condvar", auto_remove)
    {
    }

    auto_rm<lock_type>       mutex;
    auto_rm<named_condition> condvar;
};

struct client2server
{
    typedef named_mutex lock_type;

    client2server()
        : mutex     ("simex.ipc.c2s_mutex"  , auto_remove)
        , condvar   ("simex.ipc.c2s_condvar", auto_remove)
    {
    }

    auto_rm<lock_type>       mutex;
    auto_rm<named_condition> condvar;
};

} // namespace ipc
