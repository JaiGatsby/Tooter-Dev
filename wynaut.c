// @TODO Step 50
/** includes **/
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>

/** defines **/

#define WYNAUT_VERSION "0.0.1"

// ANDs a character with 00011111
#define CTRL_KEYS(k) ((k) & 0x1f)


enum editorKey{
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN
};

/** data **/
struct editorConfig {
	int cx,cy; // cursor position
	int screenrows;
	int screencols;
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

		// Checks for arrow keys and maps them to 'wasd'
		if (seq[0] == '['){
			switch(seq[1]){
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
// Draws a '~' on every line
void editorDrawRows(struct abuf* ab){
	for (int y=0; y<E.screenrows; y++){
		// prints welcome message
		if (y == E.screenrows/3){
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
	struct abuf ab = ABUF_INIT;
	// hides the cursor
	abAppend(&ab, "\x1b[?25l",6);

	// H command repositions cursor
	// It takes two arguments, but defaults to 1;1
	abAppend(&ab,"\x1b[H", 3);

	editorDrawRows(&ab);

	// Repositions cursor
	char buf[32];
	snprintf(buf,sizeof(buf),"\x1b[%d;%dH",E.cy+1,E.cx+1); // add 1 to conform to 1 based indexing of terminal
	abAppend(&ab, buf, strlen(buf));

	abAppend(&ab, "\x1b[?25h",6);	// shows the cursor

	write(STDOUT_FILENO, ab.b,ab.len);
	abFree(&ab);
}

/** input **/

// allows user to move using 'wasd' keys
void editorMoveCursor(int key){
	switch (key) {
		case ARROW_LEFT:
			if (E.cx !=0)
			E.cx--;
			break;
		case ARROW_RIGHT:
			if (E.cx != E.screencols-1)
			E.cx++;
			break;
		case ARROW_UP:
			if (E.cy != 0)
			E.cy--;
			break;
		case ARROW_DOWN:
			if (E.cy != E.screenrows-1)
			E.cy++;
			break;
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

int initEditor(){
	E.cx = 0;
	E.cy = 0;

	if (getWindowsSize(&E.screenrows, &E.screencols) ==-1){
		die("getWindowsSize");
	}
}

int main() {
	enableRawMode();
	initEditor();

	while (1){	//Empty while loop that keeps taking input till user enters 'q'
		editorRefreshScreen();
		editorProcessKeypress();
	}	
	return 0;
}
