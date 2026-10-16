// Reported By: Strife Onizuka
// Date: 2006-02-15
// Version: 0.2.2
// Error: ZERO_VECTOR and ZERO_ROTATION expressions have uninitialized values

default {
    state_entry() {
        
      vector llTargetOmega_axis       = ZERO_VECTOR;
      float  llTargetOmega_spinrate   = 0.0;
      float  llTargetOmega_gain       = 0.0;

      if ( llTargetOmega_axis == ZERO_VECTOR ) // $[E20012] specific case
          return;

      if((llTargetOmega_axis != ZERO_VECTOR) || (llTargetOmega_spinrate != 0.0) || (llTargetOmega_gain != 0.0)) // $[E20013]
        llOwnerSay("blah");

    }
}
