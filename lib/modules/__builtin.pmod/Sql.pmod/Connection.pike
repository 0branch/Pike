#pike __REAL_VERSION__

//! This class is the base class for connections to SQL servers. It is a
//! generic interface on top of which the DB server specific implement
//! their specifics.
//!
//! This class thus serves as an interface guideline for the DB server
//! specific connection classes.
//!
//! @section Untyped and typed mode
//!
//!   The query results are returned in different ways depending on the
//!   query functions used: The @tt{..typed_query@} functions select
//!   typed mode, while the other query functions uses the older untyped
//!   mode.
//!
//!   @ul
//!     @item
//!       In untyped mode, all values except SQL NULL are returned as
//!       strings in their display representation, and SQL NULL is returned
//!       as zero.
//!     @item
//!       In typed mode, values are returned in pike native form where it
//!       works well. That means at least that SQL integer fields are
//!       returned as pike integers, floats as floats, SQL NULL as
//!       @[Val.null], and of course strings still as strings. The
//!       representation of other SQL types depend on the capabilities of
//!       the server specific backends. It's also possible that floats in
//!       some cases are represented in other ways if too much precision is
//!       lost in the conversion to pike floats.
//!   @endul
//!
//! @endsection
//!
//! @note
//!   For historical reasons, there may be server specific backends that
//!   operate differently from what is described here, e.g. some that
//!   return a bit of typed data in untyped mode.
//!
//! @note
//!   Typed operation was not supported at all prior to Pike 7.8.363,
//!   and may not be supported for all databases.

#define ERROR(X ...)	predef::error(X)

