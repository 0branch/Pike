/*
 * Compat place-holder.
 */

#if 1
inherit Tools.Standalone.autodoc_to_split_html;
#else /* For dev purposes */
inherit __DIR__ + "/../../lib/modules/Tools.pmod/Standalone.pmod/autodoc_to_split_html.pike";
#endif
