/*** includes ***/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

#define KILO_VERSION "0.0.1"

#define CTRL_KEY(K) ((K) & 0x1f)

enum editorKey {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  PAGE_UP,
  PAGE_DOWN,
  HOME_KEY,
  END_KEY,
  DELETE_KEY
};
/*** data ***/

typedef struct erow {
  int size;
  // char **chars;
  char *chars;
} erow;

struct editorConfig {
  int cx, cy;
  int screenrows;
  int screencols;
  struct termios orig_termios;
  int numrows;
  erow rows;
};
struct editorConfig E;

/*** terminal ***/

void die(const char *error) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  perror(error);
  exit(1);
}
void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
    die("tcgetattr");
  atexit(disableRawMode);
  struct termios raw = E.orig_termios;

  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

int editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }
  if (c == '\x1b') {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return '\x1b';
    if (seq[1] == 'O') {
      if (read(STDIN_FILENO, &seq[2], 1) != 1)
        return ('\x1b');
      switch (seq[2]) {
      case 'H':
        return HOME_KEY;
      case 'F':
        return END_KEY;
      }
    } else if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1)
          return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
          case '5':
            return PAGE_UP;
          case '6':
            return PAGE_DOWN;
          case '1':
          case '7':
          case 'H':
            return HOME_KEY;
          case '4':
          case '8':
          case 'F':
            return END_KEY;
          case '3':
            return DELETE_KEY;
          }
        }
      } else {
        switch (seq[1]) {
        case 'A':
          return ARROW_UP;
        case 'B':
          return ARROW_DOWN;
        case 'C':
          return ARROW_RIGHT;
        case 'D':
          return ARROW_LEFT;
        }
      }
    }
    return '\x1b';
  } else {
    return c;
  }
}

int getCurserPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    return (-1);

  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1)
      break;
    if (buf[i] == 'R')
      break;
    i++;
  }
  buf[++i] = '\0';
  sscanf(&buf[1], "[%d;%dR", rows, cols);

  return (0);
}

int getWindowSize(int *rows, int *cols) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
      return (-1);
    return getCurserPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return (0);
  }
}

/*** utils ***/

ssize_t fileLines(char *filename) {
  ssize_t fileLines = 0;
  char *line;
  char chunk[128];
  ssize_t len = sizeof(chunk);
  FILE *fp = fopen(filename, "r");
  if (!fp)
    die("fopen");

  line = (char *)malloc(len);
  if (!line)
    die("malloc");

  line[0] = '\0';

  while (fgets(chunk, sizeof(chunk), fp) != NULL) {
    ssize_t len_used = strlen(line);
    ssize_t chunk_used = strlen(chunk);
    if (len - len_used < chunk_used) {
      len *= 2;
      line = realloc(line, len);
      if (!line)
        die("realloc");
    }
    memcpy(line + len_used, chunk, len - len_used);
    len_used += chunk_used;
    if (line[len_used - 1] == '\n') {
      fileLines++;
      line[0] = '\0';
    }
  }
  free(line);
  fclose(fp);
  return (fileLines);
}

/*** file i/o ***/

void editorOpen(char *filename) {
  ssize_t filelines = fileLines(filename);
  char line[50];
  ssize_t linelen;
  linelen = (ssize_t)snprintf(line, sizeof(line), "This file has %zu lines",
                              filelines);
  E.rows.size = linelen;
  E.rows.chars = (char *)malloc(linelen);
  memcpy(E.rows.chars, line, linelen);
  E.numrows = 1;
  // FILE *fp = fopen(filename, "r");
  // if (fp == NULL)
  //   die("fopen");
  // char *line = NULL;
  // ssize_t linecap = 0, linelen;
  // linelen = getline(&line, &linecap, fp);
  // if (linelen != -1) {
  //   E.rows.chars = (char **)malloc(fileLines);
  //   while (linelen > 0 &&
  //          (line[linelen - 1] == '\r' || line[linelen - 1] == '\n'))
  //     --linelen;
  //
  //   E.rows.size = linelen;
  //   E.rows.chars = (char *)malloc(linelen + 1);
  //   memcpy(E.rows.chars, line, linelen);
  //   E.rows.chars[linelen] = '\0';
  //   E.numrows = 1;
  // } else {
  //   die("getline");
  // }
  // free(line);
  // fclose(fp);
}

/*** append buffer ***/
struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT                                                              \
  { NULL, 0 }

void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);
  if (new == NULL)
    return;

  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab) { free(ab->b); }

/*** input ***/

void editorMoveCursor(int key) {
  switch (key) {
  case ARROW_LEFT:
    if (E.cx != 0)
      E.cx--;
    break;
  case ARROW_RIGHT:
    if (E.cx != E.screencols - 1)
      E.cx++;
    break;
  case ARROW_UP:
    if (E.cy != 0)
      E.cy--;
    break;
  case ARROW_DOWN:
    if (E.cy != E.screenrows - 1)
      E.cy++;
    break;
  }
}

void editorProcessKeypress() {
  int c = editorReadKey();
  switch (c) {
  case CTRL_KEY('q'):
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
    break;
  case PAGE_UP:
  case PAGE_DOWN: {
    int times = E.screenrows;
    while (times--)
      editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
  } break;
  case HOME_KEY:
  case END_KEY: {
    int times = E.screencols;
    while (times--)
      editorMoveCursor(c == HOME_KEY ? ARROW_LEFT : ARROW_RIGHT);
  } break;
  case ARROW_UP:
  case ARROW_DOWN:
  case ARROW_LEFT:
  case ARROW_RIGHT:
    editorMoveCursor(c);
    break;
  }
}

/*** output ***/

void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screenrows; y++) {
    if (y >= E.numrows) {
      if (y == E.screenrows / 3) {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
                                  "Kilo editor -- version %s", KILO_VERSION);
        if (welcomelen > E.screencols)
          welcomelen = E.screencols;
        int padding = (E.screencols - welcomelen) / 2;
        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--)
          abAppend(ab, " ", 1);
        abAppend(ab, welcome, welcomelen);
      } else {
        abAppend(ab, "~", 1);
      }
    } else {
      int len = E.rows.size;
      if (len > E.screencols)
        len = E.screencols;
      abAppend(ab, E.rows.chars, len);
    }

    abAppend(ab, "\x1b[K", 3);
    if (y < E.screenrows - 1) {
      abAppend(ab, "\r\n", 2);
    }
  }
}

void editorRefreshScreen() {
  struct abuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
  abAppend(&ab, buf, strlen(buf));
  abAppend(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}
/*** init ***/

void initEditor() {
  E.cx = 0;
  E.cy = 0;
  E.numrows = 0;
  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");
}

int main(int argc, char *argv[]) {
  enableRawMode();
  initEditor();
  if (argc >= 2)
    editorOpen(argv[1]);

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }
  return (0);
}
