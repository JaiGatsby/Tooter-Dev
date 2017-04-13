/** includes **/
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
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

int getCursorPosition(int* rows, int* cols){
	if(write(STDOUT_FILENO,"\x1b[6n",4) != 4){
		return -1;
	}
	printf("\r\n");
	char c;
	while (read(STDIN_FILENO,&c,1)==1){
		if(iscntrl(c)){
			printf("%d\r\n",c);
		} else {
			printf("%d ('%c')\r\n",c,c);
		}
	}
	editorReadKey();

	return -1;
}

int getWindowsSize(int* rows, int* cols){
	struct winsize ws;
	if (1 || ioctl(STDOUT_FILENO,TIOCGWINSZ, &ws)==-1 || ws.ws_col == 0){
		if (write(STDOUT_FILENO,"\x1b[999C\x1b[999B",12) != 12) return -1;
		return getCursorPosition(rows,cols);
	}
	else{
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}

/** output **/
void editorDrawRows(){
	for (int y=0; y<E.screenrows; y++){
		write(STDOUT_FILENO,"~\r\n",3);
	}
}

// Refreshes screen with new ouput every step
void editorRefreshScreen(){
	// 4 -> writing 4 bytes to terminal
	// \x1b is the escape character
	// Escape sequences always start with escape character and [
	// J command clears screen
	// 2 is an argument to J, clear entire screen
	write(STDOUT_FILENO,"\x1b[2J",4);
	// H command repositions cursor
	// It takes two arguments, but defaults to 1;1
	write(STDOUT_FILENO,"\x1b[H", 3);

	editorDrawRows();

	write(STDOUT_FILENO,"\x1b[H",3);
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