#include <unistd.h>
#include <string>
#include <assert.h>
#include <list>

#include "network.h"
#include "select.h"

#include "deliveryforecast.pb.h"

using namespace std;
using namespace Network;

class BulkPacket
{
private:
  string _forecast, _data;

public:
  BulkPacket() : _forecast(), _data() {}

  /* No forecast update */
  BulkPacket( bool, const string & s_data )
    : _forecast(),
      _data( s_data )
  {}

  void add_forecast( const Sprout::DeliveryForecast & s_forecast )
  {
    _forecast = s_forecast.SerializeAsString();
    assert( _forecast.size() <= 65535 );
  }

  BulkPacket( const string & incoming )
    : _forecast( incoming.substr( sizeof( uint16_t ), *(uint16_t *) incoming.data() ) ),
      _data( incoming.substr( sizeof( uint16_t ) + _forecast.size() ) )
  {}

  string tostring( void ) const
  {
    uint16_t forecast_size = _forecast.size();
    string ret( (char *) & forecast_size, sizeof( forecast_size ) );
    if ( forecast_size ) {
      ret.append( _forecast );
    }
    ret.append( _data );
  
    return ret;
  }

  const string & raw_forecast( void ) const { return _forecast; }
  bool has_forecast( void ) const { return _forecast.size() > 0; }

  Sprout::DeliveryForecast forecast( void ) const
  {
    assert( has_forecast() );

    Sprout::DeliveryForecast ret;
    assert( ret.ParseFromString( _forecast ) );

    return ret;
  }
};

int main( int argc, char *argv[] )
{
  char *key;
  char *ip;
  int port;

  Network::Connection *net;

  bool server = true;

  if ( argc > 1 ) {
    /* client */

    server = false;

    key = argv[ 1 ];
    ip = argv[ 2 ];
    port = atoi( argv[ 3 ] );

    net = new Network::Connection( key, ip, port );
  } else {
    net = new Network::Connection( NULL, NULL );
  }

  fprintf( stderr, "Port bound is %d, key is %s\n", net->port(), net->get_key().c_str() );

  Select &sel = Select::get_instance();
  sel.add_fd( net->fd() );

  const int interval = 1;

  /* wait to get attached */
  if ( server ) {
    while ( 1 ) {
      int active_fds = sel.select( -1 );
      if ( active_fds < 0 ) {
	perror( "select" );
	exit( 1 );
      }

      if ( sel.read( net->fd() ) ) {
	net->recv();
      }

      if ( net->get_has_remote_addr() ) {
	break;
      }
    }
  }

  uint64_t time_of_last_forecast = -1;
  uint64_t time_of_next_transmission = timestamp() + interval;

  fprintf( stderr, "Looping...\n" );  

  /* loop */
  while ( 1 ) {
    int wait_time = time_of_next_transmission - timestamp();
    if ( wait_time < 0 ) {
      wait_time = 0;
    }

    int active_fds = sel.select( wait_time );
    if ( active_fds < 0 ) {
      perror( "select" );
      exit( 1 );
    }

    uint64_t now = timestamp();

    /* send */
    if ( time_of_next_transmission <= now ) {
      Sprout::DeliveryForecast forecast = net->forecast();

      do {
	string data( 1400, ' ' );
	assert( data.size() == 1400 );

	BulkPacket bp( false, data );

	if ( forecast.time() != time_of_last_forecast ) {
	  bp.add_forecast( forecast );
	  time_of_last_forecast = forecast.time();
	  //	  fprintf( stderr, "Sending forecast, size = %lu (should be %lu)\n", bp.raw_forecast().size(), forecast.SerializeAsString().size() );
	} else {
	  //	  fprintf( stderr, "Sending...\n" );
	}

	net->send( bp.tostring() );
	time_of_next_transmission += interval;
      } while ( time_of_next_transmission <= now );
    }

    /* receive */
    if ( sel.read( net->fd() ) ) {
      BulkPacket packet( net->recv() );

      if ( packet.has_forecast() ) {
	Sprout::DeliveryForecast forecast( packet.forecast() );

	fprintf( stderr, "Forecast: packet=%ld t=%ld %d %d %d %d %d %d %d %d %d %d\n",
		 forecast.received_or_lost_count(),
		 forecast.time(),
		 forecast.counts( 0 ),
		 forecast.counts( 1 ),
		 forecast.counts( 2 ),
		 forecast.counts( 3 ),
		 forecast.counts( 4 ),
		 forecast.counts( 5 ),
		 forecast.counts( 6 ),
		 forecast.counts( 7 ),
		 forecast.counts( 8 ),
		 forecast.counts( 9 ) );
      }
    }
  }
}