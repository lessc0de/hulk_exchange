#include "hulk/core/logger.h"
#include "hulk/fix/tcp.h"
#include "hulk/exchange/order.h"
#include "hulk/exchange/orderbook.h"

#include <set>

using namespace hulk;
using namespace hulk::exchange;

log& l = logger::instance().get( "hulk.exchange" );

// -----------------------------------------------------------------------------
bool fill( order& new_order, order& book_order )
{
    order* buy;
    order* sell;

    if( new_order._side == BUY ) {
        buy = &new_order; sell = &book_order;
    } else {
        buy = &book_order; sell = &new_order;
    }

    if( sell->_px <= buy->_px )
    {
        LOG_DEBUG( l, "matched! " <<
            "B " << buy->_order_qty << " @ " << buy->_px << " versus " <<
            "S " << sell->_order_qty << " @ " << sell->_px );

        qty qty = std::min( new_order._leaves_qty, book_order._leaves_qty );
        new_order.fill( qty, book_order._px );
        book_order.fill( qty, book_order._px );

        return true;
    }

    return false;
}

template< typename TOrders >
void match( order& order, orderbook& book, TOrders& book_orders )
{
    std::vector< typename TOrders::iterator > to_del;
    ::hulk::exchange::order* book_order;

    typename TOrders::iterator it = book_orders.begin();
    while( it != book_orders.end() && !order.is_filled() )
    {
        book_order = it->second;

        if( !fill( order, *book_order ) ) {
            LOG_INFO( l, "no match!" ); break;
        }

        if( book_order->is_filled() ) {
            to_del.push_back( it );
        }

        ++it;
    }

    for( int i=0; i<to_del.size(); i++ ) {
        book.del( to_del[i] );
    }
}

void process( order& order, orderbook& book )
{
    order.ack();

    if( order._side == BUY ) {
        match( order, book, book.get_sell_orders() );
    } else {
        match( order, book, book.get_buy_orders() );
    }

    if( !order.is_filled() ) {
        book.add( order );
    }
}

// -----------------------------------------------------------------------------
struct drop_copy_callback : public tcp_callback
{
    virtual void on_open( tcp_context& c )
    {
        LOG_INFO( l, "drop copy client connected" );
        _fds.insert( c._fd );
    }

    virtual void on_close( tcp_context& c )
    {
        LOG_INFO( l, "drop copy client disconnected" );
        _fds.erase( c._fd );
    }

    void publish( const std::string& s )
    {
        if( _fds.size() )
        {
            LOG_INFO( l, "publishing to " << _fds.size() << " drop copy clients" );

            std::set< int >::iterator it = _fds.begin();
            while( it != _fds.end() )
            {
                ::send( *it, (s+"\r\n").c_str(), s.size(), 0 );
                ++it;
            }
        }
    }

    std::set< int > _fds;
};

drop_copy_callback* dc_callback = new drop_copy_callback;

// -----------------------------------------------------------------------------
class market_data_session;

std::set< market_data_session* > md_sessions;

struct market_data_session : public fix::session
{
    market_data_session( fix::transport& transport )
    : fix::session( transport )
    {
        LOG_INFO( l, "created market_data_session @ " << this );

        fix::fields header;
        header.push_back( fix::field( 49, "HULK-MD" ) );
        header.push_back( fix::field( 50, "HULK-MD-S" ) );
        header.push_back( fix::field( 56, "HULK-CLIENT" ) );
        set_protocol( "FIX.4.4" );
        set_header( header );

        md_sessions.insert( this );
    }

    virtual void closed()
    {
        LOG_INFO( l, "closed market_data_session @ " << this );
        md_sessions.erase( this );
    }
};

// -----------------------------------------------------------------------------
struct my_order_callback : public order::callback
{
    void build_msg( const order& o, fix::fields& body, const fix::value& exec_type )
    {
        body.push_back( fix::field( 11, o._id ) );
        body.push_back( fix::field( 38, o._order_qty ) );
        body.push_back( fix::field( 44, o._px ) );
        body.push_back( fix::field( 54, o._side == BUY ? 1 : 2 ) );
        body.push_back( fix::field( 55, o._symbol ) );
        body.push_back( fix::field( 150, exec_type ) );
    }

    virtual void on_ack( const order& o )
    {
        LOG_INFO( l, "ack " << o._id );

        fix::session* session = o.get_session();
        fix::fields body;
        if( o._state == NEW ) {
            build_msg( o, body, "0" );
        } else {
            build_msg( o, body, "4" );
        }

        std::string buf;
        session->send( "8", body, &buf );
        dc_callback->publish( buf );
    }

    virtual void on_fill( const order& o, qty qty, px px )
    {
        LOG_INFO( l, "fill " << qty << " @ " << px << " leaves " << o._leaves_qty );

        fix::session* session = o.get_session();
        fix::fields body;
        build_msg( o, body, "F" );
        body.push_back( fix::field( 31, px ) );
        body.push_back( fix::field( 32, qty ) );

        std::string buf;
        session->send( "8", body, &buf );
        dc_callback->publish( buf );
    }
};

// -----------------------------------------------------------------------------
typedef std::map< px, qty, std::greater< px > > buy_levels;
typedef std::map< px, qty, std::less< px > > sell_levels;

typedef std::map< id, order* > id_to_order_map;
id_to_order_map txn_to_order;

