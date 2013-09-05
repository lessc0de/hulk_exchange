
#include "hulk/core/thread.h"
#include "hulk/core/disruptor.h"
#include "hulk/core/stopwatch.h"
#include "hulk/exchange/events.h"

#include <fstream>
#include <iostream>
#include <cstring>

using namespace hulk::core;
using namespace hulk::exchange;

stopwatch watch;

template< class T >
class reader_thread : public thread
{
public:
    reader_thread( ring_buffer<T>& rb )
    : _rb( rb ),
      _barrier( rb.get_sequence() )
    {
    }

    reader_thread( reader_thread<T>& r )
    : _rb( r._rb ),
      _barrier( r._seq )
    {
    }

    sequence& get_sequence()
    {
        return _seq;
    }

    ring_buffer<T>& get_ring_buffer()
    {
        return _rb;
    }

protected:
    virtual void process( const T& item ) = 0;

private:
    virtual void run()
    {
        while( 1 )
        {
            if( _barrier.get() - _seq.get() )
            {
                process( _rb.at( _seq ) );
                _seq.add();
            }
            else
            {
                yield();
            }
        }
    }

    ring_buffer<T>& _rb;
    sequence& _barrier;
    sequence _seq;
};

class output_thread : public reader_thread< output_event >
{
public:
    output_thread( ring_buffer< output_event >& rb )
    : reader_thread< output_event >( rb ) {}

protected:
    virtual void process( const output_event& item ) 
    {
        //std::cout << "process: " << item._body._ack_order._transaction_id << std::endl;

        static unsigned long long i = 0;

        ++i;
        if( i % 1000000 == 0 ) {
            std::cout << "processed " << i << " in " << watch.elapsed_s() << " secs, rate " << (double)i / (double)watch.elapsed_s() << std::endl;
        }
    }
};

typedef reader_thread< input_event > input_thread;

class journal_thread : public input_thread
{
public:
    journal_thread( ring_buffer< input_event >& rb )
    : input_thread( rb ) {}

protected:
    virtual void process( const input_event& item )
    {
        /*
        static std::ofstream op( "/var/tmp/input_events.bin", std::ios::binary );
        op.write( (char*)&item, sizeof( input_event ) );
        op.flush();
        */
    }
};

class replicate_thread : public input_thread
{
public:
    replicate_thread( input_thread& r )
    : input_thread( r ) {}

protected:
    virtual void process( const input_event& item ) {
        //std::cout << "replicate: " << item._data << std::endl;
    }
};

class business_thread : public input_thread
{
public:
    business_thread( input_thread& i, output_thread& o )
    : input_thread( i ),
      _output_thread( o ),
      _output_rb( o.get_ring_buffer() ) {}

protected:
    virtual void process( const input_event& e )
    {
        sequence& s1 = _output_rb.get_sequence();
        sequence& s2 = _output_thread.get_sequence();
        size_t sz = _output_rb.size();

        while( s1.get() - s2.get() >= sz ) {
            yield();
        }

        output_event& o = _output_rb.next();
        o._header._type = output_event::ACK_ORDER;
        o._body._ack_order._transaction_id = e._body._new_order._transaction_id;
        _output_rb.commit();
    }

private:
    output_thread& _output_thread;
    ring_buffer< output_event >& _output_rb;
};

int main( int argc, char** argv )
{
    ring_buffer< output_event > output_rb( 8*1024 );
    output_thread ot( output_rb );

    ring_buffer< input_event > input_rb( 8*1024 );
    journal_thread jt( input_rb );
    business_thread bt( jt, ot );

    watch.start();
    int tid = 0;

    while( 1 )
    {
        if( input_rb.get_sequence().get() - bt.get_sequence().get() < input_rb.size() )
        {
            input_event& e = input_rb.next();
            e._header._type = input_event::NEW_ORDER;
            e._body._new_order._side = BUY;
            e._body._new_order._transaction_id = ++tid;
            e._body._new_order._qty = 1;
            e._body._new_order._px = 1;
            strcpy( e._body._new_order._symbol._data, "VOD.L" );
            input_rb.commit();
        }
        else
        {
            pthread_yield();
        }
    }

    jt.join();
    bt.join();
    ot.join();
}