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


}
