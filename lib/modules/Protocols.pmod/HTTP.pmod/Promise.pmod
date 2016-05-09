#pike __REAL_VERSION__
#require constant(Concurrent.Future)

//! This HTTP client module utilses the @[Concurrent.Promise] and
//! @[Concurrent.Future] classes and only does asynchronous calls.
//!
//! @example
//! @code
//! Concurrent.Future q1 = Protocols.HTTP.Promise.get_url("http://www.roxen.com");
//! Concurrent.Future q2 = Protocols.HTTP.Promise.get_url("http://www.google.com");
//!
//! array(Concurrent.Future) all = ({ p1, p2 });
//!
//! /*
//!   To get a callback for each of the requests
//! */
//!
//! all->on_success(lambda (Protocols.HTTP.Promise.Success ok_resp) {
//!   werror("Got successful response for %O\n", ok_resp->host);
//! })->on_failure(lambda (Protocols.HTTP.Promise.Failure failed_resp) {
//!   werror("Request for %O failed!\n", failed_resp->host);
//! });
//!
//!
//! /*
//!   To get a callback when all of the requests are done. In this case
//!   on_failure will be called if any of the request fails.
//! */
//!
//! Concurrent.Future all2 = Concurrent.results(all);
//!
//! all2->on_success(lambda (array(Protocols.HTTP.Promise.Success) ok_resp) {
//!   werror("All request were successful: %O\n", ok_resp);
//! })->on_failure(lambda (Protocols.HTTP.Promise.Success failed_resp) {
//!   werror("The request to %O failed.\n", failed_resp->host);
//! });
//! @endcode

//#define HTTP_PROMISE_DEBUG

#ifdef HTTP_PROMISE_DEBUG
# define TRACE(X...)werror("%s:%d: %s",basename(__FILE__),__LINE__,sprintf(X))
#else
# define TRACE(X...)0
#endif


//! @ignore
protected int _timeout;
protected int _maxtime;
//! @endignore


//! @decl void set_timeout(int t)
//! @decl void set_maxtime(int t)
//!
//! @[set_timeout()] sets the default timeout for connecting and data fetching.
//! The watchdog will be reset each time data is fetched.
//!
//! @[set_maxtime()] sets the timeout for the entire operation. If this is set
//! to @tt{30@} seconds for instance, the request will be aborted after
//! @tt{30@} seconds event if data is still being received. By default this is
//! indefinitley.
//!
//! @[t] is the timeout in seconds.

public void set_timeout(int t)
{
  _timeout = t;
}

public void set_maxtime(int t)
{
  _maxtime = t;
}


//! @decl Concurrent.Future get_url(Protocols.HTTP.Session.URL url,    @
//!                                 void|mapping variables,            @
//!                                 void|mapping headers)
//! @decl Concurrent.Future post_url(Protocols.HTTP.Session.URL url,   @
//!                                  void|string|mapping data,         @
//!                                  void|mapping headers)
//! @decl Concurrent.Future put_url(Protocols.HTTP.Session.URL url,    @
//!                                 void|mapping variables,            @
//!                                 void|string file,                  @
//!                                 void|mapping headers)
//! @decl Concurrent.Future delete_url(Protocols.HTTP.Session.URL url, @
//!                                    void|mapping variables,         @
//!                                    void|mapping headers)
//!
//! Sends a @tt{GET@}, @tt{POST@}, @tt{PUT@} or @tt{DELETE@} request to @[url]
//! asynchronously. A @[Concurrent.Future] object is returned on which you
//! can register callbacks via @[Concurrent.Future->on_success()] and
//! @[Concurrent.Future.on_failure()] which will get a @[Sucess] or @[Failure]
//! object as argument respectively.
//!
//! For an example of usage see @[Protocols.HTTP.Promise]

public Concurrent.Future get_url(Protocols.HTTP.Session.URL url,
                                 void|mapping variables,
                                 void|mapping headers)
{
  return do_method("GET", url, variables, headers);
}

public Concurrent.Future post_url(Protocols.HTTP.Session.URL url,
                                  void|string|mapping data,
                                  void|mapping headers)
{
  return do_method("POST", url, 0, headers, data);
}

public Concurrent.Future put_url(Protocols.HTTP.Session.URL url,
                                 void|string|mapping variables,
                                 void|string file,
                                 void|mapping headers)
{
  return do_method("PUT", url, variables, headers, file);
}

public Concurrent.Future delete_url(Protocols.HTTP.Session.URL url,
                                    void|mapping variables,
                                    void|mapping headers,)
{
  return do_method("POST", url, variables, headers);
}


//! Fetch an URL with the @[http_method] method.
public Concurrent.Future do_method(string http_method,
                                   Protocols.HTTP.Session.URL url,
                                   void|mapping variables,
                                   void|mapping headers,
                                   void|string|mapping data)
{
  Concurrent.Promise p = Concurrent.Promise();
  Session s = Session();

  if (_maxtime) s->maxtime = _maxtime;
  if (_timeout) s->timeout = _timeout;

  s->async_do_method_url(http_method, url, variables, data, headers, 0,
                         lambda (Result ok) {
                           p->success(ok);
                         },
                         lambda (Result fail) {
                           p->failure(fail);
                         },
                         ({}));
  return p->future();
}


