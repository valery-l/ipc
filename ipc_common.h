#pragma once

namespace ipc
{

using namespace boost::interprocess;
using namespace boost::posix_time;
using namespace boost::asio;

inline ptime time_after_ms(size_t ms)
{
    return microsec_clock::universal_time() + milliseconds(ms);
}


const size_t wait_timeout_ms = 200; // in milliseconds

// if current time and last time of client (server) differs by more than disconnect_timeout, it's considered as disconnected
const size_t disconnect_timeout  = mutex_timeout * 5;

const size_t buffer_size = 20 * 1024 * 1024;

}
