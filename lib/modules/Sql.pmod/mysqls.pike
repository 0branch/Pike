/*
 * Glue for the Mysql-module using SSL
 */

//! Implements SQL-urls for
//!   @tt{mysqls://[user[:password]@@][hostname][:port][/database]@}
//!
//! Sets the connection to SSL-mode, and sets the default configuration
//! file to @expr{"/etc/my.cnf"@}.
//!
//! @fixme
//!   Ought to load a suitable default configuration file for Win32 too.
//!
//! @note
//!   This connection method only exists if the Mysql-module has been
//!   compiled with SSL-support.

#pike __REAL_VERSION__
#require constant(Mysql.mysql.CLIENT_SSL)

// Cannot dump this since the #require check may depend on the
// presence of system libs at runtime.
optional constant dont_dump_program = 1;

inherit Sql.mysql;

void create(string host,
	    string db,
	    string user,
	    string password,
	    mapping(string:mixed)|void options)
{
  if (!mappingp(options))
    options = ([ ]);

  options->connect_options |= CLIENT_SSL;

  if (!options->mysql_config_file)
    options->mysql_config_file = "/etc/my.cnf";

  ::create(host||"", db||"", user||"", password||"", options);
}