//! HTTP result class. Consider internal.
//!
//! @seealso
//!  @[Success], @[Failure]
class Result
{
  //! Internal result mapping
  protected mapping result;

  //! Return @tt{true@} if scuccess, @tt{false@} otherwise
  public bool `ok();

  //! The HTTP response headers
  public mapping `headers()
  {
    return result->headers;
  }

  //! The host that was called in the request
  public string `host()
  {
    return result->host;
  }

  //! The HTTP status of the response, e.g @tt{200@}, @tt{201@}, @tt{404@}
  //! and so on.
  public int `status()
  {
    return result->status;
  }

  //! The textual representation of @[status].
  public string `status_description()
  {
    return result->status_desc;
  }

  //! @ignore
  protected void create(mapping _result)
  {
    //TRACE("this_program(%O)\n", _result->headers);
    result = _result;
  }
  //! @endignore
}


//! A class representing a successful request and its response. An instance of
//! this class will be given as argument to the
//! @[Concurrent.Future()->on_success()] callback registered on the returned
//! @[Concurrent.Future] object from @[get_url()], @[post_url()],
//! @[delete_url()], @[put_url()] or @[do_method()].
class Success
{
  inherit Result;

  public bool `ok() { return true; }

  //! The response body, i.e the content of the requested URL
  public string `data() { return result->data; }

  //! Returns the value of the @tt{content-length@} header.
  public int `length()
  {
    return headers && (int)headers["content-length"];
  }

  //! Returns the content type of the requested document
  public string `content_type()
  {
    if (string ct = (headers && headers["content-type"])) {
      sscanf (ct, "%s;", ct);
      return ct;
    }
  }

  //! Returns the content encoding of the requested document, if given by the
  //! response headers.
  public string `content_encoding()
  {
    if (string ce = (headers && headers["content-type"])) {
      if (sscanf (ce, "%*s;%*scharset=%s", ce) == 3) {
        if (ce[0] == '"' || ce[0] == '\'') {
          ce = ce[1..<1];
        }
        return ce;
      }
    }
  }
}


//! A class representing a failed request. An instance of
//! this class will be given as argument to the
//! @[Concurrent.Future()->on_failure()] callback registered on the returned
//! @[Concurrent.Future] object from @[get_url()], @[post_url()],
//! @[delete_url()], @[put_url()] or @[do_method()].
class Failure
{
  inherit Result;
  public bool `ok() { return false; }
}


//! Internal class for the actual HTTP requests
protected class Session
{
  inherit Protocols.HTTP.Session : parent;

  public int(0..) maxtime, timeout;

  Request async_do_method_url(string method,
                              URL url,
                              void|mapping query_variables,
                              void|string|mapping data,
                              void|mapping extra_headers,
                              function callback_headers_ok,
                              function callback_data_ok,
                              function callback_fail,
                              array callback_arguments)
  {
    return ::async_do_method_url(method, url, query_variables, data,
                                 extra_headers, callback_headers_ok,
                                 callback_data_ok, callback_fail,
                                 callback_arguments);
  }


  class Request
  {
    inherit parent::Request;

    protected void async_fail(object q)
    {
      mapping ret = ([
        "status"      : q->status,
        "status_desc" : q->status_desc,
        "host"        : q->host,
        "headers"     : copy_value(q->headers)
      ]);

      // clear callbacks for possible garbation of this Request object
      con->set_callbacks(0, 0);

      function fc = fail_callback;
      set_callbacks(0, 0, 0); // drop all references
      extra_callback_arguments = 0;

      if (fc) {
        fc(Failure(ret));
      }
    }


    protected void async_ok(object q)
    {
      TRACE("async_ok: %O -> %s!\n", q, q->host);

      ::check_for_cookies();

      if (con->status >= 300 && con->status < 400 &&
          con->headers->location && follow_redirects)
      {
        Standards.URI loc = Standards.URI(con->headers->location,url_requested);
        TRACE("New location: %O\n", loc);

        if (loc->scheme == "http" || loc->scheme == "https") {
          destroy(); // clear
          follow_redirects--;
          do_async(prepare_method("GET", loc));
          return;
        }
      }

      // clear callbacks for possible garbation of this Request object
      con->set_callbacks(0, 0);

      if (data_callback) {
        con->timed_async_fetch(async_data, async_fail); // start data downloading
      }
      else {
        extra_callback_arguments = 0; // to allow garb
      }
    }


    protected void async_data()
    {
      mapping ret = ([
        "host"        : con->host,
        "status"      : con->status,
        "status_desc" : con->status_desc,
        "headers"     : copy_value(con->headers),
        "data"        : con->data()
      ]);

      // clear callbacks for possible garbation of this Request object
      con->set_callbacks(0, 0);

      if (data_callback) {
        data_callback(Success(ret));
      }

      extra_callback_arguments = 0;
    }
  }


  class SessionQuery
  {
    inherit parent::SessionQuery;

    protected void create()
    {
      if (Session::maxtime) {
        this::maxtime = Session::maxtime;
      }

      if (Session::timeout) {
        this::timeout = Session::timeout;
      }
    }
  }
}
