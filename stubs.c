#ifndef HAVE_DCGETTEXT
const char *dcgettext (const char *domain, const char *msg, int category)
{
  return msg;
}
#endif /* !HAVE_DCGETTEXT */

#ifndef HAVE_LIBINTL_DGETTEXT
const char *libintl_dgettext (const char *domain, const char *msg)
{
  return msg;
}
#endif /* !HAVE_LIBINTL_DGETTEXT */
