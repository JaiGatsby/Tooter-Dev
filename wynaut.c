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

// ANDs a character with 00011111
#define CTRL_KEYS(k) ((k) & 0x1f)

/** data **/
struct editorConfig {
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
// Later functionality to be added -> handle escape sequences
char editorReadKey(){
	int nread;
	char c;
	while((nread = read(STDIN_FILENO,&c,1)) != 1){
		if (nread == -1 && errno != EAGAIN){
			die("read");
		}
	}
	return c;
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
		abAppend(ab,"~",1);

		// Does not print new line on last line
		if(y < E.screenrows -1){
			abAppend(ab,"\r\n",2);
		}
	}
}

// Refreshes screen with new ouput every step
void editorRefreshScreen(){
	struct abuf ab = ABUF_INIT;
	// 4 -> writing 4 bytes to terminal
	// \x1b is the escape character
	// Escape sequences always start with escape character and [
	// J command clears screen
	// 2 is an argument to J, clear entire screen
	abAppend(&ab,"\x1b[2J",4);
	// H command repositions cursor
	// It takes two arguments, but defaults to 1;1
	abAppend(&ab,"\x1b[H", 3);

	editorDrawRows(&ab);

	abAppend(&ab,"\x1b[H",3);

	write(STDOUT_FILENO, ab.b,ab.len);
	abFree(&ab);
}

/** input **/
// waits for keypress and handles it
// Later -> handle special combinations
void editorProcessKeypress(){
	char c = editorReadKey();

	switch(c){
		case CTRL_KEYS('q'):
			// Clears the screen and resets the cursor. See editorRefreshScreen for details
			write(STDOUT_FILENO,"\x1b[2J",4);
			write(STDOUT_FILENO,"\x1b[H", 3);
			exit(0);
			break;
		default:
		printf("%c", c);
	}
}

/** init **/

int initEditor(){
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
