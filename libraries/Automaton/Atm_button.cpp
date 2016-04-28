#include <Automaton.h>
#include "Atm_button.h"

// Add option for button press callback (for reading i2c buttons etc)

Atm_button & Atm_button::begin( int attached_pin )
{
	const static state_t state_table[] PROGMEM = {
	/* Standard Mode: press/repeat */
		/*                  ON_ENTER  ON_LOOP       ON_EXIT  EVT_LMODE  EVT_TIMER  EVT_DELAY  EVT_REPEAT EVT_PRESS  EVT_RELEASE  EVT_COUNTER   EVT_AUTO  ELSE */
		/* IDLE     */            -1,      -1,           -1,     LIDLE,        -1,        -1,         -1,     WAIT,          -1,          -1,      AUTO,   -1,
		/* WAIT     */            -1,      -1,           -1,        -1,   PRESSED,        -1,         -1,       -1,        IDLE,          -1,        -1,    -1,
		/* PRESSED  */     ACT_PRESS,      -1,           -1,        -1,        -1,    REPEAT,         -1,       -1,     RELEASE,          -1,        -1,    -1,
		/* REPEAT   */     ACT_PRESS,      -1,           -1,        -1,        -1,        -1,     REPEAT,       -1,     RELEASE,          -1,        -1,    -1,
		/* RELEASE  */   ACT_RELEASE,      -1,           -1,        -1,        -1,        -1,         -1,       -1,          -1,          -1,        -1,  IDLE,
	/* Long Press Mode: press/long press */	
		/* LIDLE    */            -1,      -1,           -1,        -1,        -1,        -1,         -1,    LWAIT,          -1,          -1,        -1,    -1,
		/* LWAIT    */    ACT_LSTART,      -1,           -1,        -1,  LPRESSED,        -1,         -1,       -1,       LIDLE,          -1,        -1,    -1,
		/* LPRESSED */    ACT_LCOUNT,      -1,           -1,        -1,        -1,  LPRESSED,         -1,       -1,    LRELEASE,    WRELEASE,        -1,    -1,
		/* LRELEASE */  ACT_LRELEASE,      -1, ACT_WRELEASE,        -1,        -1,        -1,         -1,       -1,          -1,          -1,        -1, LIDLE,
		/* WRELEASE */  ACT_LRELEASE,      -1, ACT_WRELEASE,        -1,        -1,        -1,         -1,       -1,       LIDLE,          -1,        -1,    -1,
		/* AUTO     */      ACT_AUTO,      -1,           -1,        -1,        -1,        -1,         -1,       -1,          -1,          -1,        -1,  IDLE,
	};
	Machine::begin( state_table, ELSE );
	pin = attached_pin;
    counter_longpress.set( 0 );	
	timer_debounce.begin( this, DEBOUNCE );
	timer_delay.begin( this, ATM_TIMER_OFF );
	timer_repeat.begin( this, ATM_TIMER_OFF );
	timer_auto.begin( this, ATM_TIMER_OFF );
	pinMode( pin, INPUT_PULLUP );
	return *this;
}

Atm_button & Atm_button::begin( int attached_pin, presscb_t press_callback )
{
    begin( attached_pin );
    callback = press_callback;
    callback_idx = -1;
    return *this;
}

Atm_button & Atm_button::onPress( Machine * machine, int msg ) 
{
  client_machine = machine;
  client_press = msg;
  return *this;  
}

Atm_button & Atm_button::onPress( Machine * machine, int msg_press, int msg_release ) 
{
  client_machine = machine;
  client_press = msg_press;
  client_release = msg_release;
  return *this;  
}

Atm_button & Atm_button::onPress( presscb_t press_callback ) 
{
  callback = press_callback;
  callback_idx = -1;
  return *this;  
}

Atm_button & Atm_button::onPress( presscb_id_t press_callback, int idx ) 
{
  callback_id = press_callback;
  callback_idx = idx;
  return *this;  
}


Atm_button & Atm_button::debounce( int delay ) {
	
	timer_debounce.set( delay );
	return *this;
}

Atm_button & Atm_button::longPress( int max, int delay ) {
	
	longpress_max = max;
    counter_longpress.set( longpress_max );
	timer_delay.set( delay );
	return *this;
}

Atm_button & Atm_button::repeat( int delay, int speed ) {
	
	timer_delay.set( delay );
	timer_repeat.set( speed );	
	return *this;
}

Atm_button & Atm_button::repeat( void ) {
	
	timer_delay.set( 500 );
	timer_repeat.set( 50 );	
	return *this;
}

Atm_button & Atm_button::autoPress( int delay, int press ) {
	
    _auto_press = press;
	timer_auto.set( delay );    
	return *this;
}

int Atm_button::event( int id ) 
{
  switch ( id ) {
	case EVT_LMODE :
	  return counter_longpress.value > 0;        
	case EVT_TIMER :
	  return timer_debounce.expired();
	case EVT_DELAY :
	  return timer_delay.expired();        
	case EVT_REPEAT :
	  return timer_repeat.expired();        
	case EVT_AUTO :
	  return timer_auto.expired();        
	case EVT_PRESS :
	  return !digitalRead( pin );        
	case EVT_RELEASE :
	  return digitalRead( pin );        
	case EVT_COUNTER :
	  return counter_longpress.expired();        
  }
  return 0;
}


void Atm_button::cb( int press, int idx ) {

    if ( callback ) 
      (*callback)( press );
    if ( callback_id ) 
      (*callback_id)( press, idx );
}


void Atm_button::action( int id ) 
{
  switch ( id ) {
	case ACT_PRESS :
      cb( 1, callback_idx );
      if ( client_machine && client_press != -1 ) {
          client_machine->msgWrite( client_press );
      }
	  return;
	case ACT_AUTO :
      cb( _auto_press, callback_idx );
	  return;
	case ACT_RELEASE :
      cb( 0, callback_idx );
      if ( client_machine && client_release != -1 ) {
          client_machine->msgWrite( client_release );
      }
	  return;
	case ACT_LSTART :
	  counter_longpress.set( longpress_max );
	  return;
	case ACT_LCOUNT :
	  counter_longpress.decrement();
	  cb( ( longpress_max - counter_longpress.value ) * -1, callback_idx );	  
	  return;
	case ACT_LRELEASE :
	  cb( longpress_max - counter_longpress.value, callback_idx );
	  return;
	case ACT_WRELEASE :
	  cb( 0, callback_idx );
	  return;
  }
}
