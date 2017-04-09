/** includes **/
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>

/** data **/
struct termios orig_termios;

/** terminal **/
void die(const char *s){
	perror(s);
	exit(1);
}

void disableRawMode(){
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1){
		die("tcsetattr");
	}
}

void enableRawMode() {
	// Gets the terminal attributes
	if (tcgetattr(STDIN_FILENO, &orig_termios) ==-1){
		die("tcgetattr");
	}
	atexit(disableRawMode);

	struct termios raw = orig_termios;
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

/** init **/
int main() {
	enableRawMode();
	while (1){	//Empty while loop that keeps taking input till user enters 'q'
		char c = '\0';
		if (read(STDIN_FILENO, &c,1)==-1 && errno!= EAGAIN){
			die("read");
		}
		if (iscntrl(c)){	// iscntrl checks for control characters such as 'ctrl' 'alt' etc
			printf("%d\r\n ", c);
		}
		else{
			printf("%d ('%c')\r\n",c,c);
		}
		if (c=='q')break;
	}	
	return 0;
}