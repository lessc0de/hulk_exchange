
#ifndef _hulk_exchange_match_engine_h_
#define _hulk_exchange_match_engine_h_

#include "hulk/exchange/order-events.h"
#include "hulk/core/shared_ptr.h"

namespace hulk {
namespace exchange {

class journal_thread;
class business_thread;

class match_engine
{
public:
    match_engine();

    id new_limit_order( symbol&, side, qty, px );
    id new_market_order( symbol&, side, qty );
    void cancel_order( id );

    void start();
    void join();

    hulk::shared_ptr< o_order_event_reader > get_reader();

private:
    hulk::shared_ptr< i_order_events > _i_events;
    hulk::shared_ptr< o_order_events > _o_events;

    hulk::shared_ptr< o_order_event_reader > _output_reader;
    hulk::shared_ptr< o_order_event_writer > _output_writer;
    hulk::shared_ptr< journal_thread > _journal_thread;
    hulk::shared_ptr< business_thread > _business_thread;
    hulk::shared_ptr< i_order_event_writer > _input_writer;
};

}
}

#endif
