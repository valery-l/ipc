#pragma once

#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>

#include "live_table.h"

namespace ipc
{
    template<class live_status_provider>
    struct shared_connected_state
    {
        typedef
            typename live_status_provider::live_status
            live_status;

        shared_connected_state()
            : connected_(false)
        {
        }

        live_status set_connected(live_status const& connected) // returns previous connection state
        {
            boost::mutex::scoped_lock lock(mutex_);
            live_status was_connected = connected_;

            connected_ = connected;

            if (!was_connected && connected)
                condvar_.notify_one();

            return was_connected;
        }

        live_status is_connected()
        {
            boost::mutex::scoped_lock lock(mutex_);
            return connected_;
        }

        void wait_for_connected()
        {
            boost::mutex::scoped_lock lock(mutex_);

            while (!connected_)
                condvar_.wait(lock);
        }

    private:
        live_status                 connected_;
        boost::mutex                mutex_;
        boost::condition_variable   condvar_;
    };


} // namespace ipc
