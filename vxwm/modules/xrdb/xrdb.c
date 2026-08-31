void
loadxrdb()
{
  Display *display;
  char * resm;
  XrmDatabase xrdb;
  char *type;
  XrmValue value;

  display = XOpenDisplay(NULL);

  if (display != NULL) {
    resm = XResourceManagerString(display);

    if (resm != NULL) {
      xrdb = XrmGetStringDatabase(resm);

      if (xrdb != NULL) {
        /* fallback to pywal generic *.colorX */
        XRDB_LOAD_COLOR("*.color0", normbgcolor);
        XRDB_LOAD_COLOR("*.color7", normfgcolor);
        XRDB_LOAD_COLOR("*.color8", normbordercolor);
        XRDB_LOAD_COLOR("*.color4", selbgcolor);
        XRDB_LOAD_COLOR("*.color7", selfgcolor);
        XRDB_LOAD_COLOR("*.color4", selbordercolor);
        /* custom dwm keys override */
        XRDB_LOAD_COLOR("dwm.normbgcolor",     normbgcolor);
        XRDB_LOAD_COLOR("dwm.normfgcolor",     normfgcolor);
        XRDB_LOAD_COLOR("dwm.normbordercolor", normbordercolor);
        XRDB_LOAD_COLOR("dwm.selbgcolor",      selbgcolor);
        XRDB_LOAD_COLOR("dwm.selfgcolor",      selfgcolor);
        XRDB_LOAD_COLOR("dwm.selbordercolor",  selbordercolor);
      }
    }
  }

  XCloseDisplay(display);
}

void
xrdb(const Arg *arg)
{
  loadxrdb();
  int i;
  for (i = 0; i < LENGTH(colors); i++)
                scheme[i] = drw_scm_create(drw, colors[i], 3);
  focus(NULL);
  arrange(NULL);
}

