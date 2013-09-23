
#ifndef _hulk_exchange_events_h_
#define _hulk_exchange_events_h_

#include "hulk/core/disruptor.h"
#include "hulk/exchange/types.h"

namespace hulk {
namespace exchange {

struct i_order_event
{
    enum type {
        NEW_ORDER, CANCEL_ORDER
    };

    struct header {
        type _type;
    };

    struct new_order
    {
        id _order_id;
        symbol _symbol;
        side _side;
        qty _qty;
        px _px;
    };

    struct cancel_order {
        id _order_id;
    };

    union body
    {
        new_order _new_order;
        cancel_order _cancel_order;
    };

    header _header;
    body _body;
};

struct o_order_event
{
    enum type {
        ORDER_ACCEPTED, ORDER_REJECTED, ORDER_EXECUTED
    };

    struct header {
        type _type;
    };

    struct order_accepted
    {
        id _order_id;
        symbol _symbol;
        side _side;
        qty _order_qty;
        qty _exec_qty;
        px _px;
    };

    struct order_rejected
    {
        id _order_id;
        reason _reason;
    };

    struct order_execution
    {
        id _order_id;
        symbol _symbol;
        side _side;
        qty _qty;
        px _px;
    };

    union body
    {
        order_accepted _order_accepted;
        order_rejected _order_rejected;
        order_execution _order_execution;
    };

    header _header;
    body _body;
};

typedef hulk::core::ring_buffer< i_order_event > i_order_events;
typedef hulk::core::ring_buffer< o_order_event > o_order_events;

typedef hulk::core::reader< o_order_event > o_order_event_reader;
typedef hulk::core::writer< o_order_event > o_order_event_writer;
typedef hulk::core::writer< i_order_event > i_order_event_writer;

}
}

#endif
