// @TODO Step 90
/** includes **/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ioctl.h>

/** defines **/

#define WYNAUT_VERSION "0.0.1"
#define WYNAUT_TAB_STOP 4

// ANDs a character with 00011111
#define CTRL_KEYS(k) ((k) & 0x1f)


enum editorKey{
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	DEL_KEY,
	HOME_KEY,
	END_KEY,
	PAGE_UP,
	PAGE_DOWN
};

/** data **/

// defines datatype erow to be a struct with char array and size
typedef struct erow {
	int size;
	int rsize;
	char* chars;
	char* render;

} erow;

struct editorConfig {
	int cx,cy; // cursor position
	int rx;
	int rowoff; // rowoffset
	int coloff;
	int screenrows;
	int screencols;
	int numrows;
	erow* row;
	struct termios orig_termios;
};

struct editorConfig E;

/** terminal **/
void die(const char *s){
	// Clears the screen and resets the cursor. See editorRefreshScreen for details
	write(STDOUT_FILENO,"\x1b[2J",4);
	write(STDOUT_FILENO,"\x1b[H", 3);
	perror(s);
	exit(1);
}

void disableRawMode(){
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1){
		die("tcsetattr");
	}
}

void enableRawMode(){
	// Gets the terminal attributes
	if (tcgetattr(STDIN_FILENO, &E.orig_termios) ==-1){
		die("tcgetattr");
	}
	atexit(disableRawMode);

	struct termios raw = E.orig_termios;
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO| ICANON| ISIG | IEXTEN);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;
	// removes ECHO - attr which displays user input and ICANON and ISIG which controls SIGINT signals sent by ctrlC and ctrl Z
	/*These attr flags are bit flags, '~' is the NOT operator that inverts the bits and '&=' ANDs the negated result with raw, which ensures that
	all the existing bits are preserved and the bits corresponding to the removed flag are overwritten to 0
	lflag refers to local flags */
	// Sets the attr back to terminal
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw)==-1){
		die("tcsetattr");
	}
}

// Waits for one key press, reads(low-level) it and returns it
// if esc sequence detected, would read further
int editorReadKey(){
	int nread;
	char c;
	while((nread = read(STDIN_FILENO,&c,1)) != 1){
		if (nread == -1 && errno != EAGAIN){
			die("read");
		}
	}

	if (c =='\x1b'){
		char seq[3];

		if (read(STDIN_FILENO, &seq[0],1) != 1) return'\x1b';
		if (read(STDIN_FILENO, &seq[1],1) != 1) return'\x1b';

		// Checks for arrow keys
		// Also checks for other escape sequences such as page up and down
		if (seq[0] == '['){
			if (seq[1] >= '0' && seq[1] <= '9'){
				if (read(STDIN_FILENO, &seq[2],1) != 1) return '\x1b';
				if (seq[2] == '~'){
					switch (seq[1]) {
						case '1': return HOME_KEY;
						case '3': return DEL_KEY;
						case '4': return END_KEY;
						case '5': return PAGE_UP;
						case '6': return PAGE_DOWN;
						case '7': return HOME_KEY;
						case '8': return END_KEY;
					}
				}
			}
			else{
				switch(seq[1]){
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'H': return HOME_KEY;
					case 'F': return END_KEY;
				}
			}
		}
		// Sometimes Home and End keys might return <esc>OH/F
		else if (seq[0] == 'O'){
			switch (seq[1]) {
				case 'H': return HOME_KEY;
				case 'F': return END_KEY;
			}
		}

		return '\x1b';
	}
	else{
		return c;
	}
}

//returns the position of the cursor
int getCursorPosition(int* rows, int* cols){
	char buf[32];
	unsigned int i=0;

	if(write(STDOUT_FILENO,"\x1b[6n",4) != 4){
		return -1;
	}
	// parses the formating
	while (i<sizeof(buf)-1){ 
		if(read(STDIN_FILENO,&buf[i],1)!=1)break;
		if (buf[i] == 'R')break;
		i++;
	}

	buf[i] = '\0';

	if (buf[0] != '\x1b' || buf[1] != '[') return 1;
	if (sscanf(&buf[2], "%d;%d",rows,cols) !=2)return -1;
	return 0;
}

