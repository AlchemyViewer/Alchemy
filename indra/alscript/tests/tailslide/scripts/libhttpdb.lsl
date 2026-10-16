//----------------------------------------------------------------------------//
// httpdb constants                                                           //
//----------------------------------------------------------------------------//

// The full base url to use for httpdb requests.
string      HTTPDB_URL          = "http://w-hat.com/httpdb/";

//----------------------------------------------------------------------------//
// httpdb messages                                                            //
//----------------------------------------------------------------------------//

// If you want to use httpdb through link messages in another script, you should
// copy these constants. You then make requests with:
//   llMessageLinked( [link_number], [request_code], [value], [name] );
// ex:
//   llMessageLinked( LINK_SET, HTTPDB_SAVE, "some data", "log/example.log");

// Request codes. These are also used internally - do not remove them.
integer     HTTPDB_SAVE         = 1000;
integer     HTTPDB_LOAD         = 1100;
integer     HTTPDB_DELETE       = 1200;

// Response codes. If you don't want to use httpdb through link messages, you
// can remove these.
integer     HTTPDB_VALUE_LOADED = 5000;
integer     HTTPDB_FAILURE      = 5100;
integer     HTTPDB_SUCCESS      = 5200;

//----------------------------------------------------------------------------//
// httpdb variables                                                           //
//----------------------------------------------------------------------------//

// Strided [reqid, name, type] list of pending requests.
list        httpdb_reqids     = [];

// The amount of free space returned by last PUT/DELETE request.
integer     httpdb_free_space = -1;

//----------------------------------------------------------------------------//
// httpdb functions                                                           //
//----------------------------------------------------------------------------//

// Abstract httpdb request interface.
httpdb_request( integer type, string type_str, string name, string body ) {
    key reqid = llHTTPRequest(HTTPDB_URL + name, [HTTP_METHOD, type_str], body );
    if ( reqid == NULL_KEY )
        httpdb_failure( type, name, 0, "HTTP throttled" );
    else
        httpdb_reqids += [reqid, name, type];
}

// Save a value to httpdb.
httpdb_save( string name, string value ) {
    httpdb_request( HTTPDB_SAVE, "PUT", name, value );
}

/// Load a value from httpdb. The function httpdb_value_loaded() will be
/// called with the name and value when the request completes.
httpdb_load(string name) {
    httpdb_request( HTTPDB_LOAD, "GET", name, "" );
}

// Delete a value from httpdb.
httpdb_delete(string name) {
    httpdb_request( HTTPDB_DELETE, "DELETE", name, "" );
}

//----------------------------------------------------------------------------//
// httpdb "virtual" functions.                                                //
//----------------------------------------------------------------------------//
// If you don't want to put httpdb into a seperate script and use it through
// link messages, then you should replace the link message code with your own
// in these functions.

// Called when a value is successfully loaded.
httpdb_value_loaded(string name, string value) {

    llMessageLinked(LINK_SET, HTTPDB_VALUE_LOADED, value, name);

}

// Called when a request fails.
httpdb_failure( integer type, string name, integer status, string body) {

    // You can programatically handle errors here, or just report them.
    llOwnerSay( "[ERROR] httpdb returned status " + (string)status + " for " + (string)type + " on " + name );

    llMessageLinked(LINK_SET, HTTPDB_FAILURE, llList2CSV([type, status, body]), name );

}

// Called when a request was successful.
httpdb_success(integer type, string name, integer status, string body) {

    llMessageLinked(LINK_SET, HTTPDB_SUCCESS, llList2CSV([type, status, body]), name );

}

//----------------------------------------------------------------------------//
// httpdb events                                                              //
//----------------------------------------------------------------------------//

default {

    // httpdb link message handler. if you're not using httpdb in a seperate
    // script using link messages, you should remove this.
    link_message( integer sender, integer num, string str, key id ) {
        if ( num == HTTPDB_SAVE )
            httpdb_save( (string)id, str );
        else if ( num == HTTPDB_LOAD )
            httpdb_load( (string)id );
        else if ( num == HTTPDB_DELETE )
            httpdb_delete( (string)id );
    }


    // httpdb llHTTPRequest response handler. this should not be removed.
    // insert your own code at the bottom of the function.
    http_response( key reqid, integer status, list meta, string body ) {

        // See if it's an httpdb request
        integer httpdb_req_index = llListFindList( httpdb_reqids, [reqid] );
        if ( httpdb_req_index != -1 ) {
            // pull its info out the list
            string  name = llList2String( httpdb_reqids, httpdb_req_index+1 );
            integer type = llList2Integer(httpdb_reqids, httpdb_req_index+2 );

            // remove it from the list
            httpdb_reqids = llDeleteSubList( httpdb_reqids, httpdb_req_index, httpdb_req_index+2 );

            // only 2xx codes represent success
            if ( status < 200 || status >= 300 ) {
                httpdb_failure( type, name, status, body );
                return;
            }

            // if it's a load request, call httpdb_value_loaded
            if ( type == HTTPDB_LOAD )
                httpdb_value_loaded( name, body );
            // otherwise, the body should be the updated amount of free space    
            else
                httpdb_free_space = (integer)body;

            httpdb_success( type, name, status, body );

            return;
        }

        // Not an httpdb request

        // ..add your own handlers here..

    }

}