std::map< std::string, orderbook > orderbooks;

class order_entry_session : public fix::session
{
public:
    order_entry_session( fix::transport& transport )
    : fix::session( transport ), num_recvd( 0 )
    {
        fix::fields header;
        header.push_back( fix::field( 49, "HULK-EXCHANGE" ) );
        header.push_back( fix::field( 56, "HULK-CLIENT" ) );
        set_protocol( "FIX.4.4" );
        set_header( header );

        LOG_INFO( l, "created order_entry_session" );
    }

    virtual void recv( const fix::fields& msg, const std::string buf )
    {
        dc_callback->publish( buf );

        LOG_INFO( l, "parsing " << msg.size() << " fields" );

        std::string msg_type;
        std::string symbol;
        id txn_id;
        qty qty;
        px px;
        side side;

        for( int i=0; i<msg.size(); i++ )
        {
            switch( msg[i]._tag )
            {
                case 35: msg_type = msg[i]._value; break;
                case 11: txn_id = msg[i]._value; break;
                case 38: qty = atoi( msg[i]._value.c_str() ); break;
                case 44: px = atof( msg[i]._value.c_str() ); break;
                case 54: side = ( msg[i]._value[0] == '1' ? BUY : SELL ); break;
                case 55: symbol = msg[i]._value; break;
            }
        }

        orderbook& book = orderbooks[symbol];

        LOG_DEBUG( l,
            "msg_type=" << msg_type << ", " <<
            "txn_id=" << txn_id << ", " <<
            "qty=" << qty << ", " <<
            "px=" << px << ", " <<
            "side=" << ( side == BUY ? "B" : "S" ) << ", " <<
            "symbol=" << symbol );

        if( msg_type == "D" )
        {
            LOG_INFO( l, "new order " << txn_id );

            order* o = new order( symbol, txn_id, side, qty, px );
            o->set_callback( cb );
            o->set_session( *this );
            process( *o, book );

            if( !o->is_filled() ) {
                txn_to_order[ txn_id ] = o;
            }
        }
        else
        if( msg_type == "F" )
        {
            LOG_INFO( l, "cxl order " << txn_id );

            id_to_order_map::iterator it = txn_to_order.find( txn_id );
            if( it != txn_to_order.end() )
            {
                order* o = it->second;
                o->cancel();
                o->ack();

                txn_to_order.erase( txn_id );
                book.del( *o );
            }
        }
        else
        {
            LOG_INFO( l, "ignored msg_type " << msg_type );
        }

        LOG_INFO( l, "book " << symbol << " has "
            << book.get_buy_orders().size() << " buys and "
            << book.get_sell_orders().size() << " sells" );

        publish_md( book, symbol );
    }

    void publish_md( orderbook& book, const std::string& symbol, int n = 5 )
    {
        if( md_sessions.size() )
        {
            LOG_INFO( l, "publishing to " << md_sessions.size() << " market data clients" );

            buy_levels blevels;

            orderbook::buy_orders& bo = book.get_buy_orders();
            for( orderbook::buy_orders::iterator it = bo.begin(); it != bo.end(); it++ )
            {
                if( blevels.find( it->first ) == blevels.end() ) {
                    blevels[ it->first ] = it->second->_leaves_qty;
                } else {
                    blevels[ it->first ] += it->second->_leaves_qty;
                }

                if( blevels.size() > n ) {
                    blevels.erase( it->first ); break;
                }
            }

            sell_levels slevels;

            orderbook::sell_orders& so = book.get_sell_orders();
            for( orderbook::sell_orders::iterator it = so.begin(); it != so.end(); it++ )
            {
                if( slevels.find( it->first ) == slevels.end() ) {
                    slevels[ it->first ] = it->second->_leaves_qty;
                } else {
                    slevels[ it->first ] += it->second->_leaves_qty;
                }

                if( slevels.size() > n ) {
                    slevels.erase( it->first ); break;
                }
            }

            fix::fields body;
            body.push_back( fix::field( 55, symbol ) );

            for( buy_levels::iterator it = blevels.begin(); it != blevels.end(); it++ )
            {
                body.push_back( fix::field( 132, it->first ) );
                body.push_back( fix::field( 134, it->second ) );
            }

            for( sell_levels::iterator it = slevels.begin(); it != slevels.end(); it++ )
            {
                body.push_back( fix::field( 133, it->first ) );
                body.push_back( fix::field( 135, it->second ) );
            }

            for( std::set< market_data_session* >::iterator it = md_sessions.begin(); it != md_sessions.end(); it++ ) {
                (*it)->send( "i", body );
            }
        }
    }

    int num_recvd;
    my_order_callback cb;
};

// -----------------------------------------------------------------------------
int main( int argc, char** argv )
{
    int port = 8001;
    LOG_INFO( l, "hulk exchange starting" );

    fix::tcp_event_loop oe_eloop;
    oe_eloop.new_acceptor< order_entry_session >( port );

    fix::tcp_event_loop md_eloop;
    md_eloop.new_acceptor< market_data_session >( port+2 );

    tcp_event_loop dc_eloop;
    dc_eloop.watch( tcp_bind( port+1 ), true, *dc_callback );

    LOG_INFO( l, "starting main loop" );
    while( 1 )
    {
        oe_eloop.loop();
        dc_eloop.loop();
        md_eloop.loop();
    }
}