//! Compatibility interface.
//!
//! This used to be a variable containing the server specific
//! connection object used for the actual SQL queries.
//!
//! As the wrapper no longer exists, this symbol now
//! just evaluates to the object.
__deprecated__ this_program `master_sql()
{
  return this;
}

//! Convert all field names in mappings to @[lower_case].
//! Only relevant to databases which only implement @[big_query()],
//! and use upper/mixed-case fieldnames (eg Oracle).
//! @int
//! @value 0
//!   No (default)
//! @value 1
//!   Yes
//! @endint
int(0..1) case_convert;

//! @decl string quote(string s)
//! Quote a string @[s] so that it can safely be put in a query.
//!
//! All input that is used in SQL-querys should be quoted to prevent
//! SQL injections.
//!
//! Consider this harmfull code:
//! @code
//!   string my_input = "rob' OR name!='rob";
//!   string my_query = "DELETE FROM tblUsers WHERE name='"+my_input+"'";
//!   my_db->query(my_query);
//! @endcode
//!
//! This type of problems can be avoided by quoting @tt{my_input@}.
//! @tt{my_input@} would then probably read something like
//! @i{rob\' OR name!=\'rob@}
//!
//! Usually this is done - not by calling @[quote] explicitly - but through
//! using a @[sprintf] like syntax:
//! @code
//!   string my_input = "rob' OR name!='rob";
//!   my_db->query("DELETE FROM tblUsers WHERE name=%s", my_input);
//! @endcode
//!
//! The default implementation quotes single quotes by doubling them.
string quote(string s)
{
  return replace(s, "\'", "\'\'");
}

private constant timezone = localtime (0)->timezone;

//! Converts a system time value to an appropriately formatted time
//! spec for the database.
//!
//! @param time
//!   Time to encode.
//!
//! @param date
//!   If nonzero then @[time] is taken as a "full" unix time spec
//!   (where the date part is ignored), otherwise it's converted as a
//!   seconds-since-midnight value.
//!
//! The default implementation returns a colon-separated ISO 9601 time.
string encode_time(int time, int|void date)
{
  if (date) {
    if (!time) return "00:00:00";
    mapping(string:int) ct = localtime (time);
    return sprintf("%02d:%02d:%02d", ct->hour, ct->min, ct->sec);
  }
  else
    return sprintf("%02d:%02d:%02d",
		   (time / 3600) % 24,
		   (time / 60) % 60,
		   time % 60);
}

//! Converts a database time spec to a system time value.
//!
//! @param timestr
//!   Time spec to decode.
//!
//! @param date
//!   Take the date part from this system time value. If zero, a
//!   seconds-since-midnight value is returned.
//!
//! @returns
//!   Returns the number of seconds since midnight.
int decode_time(string timestr, int|void date)
{
  int hour = 0, min = 0, sec = 0;
  if (sscanf(timestr, "%d:%d:%d", hour, min, sec) <= 1)
    sscanf(timestr, "%2d%2d%2d", hour, min, sec);
  if (date && (hour || min || sec)) {
    mapping(string:int) ct = localtime(date);
    return mktime(sec, min, hour,
		  ct->mday, ct->mon, ct->year,
		  ct->isdst, ct->timezone);
  }
  else return (hour * 60 + min) * 60 + sec;
}

//! Converts a system time value to an appropriately formatted
//! date-only spec for the database.
//!
//! @param t
//!   Time to encode.
//!
//! The default implementation returns an ISO 9601 date.
string encode_date(int time)
{
  if (!time) return "0000-00-00";
  mapping(string:int) ct = localtime (time);
  return sprintf("%04d-%02d-%02d", ct->year + 1900, ct->mon + 1, ct->mday);
}

//! Converts a database date-only spec to a system time value.
//!
//! @param datestr
//!   Date spec to decode.
//!
//! @returns
//!   Returns the number of seconds since 1970-01-01T00:00:00 UTC
//!   to 00:00:00 at the specified date in the current timezone.
int decode_date(string datestr)
{
  int year = 0, mon = 0, mday = 0, n;
  n = sscanf (datestr, "%d-%d-%d", year, mon, mday);
  if (n <= 1) n = sscanf (datestr, "%4d%2d%2d", year, mon, mday);
  if (year || mon || mday)
    return mktime (0, 0, 0,
		   n == 3 ? mday : 1, n >= 2 && mon - 1, year - 1900,
		   -1, timezone);
  else return 0;
}

//! Converts a system time value to an appropriately formatted
//! date and time spec for the database.
//!
//! @param time
//!   Time to encode.
//!
//! The default implementation returns an ISO 9601 timestamp.
string encode_datetime(int time)
{
  if (!time) return "0000-00-00T00:00:00";
  mapping(string:int) ct = localtime (time);
  return sprintf ("%04d-%02d-%02dT%02d:%02d:%02d",
		  ct->year + 1900, ct->mon + 1, ct->mday,
		  ct->hour, ct->min, ct->sec);
}

//! Converts a database date and time spec to a system time value.
//!
//! @param datetime
//!   Date and time spec to decode.
//!
//! @returns
//!   Returns the number of seconds since 1970-01-01T00:00:00 UTC
//!   to the specified date and time in the current timezone.
//!
//! The default implementation decodes an ISO 9601 timestamp.
int decode_datetime(string datetime)
{
  array(string) a = datetime / "T";
  if (sizeof (a) == 2)
    return decode_date(a[0]) + decode_time(a[1]);

  a = datetime / " ";
  if (sizeof (a) == 2)
    return decode_date(a[0]) + decode_time(a[1]);

  int n = sizeof(datetime);
  if (n >= 12)
    return decode_date(datetime[..n-7]) + decode_time(datetime[n-6..n-1]);
  else
    return decode_date(datetime);
}

//! @decl void create(string host)
//! @decl void create(string host, string db)
//! @decl void create(string host, mapping(string:int|string) options)
//! @decl void create(string host, string db, string user)
//! @decl void create(string host, string db, string user, @
//!                   string password)
//! @decl void create(string host, string db, string user, @
//!                   string password, mapping(string:int|string) options)
//!
//! Create a new SQL connection.
//!
//! @param host
//!   @mixed
//!     @type string
//!       Connect to the server specified.
//!     @type int(0..0)
//!       Access through a UNIX-domain socket or similar.
//!   @endmixed
//!
//! @param db
//!   Select this database.
//!
//! @param user
//!   User name to access the database as.
//!
//! @param password
//!   Password to access the database.
//!
//! @param options
//!   Optional mapping of options.
//!   See the SQL-database documentation for the supported options.
//!   (eg @[Mysql.mysql()->create()]).
//!
//! @note
//!   Support for @[options] was added in Pike 7.3.
//!
//! @note
//!   The base class (@[__builtin.Sql.Connection]) only has a prototype.
void create(string host, void|string|mapping(string:int|string) db,
	    void|string user, void|string _password,
	    void|mapping(string:int|string) options);

//! Returns true if the connection seems to be open.
//!
//! @note
//!   This function only checks that there's an open connection,
//!   and that the other end hasn't closed it yet. No data is
//!   sent over the connection.
//!
//!   For a more reliable check of whether the connection
//!   is alive, please use @[ping()].
//!
//! @note
//!   The default implementation just returns the value @expr{1@}.
//!
//! @seealso
//!   @[ping()]
int is_open()
{
  return 1;
}

//! @decl int ping()
//!
//! Check whether the connection is alive.
//!
//! @returns
//!   Returns one of the following:
//!   @int
//!     @value 0
//!       Everything ok.
//!     @value 1
//!       The connection reconnected automatically.
//!     @value -1
//!       The server has gone away, and the connection is dead.
//!   @endint
//!
//! The default implementation performs a trivial select to
//! check the connection.
//!
//! @seealso
//!   @[is_open()]
int ping()
{
  if (!is_open()) return -1;
  catch {
    return sizeof(query("SELECT 0 AS zero") || ({})) - 1;
  };
  return -1;
}

void set_charset (string charset)
//! Changes the charset that the connection uses for queries and
//! returned text strings.
//!
//! @param charset
//!   The charset to use. The valid values and their meanings depends
//!   on the database brand. However, the special value
//!   @expr{"unicode"@} (if supported) selects a mode where the query
//!   and result strings are unencoded (and possibly wide) unicode
//!   strings.
//!
//! @throws
//!   An error is thrown if the connection doesn't support the
//!   specified charset, or doesn't support charsets being set this
//!   way at all.
//!
//! @note
//!   See the @expr{set_charset@} functions for each database
//!   connection type for further details about the effects on the
//!   connection.
//!
//! @seealso
//!   @[get_charset], @[Sql.mysql.set_charset]
{
  predef::error("This database connection does not "
		"support charset switching.\n");
}

string get_charset()
//! Returns the (database dependent) name of the charset used for (at
//! least) query strings. Returns zero if the connection doesn't
//! support charsets this way (typically means that a call to
//! @[set_charset] will throw an error).
//!
//! @seealso
//!   @[set_charset], @[Sql.mysql.get_charset]
{
  return UNDEFINED;
}

protected array(mapping(string:mixed)) res_obj_to_array(.Result res_obj)
{
  if (!res_obj)
    return 0;

  array(mapping(string:mixed)) res = ({});
  while (res_obj)
  {
    // Not very efficient, but sufficient
    array(string) fieldnames;
    array(mixed) row;

    array(mapping) fields = res_obj->fetch_fields();
    if(!sizeof(fields)) return ({});

    int has_table = fields[0]->table && fields[0]->table!="";

    if(has_table)
      fieldnames = (map(fields,
			lambda (mapping(string:mixed) m) {
			  return (m->table||"") + "." + m->name;
			}) +
		    fields->name);
    else
      fieldnames = fields->name;

    if (case_convert)
      fieldnames = map(fieldnames, lower_case);

    if(has_table)
      while (row = res_obj->fetch_row())
	res += ({ mkmapping(fieldnames, row + row) });
    else
      while (row = res_obj->fetch_row())
	res += ({ mkmapping(fieldnames, row) });

    // Try the next result.
    res_obj = res_obj->next_result && res_obj->next_result();
  }
  return res;
}

//! Return last error message.
int|string error()
{
  return "Unknown error";
}

//! Return last SQLSTATE.
//!
//! The SQLSTATE error codes are specified in ANSI SQL.
string sqlstate()
{
  return "IM001"; // "driver does not support this function"
}

//! Select database to access.
void select_db(string db);

//! Compiles the query (if possible). Otherwise returns it as is.
//! The resulting object can be used multiple times to the query
//! functions.
//!
//! @param q
//!   SQL-query to compile.
//!
//! @note
//!   The default implementation just returns @[q] unmodified.
//!
//! @seealso
//! @[query], @[typed_query], @[big_query], @[big_typed_query],
//! @[streaming_query], @[streaming_typed_query]
string|object compile_query(string q)
{
  return q;
}

//! Build a raw SQL query, given the cooked query and the variable bindings
//! It's meant to be used as an emulation engine for those drivers not
//! providing such a behaviour directly (i.e. Oracle).
//! The raw query can contain some variables (identified by prefixing
//! a colon to a name or a number (i.e. ":var" or  ":2"). They will be
//! replaced by the corresponding value in the mapping.
//!
//! @param query
//!   The query.
//!
//! @param bindings
//!   Mapping containing the variable bindings. Make sure that
//!   no confusion is possible in the query. If necessary, change the
//!   variables' names.
protected string emulate_bindings(string query,
				  mapping(string|int:mixed) bindings)
{
  array(string)k, v;
  v = map(values(bindings),
	  lambda(mixed m) {
	    if(undefinedp(m))
	      return "NULL";
	    if (objectp (m) && m->is_val_null)
	      // Note: Could need bug compatibility here - in some cases
	      // we might be passed a null object that can be cast to
	      // "", and before this it would be. This is an observed
	      // compat issue in comment #7 in [bug 5900].
	      return "NULL";
	    if(multisetp(m))
	      return sizeof(m) ? indices(m)[0] : "";
	    return "'"+(intp(m)?(string)m:quote((string)m))+"'";
	  });
  // Throws if mapping key is empty string.
  k = map(indices(bindings),lambda(string s){
			      return ( (stringp(s)&&s[0]==':') ?
				       s : ":"+s);
			    });
  return replace(query,k,v);
}

//! Handle @[sprintf]-based quoted arguments
//!
//! @param query
//!   The query as sent to one of the query functions.
//!
//! @param extraargs
//!   The arguments following the query.
//!
//! @param bindings
//!   Optional bindings mapping to which additional bindings will be
//!   added. It's returned as the second element in the return value.
//!   A new mapping is used if this isn't specified.
//!
//! @returns
//!   Returns an array with two elements:
//!   @array
//!     @elem string 0
//!       The query altered to use bindings-syntax.
//!     @elem mapping(string|int:mixed) 1
//!       A bindings mapping. Zero if no bindings were added.
//!   @endarray
protected array(string|mapping(string|int:mixed))
  handle_extraargs(string query, array(mixed) extraargs,
		   mapping(string|int:mixed)|void bindings)
{
  array(mixed) args=allocate(sizeof(extraargs));
  if (!bindings) bindings = ([]);

  int a, new_bindings;
  foreach(extraargs; int j; mixed s) {
    if (stringp(s) || multisetp(s)) {
      string bind_name;
      do {
	bind_name = ":arg"+(a++);
      } while (has_index(bindings, bind_name));
      args[j]=bind_name;
      bindings[bind_name] = s;
      new_bindings = 1;
      continue;
    }
    if (intp(s) || floatp(s)) {
      args[j] = s || .zero;
      continue;
    }
    if (objectp(s) && s->is_val_null) {
      args[j] = .null_arg;
      continue;
    }
    ERROR("Wrong type to query argument %d: %O\n", j + 1, s);
  }

  return ({sprintf(query,@args), new_bindings && bindings});
}

//! Send an SQL query synchronously to the SQL-server and return
//! the results in untyped mode.
//!
//! @param q
//!   Query to send to the SQL-server. This can either be a string with the
//!   query, or a previously compiled query (see @[compile_query()]).
//!
//! @param bindings
//!   A mapping containing bindings of variables used in the query.
//!   A variable is identified by a colon (:) followed by a name or number.
//!   Each index in the mapping corresponds to one such variable, and the
//!   value for that index is substituted (quoted) into the query wherever
//!   the variable is used.
//!
//! @code
//! res = query("SELECT foo FROM bar WHERE gazonk=:baz",
//!             ([":baz":"value"]));
//! @endcode
//!
//!   Binary values (BLOBs) may need to be placed in multisets.
//!
//! @returns
//!   The result is returned as an @[Sql.sql_result] object in untyped
//!   mode. This allows for having some more info about the result as
//!   well as processing the result in a streaming fashion, although the
//!   result itself wasn't obtained streamingly from the server.
//!
//!   Returns @expr{0@} if the query didn't return any result
//!   (e.g. @tt{INSERT@} or similar).
//!
//! @throws
//!   Throws an exception if the query fails.
//!
//! Called by @[big_query()] after it has processed any bindings.
//!
//! This prototype function is intended for overriding by drivers.
//!
//! @seealso
//!   @[query], @[streaming_query]
protected int|.Result low_big_query(object|string q,
				    mapping(string|int:mixed)|void bindings);

//! Send an SQL query synchronously to the SQL-server and return
//! the results in untyped mode.
//!
//! @param q
//!   Query to send to the SQL-server. This can either be a string with the
//!   query, or a previously compiled query (see @[compile_query()]).
//! @param extraargs
//!   This parameter, if specified, can be in two forms:
//!
//!   @ol
//!     @item
//!     A mapping containing bindings of variables used in the query.
//!     A variable is identified by a colon (:) followed by a name or number.
//!     Each index in the mapping corresponds to one such variable, and the
//!     value for that index is substituted (quoted) into the query wherever
//!     the variable is used.
//!
//! @code
//! res = query("SELECT foo FROM bar WHERE gazonk=:baz",
//!             ([":baz":"value"]));
//! @endcode
//!
//!     Binary values (BLOBs) may need to be placed in multisets.
//!
//!     @item
//!     Arguments as you would use in sprintf. They are automatically
//!     quoted.
//!
//! @code
//! res = query("select foo from bar where gazonk=%s","value");
//! @endcode
//!   @endol
//!
//! The result is returned as an @[Sql.sql_result] object in untyped
//! mode. This allows for having some more info about the result as
//! well as processing the result in a streaming fashion, although the
//! result itself wasn't obtained streamingly from the server. Returns
//! @expr{0@} if the query didn't return any result (e.g. @tt{INSERT@}
//! or similar).
//!
//! The default implementation normalizes @[q] and @[extraargs] to
//! use the bindings mapping (via @[handle_extraargs()]), and calls
//! @[low_big_query()].
//!
//! @note
//! Despite the name, this function is not only useful for "big"
//! queries. It typically has less overhead than @[query] also for
//! ones that return only a few rows.
//!
//! @seealso
//!   @[query], @[low_big_query()], @[streaming_query]
int|.Result big_query(object|string q, mixed ... extraargs)
{
  mapping(string|int:mixed) bindings;

  if (sizeof(extraargs)) {
    if (mappingp(extraargs[0]))
      bindings = extraargs[0];
    else
      [q, bindings] = handle_extraargs(q, extraargs);
  }

  if(bindings) {
    return low_big_query(q, bindings);
  } else {
    return low_big_query(q);
  }
}

//! Send an SQL query synchronously to the SQL-server and return
//! the results in untyped mode.
//!
//! For the arguments, please see the @[big_query()] function.
//!
//! @returns
//!   Returns one of the following on success:
//!   @mixed
//!     @type array(mapping(string:string))
//!       The result as an array of mappings indexed on the name of
//!       the columns. The values are either strings with the display
//!       representations or zero for the SQL NULL value.
//!     @type zero
//!       The value @expr{0@} (zero) if the query didn't return any
//!       result (eg @tt{INSERT@} or similar).
//!   @endmixed
//!
//! @throws
//!   Throws an exception if the query fails.
//!
//! @note
//!   The default implementation calls @[big_query()]
//!   and converts its result.
//!
//! @seealso
//!   @[typed_query], @[big_query], @[streaming_query]
array(mapping(string:string)) query(object|string q,
				    mixed ... extraargs)
{
  return res_obj_to_array(big_query(q, @extraargs));  
}

//! Send an SQL query synchronously to the SQL-server and return
//! the results in typed mode.
//!
//! For the arguments, please see the @[query()] function.
//!
//! The result is returned as an @[Sql.sql_result] object in typed
//! mode. This allows for having some more info about the result as
//! well as processing the result in a streaming fashion, although the
//! result itself wasn't obtained streamingly from the server. Returns
//! @expr{0@} if the query didn't return any result (e.g. @tt{INSERT@}
//! or similar).
//!
//! @note
//!   Typed mode is not supported by all sql databases. If not
//!   supported, an error is thrown.
//!
//! @note
//! Despite the name, this function is not only useful for "big"
//! queries. It typically has less overhead than @[typed_query] also
//! for ones that return only a few rows.
//!
//! Called by @[big_typed_query()] after it has processed any bindings.
//!
//! This prototype function is intended for overriding by drivers.
//!
//! @seealso
//!   @[query], @[typed_query], @[big_query], @[streaming_query]
protected int|.Result low_big_typed_query(object|string q,
					  mapping(string|int:mixed)|void bindings);

//! Send an SQL query synchronously to the SQL-server and return
//! the results in typed mode.
//!
//! For the arguments, please see the @[query()] function.
//!
//! The result is returned as an @[Sql.sql_result] object in typed
//! mode. This allows for having some more info about the result as
//! well as processing the result in a streaming fashion, although the
//! result itself wasn't obtained streamingly from the server. Returns
//! @expr{0@} if the query didn't return any result (e.g. @tt{INSERT@}
//! or similar).
//!
//! @note
//!   Typed mode is not supported by all sql databases. If not
//!   supported, an error is thrown.
//!
//! @note
//! Despite the name, this function is not only useful for "big"
//! queries. It typically has less overhead than @[typed_query] also
//! for ones that return only a few rows.
//!
//! @seealso
//!   @[query], @[typed_query], @[big_query], @[streaming_query]
int|.Result big_typed_query(object|string q, mixed ... extraargs)
{
  mapping(string|int:mixed) bindings;

  if (sizeof(extraargs)) {
    if (mappingp(extraargs[0]))
      bindings = extraargs[0];
    else
      [q, bindings] = handle_extraargs(q, extraargs);
  }

  if (bindings) {
    return low_big_typed_query(q, bindings);
  }
  return low_big_typed_query(q);
}

//! Send an SQL query synchronously to the SQL-server and return
//! the results in typed mode.
//!
//! For the arguments, please see the @[query()] function.
//!
//! @returns
//!   Returns one of the following on success:
//!   @mixed
//!     @type array(mapping(string:mixed))
//!       The result as an array of mappings indexed on the name of
//!       the columns. The values have the appropriate native pike
//!       types where they fit the SQL data types - see the class doc
//!       for details on typed mode.
//!     @type zero
//!       The value @expr{0@} (zero) if the query didn't return any
//!       result (eg @tt{INSERT@} or similar).
//!   @endmixed
//!
//! @note
//!   Typed mode is not supported by all sql databases. If not
//!   supported, an error is thrown.
//!
//! Called by @[big_typed_query()] after it has processed any bindings.
//!
//! This prototype function is intended for overriding by drivers.
//!
//! @seealso
//!   @[query], @[big_typed_query]
array(mapping(string:mixed)) typed_query(object|string q, mixed ... extraargs)
{
  return res_obj_to_array(big_typed_query(q, @extraargs));  
}

//! Send an SQL query synchronously to the SQL-server and return
//! the results streaming in untyped mode.
//!
//! For the arguments, please see the @[low_big_query()] function.
//!
//! The result is returned as a streaming @[Sql.sql_result] object in
//! untyped mode. This allows for having results larger than the
//! available memory, and returning some more info about the result.
//! Returns @expr{0@} if the query didn't return any result (e.g.
//! INSERT or similar). For the other arguments, they are the same as
//! for the @[query()] function.
//!
//! Called by @[streaming_query()] after it has processed any bindings.
//!
//! This function is intended for overriding by drivers.
//!
//! @note
//!   Streaming operation is not supported by all sql databases.
//!   The default implentation falls back to calling @[low_big_query()].
//!
//! @seealso
//!   @[big_query], @[streaming_typed_query]
int|.Result low_streaming_query(object|string q,
				mapping(string|int:mixed)|void bindings)
{
  return low_big_query(q, bindings);
}

//! Send an SQL query synchronously to the SQL-server and return
//! the results streaming in untyped mode.
//!
//! For the arguments, please see the @[query()] function.
//!
//! The result is returned as a streaming @[Sql.sql_result] object in
//! untyped mode. This allows for having results larger than the
//! available memory, and returning some more info about the result.
//! Returns @expr{0@} if the query didn't return any result (e.g.
//! INSERT or similar). For the other arguments, they are the same as
//! for the @[query()] function.
//!
//! @note
//!   Streaming operation is not supported by all sql databases.
//!   If not supported, this function will fall back to calling
//!   @[big_query()].
//!
//! @seealso
//!   @[big_query], @[streaming_typed_query]
int|.Result streaming_query(object|string q, mixed ... extraargs)
{
  mapping(string|int:mixed) bindings;

  if (sizeof(extraargs)) {
    if(mappingp(extraargs[0]))
      bindings = extraargs[0];
    else
      [q, bindings] = handle_extraargs(q, extraargs);
  }

  if(bindings) {
    return low_streaming_query(q, bindings);
  }

  return low_streaming_query(q);
}

//! Send an SQL query synchronously to the SQL-server and return
//! the results streaming in typed mode.
//!
//! For the arguments, please see the @[query()] function.
//!
//! The result is returned as a streaming @[Sql.sql_result] object in
//! typed mode. This allows for having results larger than the
//! available memory, and returning some more info about the result.
//! Returns @expr{0@} if the query didn't return any result (e.g.
//! INSERT or similar).
//!
//!
//! Called by @[streaming_query()] after it has processed any bindings.
//!
//! This function is intended for overriding by drivers.
//!
//! @note
//!   Streaming operation is not supported by all sql databases.
//!
//! @note
//!   Neither streaming operation nor typed results are supported
//!   by all sql databases. The default implentation falls back to
//!   calling @[low_big_typed_query()].
//!
//! @seealso
//!   @[streaming_query], @[big_typed_query]
protected int|.Result low_streaming_typed_query(object|string q,
						mapping(string|int:mixed)|void bindings)
{
  return low_big_typed_query(q, bindings);
}

//! Send an SQL query synchronously to the SQL-server and return
//! the results streaming in typed mode.
//!
//! For the arguments, please see the @[query()] function.
//!
//! The result is returned as a streaming @[Sql.sql_result] object in
//! typed mode. This allows for having results larger than the
//! available memory, and returning some more info about the result.
//! Returns @expr{0@} if the query didn't return any result (e.g.
//! INSERT or similar).
//!
//! @note
//!   Neither streaming operation nor typed results are supported
//!   by all sql databases. If not supported, this function will
//!   fall back to calling @[big_typed_query()].
//!
//! @seealso
//!   @[streaming_query], @[big_typed_query]
int|.Result streaming_typed_query(object|string q, mixed ... extraargs)
{
  mapping(string|int:mixed) bindings;

  if (sizeof(extraargs)) {
    if(mappingp(extraargs[0]))
      bindings = extraargs[0];
    else
      [q, bindings] = handle_extraargs(q, extraargs);
  }

  if(bindings) {
    return low_streaming_typed_query(q, bindings);
  }

  return low_streaming_typed_query(q);
}

//! Create a new database.
//!
//! @param db
//!   Name of database to create.
void create_db(string db);

//! Drop database
//!
//! @param db
//!   Name of database to drop.
void drop_db(string db);

//! Shutdown a database server.
void shutdown()
{
  ERROR("Not supported by this database.\n");
}

//! Reload the tables.
void reload()
{
  // Probably safe to make this a NOOP
}

//! Return info about the current SQL-server.
string server_info()
{
  return "Unknown SQL-server";
}

//! Return info about the connection to the SQL-server.
string host_info()
{
  return "Unknown connection to host";
}

//! List available databases on this SQL-server.
//!
//! @returns
//!   Returns an array with database names on success and @expr{0@}
//!   (zero) on failure.
//!
//! Called by @[list_dbs()].
//!
//! This function is intended for overriding by drivers
//! not supporting wildcard filtering of database names.
//!
//! @note
//!   The default implementation attempts the query
//!   @expr{"SHOW DATABASES"@}.
//!
//! @seealso
//!   @[list_dbs()]
protected array(string) low_list_dbs()
{
  catch {
    array(mapping) res = query("SHOW DATABASES");
    if (res && sizeof(res) && mappingp(res[0])) {
      return map(res, lambda (mapping m) {
			return values(m)[0]; // Hope that there's only one field
		      } );
    }
    return res && ({});
  };

  return 0;
}

//! List available databases on this SQL-server.
//!
//! @param wild
//!   Optional wildcard to match against.
//!
//! This function calls @[low_list_dbs()] and optionally
//! performs wildcard filtering.
//!
//! @seealso
//!   @[low_list_dbs()]
array(string) list_dbs(string|void wild)
{
  array(string) res = list_dbs();

  if (res && wild) {
    res = filter(res,
		 Regexp(replace(wild, ({"%", "_"}), ({".*", "."})))->match);
  }
  return res;
}

//! List tables available in the current database.
//!
//! This function is intended for overriding by drivers
//! not supporting wildcard filtering of table names.
//!
//! @note
//!   The default implementation attempts the query
//!   @expr{"SHOW TABLES"@}.
//!
//! @seealso
//!   @[list_tables()]
protected array(string) low_list_tables()
{
  array(string)|array(mapping(string:mixed))|object res;

  catch {
    array(mapping(string:mixed)) res = query("SHOW TABLES");

    if (res && sizeof(res) && mappingp(res[0])) {
      string col_name = indices(res[0])[0];
      if (sizeof(res[0]) > 1) {
	if (!zero_type(res[0]["TABLE_NAME"])) {
	  col_name = "TABLE_NAME";
	}
      }
      return map(res, lambda (mapping m, string col_name) {
			return m[col_name];
		      }, col_name);
    }
    return res && ({});
  };

  return 0;
}

//! List tables available in the current database.
//!
//! @param wild
//!   Optional wildcard to match against.
//!
//! The default implementation calls @[low_list_tables()].
array(string) list_tables(string|void wild)
{
  array(string) res = low_list_tables();

  if (res && wild) {
    res = filter(res,
		 Regexp(replace(wild, ({"%", "_"}), ({".*", "."})))->match);
  }
  return res;
}

//! List fields available in the specified table
//!
//! @param table
//!   Table to list the fields of.
//!
//! @returns
//!   Returns an array of mappings with at least the fields:
//!   @mapping
//!     @member string "name"
//!       The name of the field.
//!     @member string "table"
//!       The name of the table.
//!   @endmapping
//!
//!   Typically there are also entries for the field types,
//!   field widths and nullability.
//!
//! This function is intended for overriding by drivers
//! not supporting wildcard filtering of field names.
//!
//! @note
//!   The default implementation attempts the query
//!   @expr{"SHOW FIELDS FROM 'table'"@}, and then
//!   performs some normalization of the result.
//!
//! @seealso
//!   @[list_fields()]
protected array(mapping(string:mixed)) low_list_fields(string table)
{

  catch {
    array(mapping(string:mixed)) res =
      query("SHOW FIELDS FROM \'" + table + "\'");

    res = res && map(res,
		     lambda (mapping m, string table) {
		       foreach(indices(m), string str) {
			 // Add the lower case variants
			 string low_str = lower_case(str);
			 if (low_str != str && !m[low_str])
			   m[low_str] = m_delete(m, str);
		       }

		       if ((!m->name) && m->field)
			 m["name"] = m_delete(m, "field");

		       if (!m->table)
			 m["table"] = table;

		       return m;
		     }, table);
    return res;
  };
  return 0;
}

//! List fields available in the specified table
//!
//! @param table
//!   Table to list the fields of.
//!
//! @param wild
//!   Optional wildcard to match against.
//!
//! The default implementation calls @[low_list_fields()]
//! and applies the wild-card filter on the result.
array(mapping(string:mixed)) list_fields(string table, string|void wild)
{
  array(mapping(string:mixed)) res = low_list_fields(table);

  if (res && wild) {
    res =
      filter(res,
	     map(res->name,
		 Regexp(replace(wild, ({"%", "_"}), ({".*", "."})))->match));
  }

  return res;
}
