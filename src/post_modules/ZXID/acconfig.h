/*
|| This file is part of Pike. For copyright information see COPYRIGHT.
|| Pike is distributed under GPL, LGPL and MPL. See the file COPYING
|| for more information.
*/

#ifndef ZXID_CONFIG_H
#define ZXID_CONFIG_H

@TOP@
@BOTTOM@

/* Define this if you want the ZXID module. */
#undef HAVE_ZXID

/* Define this to the number of arguments that zxid_parse_cgi() wants. */
#undef HAVE_ZXID_PARSE_CGI

/* Define this if your struct zxid_conf has the burl member. */
#undef HAVE_STRUCT_ZXID_CONF_BURL

/* Define this if your struct zxid_cgi has the uri_path member. */
#undef HAVE_STRUCT_ZXID_CGI_URI_PATH

#endif
