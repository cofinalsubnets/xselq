#include <xcb/xcb.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define EV_TYPE(e) ((e)->response_type & 0x7f)
#define SECTION(s) printf("%-*s", 12, s ":")

int init_x();
void close_x();
xcb_atom_t str2atom(const char*);
char *atom2str(xcb_atom_t);
int get_window_property(xcb_window_t, xcb_atom_t, xcb_atom_t, void**);
void xselq(const char*);
void print_owner_info(xcb_atom_t);
void print_targets_info(xcb_atom_t);

xcb_connection_t *X = NULL;
xcb_window_t win;

int init_x() {
  if (!(X = xcb_connect(NULL, NULL)))
    return 0;

  xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(X)).data;
  win = xcb_generate_id(X);
  xcb_create_window(X, screen->root_depth, win, screen->root,
      0, 0, 1, 1, 0, XCB_COPY_FROM_PARENT, screen->root_visual,
      XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK,
      (uint32_t[]) {screen->black_pixel, 1, XCB_EVENT_MASK_PROPERTY_CHANGE});
  xcb_map_window(X, win);
  return 1;
}

void close_x() {
  xcb_destroy_window(X, win);
  xcb_disconnect(X);
}

char *atom2str(xcb_atom_t a) {
  xcb_get_atom_name_reply_t *namer = xcb_get_atom_name_reply(X,
      xcb_get_atom_name(X, a), NULL);

  char *ret = strndup(xcb_get_atom_name_name(namer),
                      xcb_get_atom_name_name_length(namer));

  free(namer);
  return ret;
}

xcb_atom_t str2atom(const char *str) {
  xcb_atom_t ret = XCB_NONE;
  xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(X,
      xcb_intern_atom(X, 0, strlen(str), str), NULL);

  if (reply) {
    ret = reply->atom;
    free(reply);
  }

  return ret;
}

void print_owner_info(xcb_atom_t sel) {
  xcb_get_selection_owner_reply_t *reply = xcb_get_selection_owner_reply(
      X, xcb_get_selection_owner(X, sel), NULL);

  if (reply && reply->owner) {
    char *data = NULL;
    get_window_property(reply->owner, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, (void**) &data);
    printf("%s (%d)\n", data, reply->owner);
    if (data) free(data);
    free(reply);
  }
  else puts("(none)");
}

void print_targets_info(xcb_atom_t sel) {
  xcb_generic_event_t *ev;

  xcb_convert_selection(X, win, sel, str2atom("TARGETS"),
      sel, XCB_CURRENT_TIME);
  xcb_flush(X);

  while (EV_TYPE(ev = xcb_wait_for_event(X)) != XCB_SELECTION_NOTIFY) free(ev);


  if (((xcb_selection_notify_event_t*) ev)->property != XCB_NONE) {
    xcb_atom_t *targets = NULL;
    int bytes = get_window_property(win, sel, XCB_ATOM_ATOM, (void**) &targets),
        len = bytes / sizeof(xcb_atom_t);
    char *data;

    for (int i=0; i<len;) {
      printf("%s", data = atom2str(targets[i]));
      if (++i<len) putchar(' ');
      free(data);
    }
    free(targets);
    xcb_delete_property(X, win, sel);
  }

  free(ev);
  putchar('\n');
}


int get_window_property(xcb_window_t w, xcb_atom_t prop, xcb_atom_t type, void **dest) {
  int len = 0;

  xcb_get_property_reply_t *reply = xcb_get_property_reply(X,
      xcb_get_property(X, 0, w, prop, type, 0, 32),
      NULL);

  if (reply) {
    if (reply->type == type) {
      len = xcb_get_property_value_length(reply);
      *dest = malloc(len);
      memcpy(*dest, xcb_get_property_value(reply), len);
    }
    free(reply);
  }

  return len;
}

void xselq(const char *selname) {
  xcb_atom_t sel = str2atom(selname);

  SECTION("Selection");
  puts(selname);

  SECTION("Owner");
  print_owner_info(sel);

  SECTION("Targets");
  print_targets_info(sel);
}

int main(int argc, const char *argv[]) {
  static const char *defaults[] = {"PRIMARY", "SECONDARY", "CLIPBOARD"};

  if (argc == 1) argc = 3, argv = defaults;
  else argc--, argv++;

  if (!init_x()) return EXIT_FAILURE;

  for (int i=0; i<argc;) {
    xselq(argv[i]);
    if (++i<argc) putchar('\n');
  }

  close_x();
  return EXIT_SUCCESS;
}

