
#include "hulk/core/logger.h"
#include "hulk/exchange/match.h"

using namespace hulk::core;
using namespace hulk::exchange;

log& l = logger::instance().get( "hulk.exchange" );

const int RB_SIZE = 1024;

namespace hulk {
namespace exchange {

class journal_thread : public reader_thread< i_order_event >
{
public:
    journal_thread( i_order_events& ie )
    : reader_thread< i_order_event >( ie )
    {
    }

protected:
    virtual void process( const i_order_event& ev )
    {
        LOG_INFO( l, "journal process" );
    }
};

class business_thread : public reader_thread< i_order_event >
{
public:
    business_thread( journal_thread& jt, hulk::shared_ptr< o_order_event_writer > ow )
    : reader_thread< i_order_event >( jt ),
      _output_writer( ow )
    {
    }

protected:
    virtual void process( const i_order_event& i_ev )
    {
        LOG_INFO( l, "order process" );

        o_order_event& o_ev = _output_writer->next();
        o_ev._header._type = o_order_event::ORDER_ACCEPTED;
        o_ev._body._order_accepted._order_id = 555;
        _output_writer->commit();
    }

private:
    hulk::shared_ptr< o_order_event_writer > _output_writer;
};

}
}

match_engine::match_engine()
: _i_events( new i_order_events( RB_SIZE ) ),
  _o_events( new o_order_events( RB_SIZE ) ),
  _output_reader( new o_order_event_reader( *_o_events ) ),
  _output_writer( new o_order_event_writer( *_output_reader ) ),
  _journal_thread( new journal_thread( *_i_events ) ),
  _business_thread( new business_thread( *_journal_thread, _output_writer ) ),
  _input_writer( new i_order_event_writer( _business_thread->get_reader() ) )
{
}

id match_engine::new_limit_order( symbol& symbol, side side, qty qty, px px )
{
    static int id = 1;
    i_order_event& ev = _input_writer->next();
    ev._header._type = i_order_event::NEW_ORDER;
    ev._body._new_order._order_id = id++;
    ev._body._new_order._side = side;
    ev._body._new_order._qty = qty;
    ev._body._new_order._px = px;
    strcpy( ev._body._new_order._symbol._data, symbol._data );
    _input_writer->commit();
}

id match_engine::new_market_order( symbol& symbol, side side, qty qty)
{
    new_limit_order( symbol, side, qty, 0 );
}

void match_engine::cancel_order( id id )
{
    i_order_event& ev = _input_writer->next();
    ev._header._type = i_order_event::CANCEL_ORDER;
    ev._body._cancel_order._order_id = id;
    _input_writer->commit();
}

hulk::shared_ptr< o_order_event_reader > match_engine::get_reader()
{
    return _output_reader;
}

void match_engine::start()
{
    _journal_thread->start();
    _business_thread->start();
}

void match_engine::join()
{
    _business_thread->join();
    _journal_thread->join();
}

int main( int argc, char** argv )
{
    match_engine me;
    me.start();
    me.new_limit_order( *(symbol*)"VOD.L", BUY, 100, 100 );

    const o_order_event& ev = me.get_reader()->next();

    std::cout << "got ev: " << ev._body._order_accepted._order_id << std::endl;

    me.join();
}
