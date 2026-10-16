// vvvvvvvvvvvvvvvvvvvvvv
// * UNIXTIME FUNCTIONS *
// vvvvvvvvvvvvvvvvvvvvvv

// Return the number of seconds since 0 hours, 0 minutes, 0 seconds, January 1, 1970 UTC
list DAYS_PER_MONTH = [ 0, -1,  30, 58, 89, 119, 150, 180,  211, 242, 272, 303, 333 ];
integer timestamp_to_unixtime(string timestamp) {
    integer year    = (integer) llGetSubString(timestamp,  0,  3);
    integer month   = (integer) llGetSubString(timestamp,  5,  6);
    
    return
    (
        // days = (year - 1970) * 365;
        (year - 1970) * 365
        // days += (integer) (((year - 1) - 1968) / 4);
        + (year - 1969) / 4
        // days += llList2Integer( daysPerMonth, month );
        + llList2Integer( DAYS_PER_MONTH, month )
        // days += day;
        + (integer) llGetSubString(timestamp,  8,  9)
        // if ( month > 2 ) days+= !(year % 4);
        + (month>2) * !(year % 4)
    )
    // time += days   * 86400;
    * 86400
    // time += hour   * 3600;  // SECONDS_PER_HOUR;
    + (integer) llGetSubString(timestamp, 11, 12) * 3600
    // time += hour   * 3600;  // SECONDS_PER_HOUR;
    + (integer) llGetSubString(timestamp, 14, 15) * 60
    // time += second;
    + (integer) llGetSubString(timestamp, 17, 18);
}
string unixtime_to_timestamp(integer unixtime) {
    integer year;
    integer month;
    integer day;
    integer hour;
    integer minute;
    integer second;
    string  ds;
    string  ts;
    integer day_temp;
    
    second  = unixtime;
    day     = (integer) second / 86400;
    second %= 86400;
    hour    = (integer) second / 3600;
    second %= 3600;
    minute  = (integer) second / 60;
    second %= 60;
    
    // caveat: does not check to see if leap days have made an extra year
    year = 1970;
    while ( day >= 365 ) {
        day -= 365 + ((year % 4) == 0);
        year += 1;
    }
    //year    = llFloor(day / 365) + 1970;
    //day     = (day % 365) + llFloor(((year - 1968) - 2) / 4) + 1;
    
    // forget about leap day for the moment
    day_temp = day - ((day >= 59) && ((year % 4) == 0));
//    llWhisper( 0, (string) day_temp + " / " + (string) day ) ;
    
         if ( day_temp >= 334 ) { month = 12; day_temp -= 334; } // 30
    else if ( day_temp >= 304 ) { month = 11; day_temp -= 304; } // 31
    else if ( day_temp >= 273 ) { month = 10; day_temp -= 273; } // 30
    else if ( day_temp >= 243 ) { month =  9; day_temp -= 243; } // 31
    else if ( day_temp >= 212 ) { month =  8; day_temp -= 212; } // 31
    else if ( day_temp >= 181 ) { month =  7; day_temp -= 181; } // 30
    else if ( day_temp >= 151 ) { month =  6; day_temp -= 151; } // 31
    else if ( day_temp >= 120 ) { month =  5; day_temp -= 120; } // 30
    else if ( day_temp >=  90 ) { month =  4; day_temp -=  90; } // 31
    else if ( day_temp >=  59 ) { month =  3; day_temp -=  59; } // 28
    else if ( day_temp >=  31 ) { month =  2; day_temp -=  31; } // 31
    else                        { month =  1;                  }

    day = day_temp + 1 + ((day == 59) && ((year % 4) == 0)); 
    //2147483647
    //  YYYYMMDD
    //  01234567
    //    HHMMSS
    //    012345
    
    ds = (string) ((year * 10000) + (month * 100) + day);
    ts = (string) ((hour * 10000) + (minute * 100) + second);
    while ( llStringLength(ts) < 6 ) ts = "0" + ts;
    
    return llGetSubString(ds, 0, 3) + "-" + llGetSubString(ds, 4, 5) + "-" + llGetSubString(ds, 6,7) + "T" +
           llGetSubString(ts, 0, 1) + ":" + llGetSubString(ts, 2, 3) + ":" + llGetSubString(ts, 4,5) + "Z";
    
}

// ^^^^^^^^^^^^^^^^^^^^^^
// * UNIXTIME FUNCTIONS *
// ^^^^^^^^^^^^^^^^^^^^^^

// vvvvvvvvvvvvvvvvvvvvvv
// * UNITTEST FUNCTIONS *
// vvvvvvvvvvvvvvvvvvvvvv

integer assertions;
integer assertions_passed;

assert_equal_string(string str1, string str2) {
    assertions += 1;
    if ( str1 != str2 ) llWhisper( 0, "ASSERTION FAILED: assert_equal_string(\"" + str1 + "\", \"" + str2 + "\")");
    else assertions_passed += 1;
}

assert_equal_integer(integer i1, integer i2) {
    assertions += 1;
    if ( i1 != i2 ) llWhisper( 0, "ASSERTION FAILED: assert_equal_integer(" + (string)i1 + ", " + (string)i2 + ")");
    else assertions_passed += 1;
}

report() {
    llWhisper(0, "Assertions passed: " + (string)assertions_passed + "/" + (string)assertions );
}

// ^^^^^^^^^^^^^^^^^^^^^^
// * UNITTEST FUNCTIONS *
// ^^^^^^^^^^^^^^^^^^^^^^
            
                                    
default {
    state_entry() {
        // timestamps to test against taken from the `date` command on a FreeBSD machine
        // eg: date -j -f "%FT%TZ" "2005-01-30T21:33:11Z" "+%s"
        assert_equal_integer( 0, timestamp_to_unixtime( "1970-01-01T00:00:00.000000Z" ) );  
        assert_equal_string( "1970-01-01T00:00:00Z", unixtime_to_timestamp( timestamp_to_unixtime( "1970-01-01T00:00:00Z" ) ) );
        assert_equal_integer( 31536000, timestamp_to_unixtime( "1971-01-01T00:00:00.000000Z" ) );  
        assert_equal_integer( 94694400, timestamp_to_unixtime( "1973-01-01T00:00:00.000000Z" ) );         
        assert_equal_string( "2005-01-30T21:33:11Z", unixtime_to_timestamp( 1107120791 ) );
        assert_equal_string( "2001-03-04T00:01:37Z", unixtime_to_timestamp( 983664097 ) );
        assert_equal_integer( 951743049, timestamp_to_unixtime( "2000-02-28T13:04:09Z" ) );
        assert_equal_string( "2000-02-28T13:04:09Z", unixtime_to_timestamp( 951743049 ) );
        assert_equal_string( "2000-02-29T12:34:56Z", unixtime_to_timestamp( timestamp_to_unixtime( "2000-02-29T12:34:56" ) ) );
        assert_equal_integer( 951916448, timestamp_to_unixtime( "2000-03-01T13:14:08Z" ) );
        assert_equal_string( "2000-03-01T13:14:08Z", unixtime_to_timestamp( 951916448 ) );
        assert_equal_string( "2001-02-28T12:34:56Z", unixtime_to_timestamp( timestamp_to_unixtime( "2001-02-28T12:34:56" ) ) );
        
        //string ts = llGetTimestamp();
        //llWhisper( 0, ts + " = " + unixtime_to_timestamp( timestamp_to_unixtime( ts ) ) );
        
        report();
    }

}

