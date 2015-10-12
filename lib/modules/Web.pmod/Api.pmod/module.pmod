//! The @tt{Web.Api@} has modules and classes for communicating with various
//! @tt{(RESTful)@} web api's such as @[Web.Api.Facebook],
//! @[Web.Api.Instagram], @[Web.Api.Twitter] etc.
//!
//! @example
//! @code
//! string api_key = "......";
//! string api_secret = ".......";
//! string redirect_uri = "http://localhost/insta/";
//!
//! Web.Api.Instagram api;
//!
//! string cookie_file = "my-cookie.txt";
//!
//! int main(int argc, array(string) argv)
//! {
//!   api = Web.Api.Instagram(api_key, api_secret, redirect_uri);
//!
//!   // If a stored authentication cookie exists, populate the auth object.
//!   if (Stdio.exist(cookie_file)) {
//!     api->auth->set_from_cookie(Stdio.read_file(cookie_file));
//!   }
//!
//!   if (!api->auth->is_authenticated()) {
//!     // A cookie existed but the authentication has expired
//!     if (api->auth->is_renewable()) {
//!       write("Trying to refresh token...");
//!       if (string data = api->auth->refresh_token())
//!         Stdio.write_file(cookie_file, data);
//!         write("done ok!\n");
//!       }
//!       else {
//!         werror("failed!\n");
//!       }
//!     }
//!     // No previous authentication exists. Create a new one...
//!     else {
//!       // Get the uri to the authentication endpoint
//!       string uri = api->auth->get_auth_uri();
//!
//!       write("Opening \"%s\" in browser.\nCopy the contents of the address "
//!             "bar and paste here: ", Standards.URI(uri));
//!
//!       sleep(2);
//!
//!       // For Mac
//!       Process.create_process(({ "open", uri }));
//!
//!       string resp = Stdio.Readline()->read();
//!       mapping p = Web.Auth.query_to_mapping(Standards.URI(resp)->query);
//!
//!       string code;
//!
//!       // If the auth object is OAuth and not OAuth2 this step is required.
//!       // Instagram is OAuth2, but Twitter is OAuth1
//!       if (p->oauth_token) {
//!         auth->set_authentication(p->oauth_token);
//!         code = p->oauth_verifier;
//!       }
//!       else {
//!         code = p->code;
//!       }
//!
//!       // Now, get an access token
//!       if (string data = auth->request_access_token(code)) {
//!         Stdio.write_file(cookie, data);
//!       }
//!       else {
//!         werror("Failed getting an access token. Aborting!\n");
//!         return 1;
//!       }
//!     }
//!   }
//!
//!   if (api->auth->is_authenticated()) {
//!     // No UID (arg1) means get the authenticated users stuff
//!     // No additional params (arg2)
//!     // Run async (arg3)
//!     mapping m = api->users->recent(0, 0, lambda(mixed m) {
//!       werror("Insta: %O\n", m);
//!       exit(0);
//!     });
//!
//!     return -1;
//!   }
//!   else {
//!     werror("No authentication was aquired!\n");
//!     return 1;
//!   }
//! }
//! @endcode


//! Default user agent in HTTP calls
constant USER_AGENT = "Mozilla 4.0 (Pike/" + __REAL_MAJOR__ + "." +
                      __REAL_MINOR__ + "." + __REAL_BUILD__ + ")";

//! Human readable representation of @[timestamp].
//!
//! Examples are:
//!
//! @code
//!     0 .. 30 seconds:    Just now
//!     0 .. 120 seconds:   Just recently
//!   121 .. 3600 seconds:  x minutes ago
//!  3601 .. 86400 seconds: x hours ago
//!       .. and so on
//! @endcode
//!
//! @param timestamp
string time_elapsed(int timestamp)
{
  int diff = (int) time(timestamp);
  int t;

  switch (diff)
  {
    case      0 .. 30: return "Just now";
    case     31 .. 120: return "Just recently";
    case    121 .. 3600: return sprintf("%d minutes ago",(int)(diff/60.0));
    case   3601 .. 86400:
      t = (int)((diff/60.0)/60.0);
      return sprintf("%d hour%s ago", t, t > 1 ? "s" : "");

    case  86401 .. 604800:
      t = (int)(((diff/60.0)/60.0)/24);
      return sprintf("%d day%s ago", t, t > 1 ? "s" : "");

    case 604801 .. 31449600:
      t = (int)((((diff/60.0)/60.0)/24)/7);
      return sprintf("%d week%s ago", t, t > 1 ? "s" : "");
  }

  return "A long time ago";
}
