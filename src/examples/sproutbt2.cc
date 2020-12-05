#include <unistd.h>
#include <string>
#include <assert.h>
#include <list>
#include <iostream>
#include <fstream>
#include <sys/time.h>

#include "sproutconn.h"
#include "select.h"

using namespace std;
using namespace Network;

int main( int argc, char *argv[] )
{
  char *ip;
  int port;
  std::ofstream s_logger;
  std::ofstream r_logger;
  char command[512];
  double timeToRun;

  Network::SproutConnection *net;

  bool server = true;

  if ( argc == 5 ) {
    /* client */

    server = false;

    ip = argv[ 1 ];
    port = atoi( argv[ 2 ] );

    net = new Network::SproutConnection( "4h/Td1v//4jkYhqhLGgegw", ip, port );
    sprintf (command, "%s_client_send.csv", argv[3]);
    s_logger.open(command);
    sprintf (command, "%s_client_recv.csv", argv[3]);
    r_logger.open(command);
    timeToRun = stod(argv[4]);
  } 
  else if ( argc == 4 ) {
    net = new Network::SproutConnection( NULL, argv[ 1 ] );

    printf( "Listening on port: %d\n", net->port() );
    sprintf (command, "%s_server_send.csv", argv[2]);
    s_logger.open(command);
    sprintf (command, "%s_server_recv.csv", argv[2]);
    r_logger.open(command);
    timeToRun = stod(argv[3]);
  } 
  else {
    printf( "usage: sproutbt2 [IP] port filename duration" );
    exit(1);
  }

  

  Select &sel = Select::get_instance();
  sel.add_fd( net->fd() );

  const int fallback_interval = server ? 20 : 50;

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

  struct timeval currentTime;
  struct timeval startTime;
  double relativeTime = 0;
  gettimeofday(&startTime,NULL);

  uint64_t time_of_next_transmission = timestamp() + fallback_interval;

  fprintf( stderr, "Looping...\n" );  

  /* loop */
  while ( relativeTime <= timeToRun ) 
  {
    gettimeofday(&currentTime, NULL);
    relativeTime = (currentTime.tv_sec- startTime.tv_sec)+(currentTime.tv_usec-startTime.tv_usec)/1000000.0;
    int bytes_to_send = net->window_size();

    if ( !server ) {
      bytes_to_send = 0;
    }

    /* actually send, maybe */
    if ( ( bytes_to_send > 0 ) || ( time_of_next_transmission <= timestamp() ) ) {
      do {
        int this_packet_size = std::min( 1440, bytes_to_send );
        bytes_to_send -= this_packet_size;
        assert( bytes_to_send >= 0 );

        string garbage( this_packet_size, 'x' );

        int time_to_next = 0;
        if ( bytes_to_send == 0 ) {
          time_to_next = fallback_interval;
        }

	      net->send( garbage, time_to_next );
        s_logger << garbage.size() << "," << time_to_next << "\n";
      } 
      while ( bytes_to_send > 0 );

      time_of_next_transmission = std::max( timestamp() + fallback_interval,
					    time_of_next_transmission );
    }

    /* wait */
    int wait_time = time_of_next_transmission - timestamp();
    if ( wait_time < 0 ) {
      wait_time = 0;
    } 
    else if ( wait_time > 10 ) {
      wait_time = 10;
    }

    int active_fds = sel.select( wait_time );
    if ( active_fds < 0 ) {
      perror( "select" );
      exit( 1 );
    }

    /* receive */
    if ( sel.read( net->fd() ) ) {
      string packet( net->recv() );
      r_logger << packet.size() << "\n";
    }
  }
  s_logger.close();
  r_logger.close();
}
