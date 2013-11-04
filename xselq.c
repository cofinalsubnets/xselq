#include <xcb/xcb.h>
#include <time.h>
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define XCB_EVENT_RESPONSE_TYPE_MASK   (0x7f) /* see xcb_event.h */
#define EV_TYPE(e)     (e->response_type & XCB_EVENT_RESPONSE_TYPE_MASK)
#define COLWIDTH 12
#define SECTION(s) printf("%-*s", COLWIDTH, s ":")

typedef struct XSelectionTargetInfo {
  xcb_atom_t atom;
  char *name;
  struct XSelectionTargetInfo *next;
} XSelectionTargetInfo;

typedef struct {
  xcb_window_t window;
  char *name;
  XSelectionTargetInfo *targets;
} XSelectionOwnerInfo;

typedef struct {
  const char *name;
  xcb_atom_t atom;
  XSelectionOwnerInfo *owner;
} XSelectionInfo;

void show_selection_info(XSelectionInfo*);
void show_selection_owner_info(XSelectionOwnerInfo*);
void show_selection_target_info(XSelectionTargetInfo*);
void free_selection_info(XSelectionInfo*);
void free_selection_owner_info(XSelectionOwnerInfo*);
void free_selection_target_info(XSelectionTargetInfo*);
int init_x();
int _main(int, const char*[]);
int is_targets_response(xcb_generic_event_t*, xcb_atom_t);
char *get_window_name(xcb_window_t);
xcb_atom_t str2atom(const char*);
char *atom2str(xcb_atom_t);
int get_window_property(xcb_window_t, xcb_atom_t, xcb_atom_t, void**);
int request_targets(xcb_atom_t, xcb_window_t);
XSelectionTargetInfo *get_targets(xcb_atom_t, xcb_window_t);
XSelectionOwnerInfo *get_selection_owner_info(xcb_atom_t);
XSelectionInfo *get_selection_info(const char*);
xcb_window_t get_selection_owner_window(xcb_atom_t);

xcb_connection_t *X = NULL;
xcb_window_t win;
const char *progname;

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

int _main(int argc, const char *argv[]) {
  static const char *defaults[] = {"PRIMARY", "SECONDARY", "CLIPBOARD"};

  if (argc == 0)
    return _main(3, defaults);

  if (!init_x()) {
    fprintf(stderr, "%s: fatal: can't connect to X server.\n", progname);
    return EXIT_FAILURE;
  }

  XSelectionInfo *sel;
  for (int i=0; i<argc;) {
    sel = get_selection_info(argv[i]);
    show_selection_info(sel);
    free_selection_info(sel);
    if (++i < argc) puts("");
  }

  return EXIT_SUCCESS;
}

int main(int argc, const char *argv[]) {
  progname = argv[0];
  return _main(argc - 1, argv + 1);
}

void show_selection_info(XSelectionInfo *sel) {
  SECTION("Selection");
  puts(sel->name);
  show_selection_owner_info(sel->owner);
}

void show_selection_owner_info(XSelectionOwnerInfo *owner) {
  SECTION("Owner");
  if (!owner) puts("none");
  else {
    printf("%d (%s)\n", owner->window, owner->name);
    show_selection_target_info(owner->targets);
  }
}

void show_selection_target_info(XSelectionTargetInfo *target) {
  SECTION("Targets");
  for (;target; target=target->next)
    printf("%s ", target->name);
  puts("");
}

XSelectionInfo *get_selection_info(const char *name) {
  XSelectionInfo *sel = malloc(sizeof(XSelectionInfo));
  sel->name = name;
  sel->atom = str2atom(name);
  sel->owner = get_selection_owner_info(sel->atom);
  return sel;
}

XSelectionOwnerInfo *get_selection_owner_info(xcb_atom_t sel) {
  xcb_window_t owner_window = get_selection_owner_window(sel);
  if (!owner_window) return NULL;

  XSelectionOwnerInfo *owner = malloc(sizeof(XSelectionOwnerInfo));
  owner->window  = owner_window;
  owner->name    = get_window_name(owner_window);
  owner->targets = get_targets(sel, owner_window);
  return owner;
}

XSelectionTargetInfo *get_targets(xcb_atom_t sel, xcb_window_t owner) {
  if (!request_targets(sel, owner)) return NULL;

  xcb_atom_t *targets;
  XSelectionTargetInfo *ret = NULL, *tmp;
  int bytes = get_window_property(win, sel, XCB_ATOM_ATOM, (void**) &targets),
      len = bytes / sizeof(xcb_atom_t);

  for (int i=len-1; i>=0; i--) {
    tmp = malloc(sizeof(XSelectionTargetInfo));
    tmp->atom = targets[i];
    tmp->name = atom2str(targets[i]);
    tmp->next = ret;
    ret = tmp;
  }

  xcb_delete_property(X, win, sel);
  xcb_flush(X);
  return ret;
}

int request_targets(xcb_atom_t sel, xcb_window_t owner) {
  xcb_convert_selection(X, win, sel, str2atom("TARGETS"),
      sel, XCB_CURRENT_TIME);
  xcb_flush(X);

  int tries = 5, resp = 0;
  struct timespec ts = { 0, 100000000 };
  xcb_generic_event_t *ev;

  for (int i=0; i<tries; i++) {
    if ((ev = xcb_poll_for_event(X))) {
      resp = is_targets_response(ev, sel);
      free(ev);
      if (resp) return 1;
    }
    nanosleep(&ts, NULL);
  }

  return 0;
}

int is_targets_response(xcb_generic_event_t *gev, xcb_atom_t sel) {
  if (EV_TYPE(gev) != XCB_SELECTION_NOTIFY) return 0;
  xcb_selection_notify_event_t *ev = (xcb_selection_notify_event_t*) gev;
  return ev->property == sel && ev->target == str2atom("TARGETS");
}

int get_window_property(xcb_window_t w, xcb_atom_t prop, xcb_atom_t type, void **dest) {
  int len = 0;

  xcb_get_property_reply_t *reply = xcb_get_property_reply(X,
      xcb_get_property(X, 0, w, prop, type, 0, 32),
      NULL);

  if (reply) {
    if (reply->type == type) {
      len = xcb_get_property_value_length(reply);
      *dest = malloc(len+1);
      memcpy(*dest, xcb_get_property_value(reply), len);
    }
    free(reply);
  }

  return len;
}

char *get_window_name(xcb_window_t w) {
  char *data = NULL;
  get_window_property(w, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, (void**) &data);
  return data;
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

void free_selection_info(XSelectionInfo *sel) {
  if (sel->owner) free_selection_owner_info(sel->owner);
  free(sel);
}

void free_selection_owner_info(XSelectionOwnerInfo *owner) {
  if (owner->name) free(owner->name);
  if (owner->targets) free_selection_target_info(owner->targets);
  free(owner);
}

void free_selection_target_info(XSelectionTargetInfo *target) {
  XSelectionTargetInfo *tmp;
  while (target) {
    free(target->name);
    tmp = target->next;
    free(target);
    target = tmp;
  }
}

xcb_window_t get_selection_owner_window(xcb_atom_t sel) {
  xcb_window_t ret = 0;
  xcb_get_selection_owner_reply_t *reply = xcb_get_selection_owner_reply(
      X, xcb_get_selection_owner(X, sel), NULL);

  if (reply) {
    ret = reply->owner;
    free(reply);
  }

  return ret;
}