// Gets the number of rows and cols
int getWindowsSize(int* rows, int* cols){
	struct winsize ws;
	if (ioctl(STDOUT_FILENO,TIOCGWINSZ, &ws)==-1 || ws.ws_col == 0){
		if (write(STDOUT_FILENO,"\x1b[999C\x1b[999B",12) != 12) return -1;
		return getCursorPosition(rows,cols);
	}
	else{
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}

/** row operations **/

int editorRowCxToRx(erow* row, int cx){
	int rx = 0;
	for (int j=0; j<cx;j++){
		if (row->chars[j] == '\t')
			rx += (WYNAUT_TAB_STOP-1) - (rx % WYNAUT_TAB_STOP);
		rx++;
	}
	return rx;
}

void editorUpdateRow(erow* row){
	int tabs = 0;
	for (int i =0; i<row->size;i++){
		if (row->chars[i] == '\t') tabs++;
	}

	free(row->render);
	row->render = malloc(row->size + tabs*(WYNAUT_TAB_STOP-1) + 1);

	int idx = 0;
	for (int j =0; j<row->size;j++){
		if(row->chars[j] == '\t'){
			row->render[idx++] = ' ';
			while (idx % WYNAUT_TAB_STOP != 0) row->render[idx++] = ' ';
		}
		else{
			row->render[idx++] = row->chars[j];
		}
	}
	row->render[idx] = '\0';
	row->rsize = idx;
}

void editorAppendRow(char* s, size_t len){
	E.row = realloc(E.row, sizeof(erow)*(E.numrows+1));

	int at = E.numrows;

	E.row[at].size = len;
	E.row[at].chars = malloc(len+1);
	memcpy(E.row[at].chars,s,len);
	E.row[at].chars[len] = '\0';

	E.row[at].rsize = 0;
	E.row[at].render = NULL;
	editorUpdateRow(&E.row[at]);

	E.numrows++;
}

/** file i/o **/

void editorOpen(char* filename) {
	FILE* fp = fopen(filename,"r");
	if (!fp) die("fopen");

	char* line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	while ((linelen = getline(&line, &linecap, fp)) != -1){
		while (linelen>0 && (line[linelen-1] == '\n' || line[linelen-1] == '\r'))
			linelen--;
		editorAppendRow(line, linelen);
	}
	free(line);
	fclose(fp);
}

/** append buffer **/

// creating a dynamic string type for write buffer
struct abuf{
	char* b;
	int len;
};

// constructor
#define ABUF_INIT {NULL, 0}

// appends string s of length len to buffer
void abAppend(struct abuf* ab, const char* s, int len){
	char* new = realloc(ab->b, ab->len + len); // Resizes and returns pointer to buffer

	if (new != NULL){
		memcpy(&new[ab->len],s,len); // Appends string to end of buffer
		
		// re-assigns buffer pointer to new
		ab->b = new;
		ab->len +=len;	
	}
}

// ~abuf | destructor
void abFree(struct abuf* ab){
	free(ab->b);
}

/** output **/

void editorScroll(){
	E.rx = 0;
	if (E.cy < E.numrows){
		E.rx = editorRowCxToRx(&E.row[E.cy],E.cx);
	}

	if (E.cy < E.rowoff){ // if cursor position is above screen, go up
		E.rowoff = E.cy;
	}
	if (E.cy >= E.rowoff + E.screenrows){
		E.rowoff = E.cy - E.screenrows +1;
	}
	if (E.rx < E.coloff){
		E.coloff = E.rx;
	}
	if (E.rx >= E.coloff + E.screencols){
		E.coloff = E.rx - E.screencols +1;
	}
}

// Draws a '~' on every line
void editorDrawRows(struct abuf* ab){
	for (int y=0; y<E.screenrows; y++){
		int filerow = y + E.rowoff;
		if (filerow >= E.numrows){
			// prints welcome message
			if (E.numrows == 0 && y == E.screenrows/3){
				char welcome[80];
				int welcomelen = snprintf(welcome,sizeof(welcome),"Wynaut editor -- version %s", WYNAUT_VERSION);
				// truncates message to fit
				if (welcomelen > E.screencols) welcomelen = E.screencols;
				// calculates padding to centre text
				int padding = (E.screencols - welcomelen)/2;
				if (padding){
					abAppend(ab,"~",1);
					padding--;
				}
				// centers the welcome message
				while(padding--)abAppend(ab," ",1);
				abAppend(ab,welcome,welcomelen);
			}
			else{
				abAppend(ab,"~",1);
			}
		} else {
			int len = E.row[filerow].rsize - E.coloff;
			if (len <0) len =0;
			if (len > E.screencols) len = E.screencols;
			abAppend(ab, &E.row[filerow].render[E.coloff], len);
		}
		// 4 -> writing 4 bytes to terminal
		// \x1b is the escape character
		// Escape sequences always start with escape character and [
		// K command erases part of current line
		// 0 is the default argument to K, clear entire line to right of cursor
		abAppend(ab,"\x1b[K",3);
		// Does not print new line on last line
		if(y < E.screenrows -1){
			abAppend(ab,"\r\n",2);
		}
	}
}

// Refreshes screen with new ouput every step
void editorRefreshScreen(){
	editorScroll();
	struct abuf ab = ABUF_INIT;
	// hides the cursor
	abAppend(&ab, "\x1b[?25l",6);

	// H command repositions cursor
	// It takes two arguments, but defaults to 1;1
	abAppend(&ab,"\x1b[H", 3);

	editorDrawRows(&ab);

	// Repositions cursor
	char buf[32];
	snprintf(buf,sizeof(buf),"\x1b[%d;%dH",E.cy-E.rowoff + 1,E.rx - E.coloff+1); // add 1 to conform to 1 based indexing of terminal
	abAppend(&ab, buf, strlen(buf));

	abAppend(&ab, "\x1b[?25h",6);	// shows the cursor

	write(STDOUT_FILENO, ab.b,ab.len);
	abFree(&ab);
}

/** input **/

// allows user to move using 'wasd' keys
void editorMoveCursor(int key){
	erow* row = (E.cy >=E.numrows)?NULL:&E.row[E.cy];

	switch (key) {
		case ARROW_LEFT:
			if (E.cx !=0){
				E.cx--;
			}
			else if (E.cy > 0){
				E.cy--;
				E.cx = E.row[E.cy].size;
			}
			break;
		case ARROW_RIGHT:
			if (row && E.cx < row->size){
				E.cx++;
			}
			else if (row && E.cx == row->size){
				E.cy++;
				E.cx=0;
			}
			break;
		case ARROW_UP:
			if (E.cy != 0)
			E.cy--;
			break;
		case ARROW_DOWN:
			if (E.cy < E.numrows-1)
			E.cy++;
			break;
	}

	row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
	int rowlen = row ? row->size:0;
	if (E.cx > rowlen){
		E.cx = rowlen;
	}
}

// waits for keypress and handles it
// Later -> handle special combinations
void editorProcessKeypress(){
	int c = editorReadKey();

	switch(c){
		case CTRL_KEYS('q'):
			// Clears the screen and resets the cursor. See editorRefreshScreen for details
			write(STDOUT_FILENO,"\x1b[2J",4);
			write(STDOUT_FILENO,"\x1b[H", 3);
			exit(0);
			break;
		case HOME_KEY:
			E.cx = 0;
			break;
		case END_KEY:
			E.cx = E.screencols-1;
			break;
		case PAGE_UP:
		case PAGE_DOWN: // We create a block of code as otherwise we cant initialize a new variable in a switch statement
			{
				int times = E.screenrows;
				while (times--){ // simulates up/down keys being pressed multiple times
					editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
				}
			}
			break;
		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_RIGHT:
		case ARROW_LEFT:
			editorMoveCursor(c);
			break;
		default:
		printf("%c", c);
		break;
	}
}

/** init **/

void initEditor(){
	E.cx = 0;
	E.cy = 0;
	E.rx = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.numrows = 0;
	E.row = NULL;

	if (getWindowsSize(&E.screenrows, &E.screencols) ==-1){
		die("getWindowsSize");
	}
}

int main(int argc, char* argv[]) {
	enableRawMode();
	initEditor();
	if (argc >= 2){
		editorOpen(argv[1]);
	}

	while (1){	//Empty while loop that keeps taking input till user enters 'q'
		editorRefreshScreen();
		editorProcessKeypress();
	}	
	return 0;
}