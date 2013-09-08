
#ifndef _hulk_exchange_order_h_
#define _hulk_exchange_order_h_

#include "hulk/exchange/types.h"

namespace hulk {

namespace fix {
    class session;
}

namespace exchange {

class order
{
public:
    struct callback
    {
        virtual void on_cancel( const order& ) {}
        virtual void on_ack( const order& ) {}
        virtual void on_fill( const order&, qty qty, px px ) {}
    };

    order( const std::string& symbol, id id, side side, qty qty, px px=0 )
    : _symbol( symbol ),
      _state( PENDING_NEW ),
      _id( id ),
      _side( side ),
      _order_qty( qty ),
      _leaves_qty( qty ),
      _exec_qty( 0 ),
      _px( px ),
      _cb( 0 )
    {
    }

    void set_callback( callback& cb )
    {
        _cb = &cb;
    }

    void set_session( fix::session& session )
    {
        _session = &session;
    }

    fix::session* get_session() const
    {
        return _session;
    }

    bool is_filled()
    {
        return _exec_qty >= _order_qty;
    }

    void cancel()
    {
        _state = PENDING_CANCEL;

        if( _cb ) {
            _cb->on_cancel( *this );
        }
    }

    void ack()
    {
        if( _state == PENDING_NEW ) {
            _state = NEW;
        } else if( _state == PENDING_CANCEL ) {
            _state = CANCELED;
        }

        if( _cb ) {
            _cb->on_ack( *this );
        }
    }

    void fill( qty qty, px px )
    {
        _exec_qty += qty;
        _leaves_qty -= qty;

        if( _exec_qty >= _order_qty ) {
            _state = FILLED;
        }

        if( _cb ) {
            _cb->on_fill( *this, qty, px );
        }
    }

    std::string _symbol;
    state _state;
    id _id;
    side _side;
    px _px;
    qty _order_qty;
    qty _leaves_qty;
    qty _exec_qty;
    callback* _cb;
    fix::session* _session;
};

}
}

#endif