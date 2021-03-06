// @TODO Step 175
// consider adding fuzzy search 
/** includes **/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ioctl.h>

/** defines **/


#define WYNAUT_VERSION "0.0.1"
#define WYNAUT_TAB_STOP 4
#define WYNAUT_QUIT_TIMES 3

// ANDs a character with 00011111	
// returns the ctrl + k combination
#define CTRL_KEYS(k) ((k) & 0x1f)


enum editorKey{
	BACKSPACE = 127,
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

enum editorHighlight {
	HL_NORMAL = 0,
	HL_COMMENT,
	HL_KEYWORD1,
	HL_KEYWORD2,
	HL_STRING,
	HL_NUMBER,
	HL_MATCH
};

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)

/** data **/

// For syntax highlighting purposes
struct editorSyntax {
	char* filetype;
	char** filematch;
	char** keywords;
	char* singleline_comment_start;
	int flags;
};

// defines datatype e(each)row to be a struct with char array and size
typedef struct erow {
	int size;
	int rsize;
	char* chars;
	char* render;
	unsigned char* hl;
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
	int dirty; //To check if the input file has been modified in anyway
	char* filename; // to be displayed in status bar
	char statusmsg[80];
	time_t statusmsg_time;
	struct editorSyntax* syntax;
	struct termios orig_termios;
};

struct editorConfig E;

/** filetypes **/

char* C_HL_extensions[] = {".c", ".h", ".cpp", NULL};
// We add a '|' at the end of the types so that we can color them a different color
char* C_HL_keywords[] = {
	"switch", "if", "while", "for", "break", "continue", "return", "else", "struct", "union",
	"typedef", "static", "enum", "class", "case", "int|", "long|", "float|", "char|", "unsigned|",
	"signed|", "void|","#define", "#include", NULL
};

// Initializing the highlight database, with 'C' rules
struct editorSyntax HLDB[] = {
	{
	 "c",
	 C_HL_extensions,
	C_HL_keywords,
	"//",
	 HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
	},
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/** prototypes **/

void editorSetStatusMessage(const char* fmt, ...);
void editorRefreshScreen();
char* editorPrompt(char* prompt, void (*callback)(char*, int));

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

/** syntax highlighting **/

// Checks chars against hardcoded list of separators
int is_separator(int c){
	// strchr returns null if it doesnt find any of these chars in c
	return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}


void editorUpdateSyntax(erow* row){
	row->hl = realloc(row->hl, row->rsize);
	//set all characters to HL_NORMAL by default
	memset(row->hl, HL_NORMAL, row->rsize);

	if (E.syntax == NULL) return;	

	char** keywords = E.syntax->keywords;

	char *scs = E.syntax->singleline_comment_start;
	int scs_len = scs ? strlen(scs) : 0;

	int prev_sep = 1;
	int in_string = 0;
	
	// now we loop over the digits and change the digits to HL_NUMBER
	int i = 0;
	while (i < row->rsize){
		char c = row->render[i];
		unsigned char prev_hl = (i > 0) ? row->hl[i-1] : HL_NORMAL;

		if (scs_len && !in_string) {
			if (!strncmp(&row->render[i], scs, scs_len)){ // checks if current char is part of the commentstart
				memset(&row->hl[i], HL_COMMENT, row->rsize - i); // sets entire line to comment color
				break;
			}
		}
		
		// if the highlight string flag is raised
		if (E.syntax->flags & HL_HIGHLIGHT_STRINGS){
			// and the character is in a string
			if (in_string) {
				row->hl[i] = HL_STRING;//color it
				// exempting escaped chars from making a difference to color
				if (c == '\\' && i + 1 < row->rsize){
					row->hl[i+1] = HL_STRING;
					i += 2;
					continue;
				}
				if ( c == in_string) in_string = 0;//if the char happens to be the matching quote, stop coloring
				i++;
				prev_sep = 1; // set it so digits are not exempted
				continue;
			}
			else {
				// if char is not in a str, but is an quote mark
				// color it like a str and set in string to it
				if (c=='"' || c == '\''){
					in_string = c;
					row->hl[i] = HL_STRING;
					i++;
					continue;
				}
			}
		}

		if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS){
			if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) || (c == '.' && prev_hl == HL_NUMBER)){
				row->hl[i] = HL_NUMBER;
				i++;
				prev_sep = 0;
				continue;
			}
		}

		if (prev_sep){
			int j;
			for (j=0; keywords[j]; j++){
				int klen = strlen(keywords[j]);
				int kw2 = keywords[j][klen-1] == '|';
				if (kw2) klen --;

				if (!strncmp(&row->render[i], keywords[j], klen) &&
						is_separator(row->render[i + klen])) {
					memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
					i += klen;
					break;
				}
			}

			if (keywords[j] != NULL) {
				prev_sep = 0;
				continue;
			}
		}
		prev_sep = is_separator(c);
		i++;
	}
}

int editorSyntaxToColor(int hl){
	switch(hl){
		case HL_COMMENT:
			return 36;
		case HL_KEYWORD1:
			return 33;
		case HL_KEYWORD2:
			return 32;
		case HL_STRING:
			return 35;
		case HL_NUMBER:
			return 31;
		case HL_MATCH:
			return 34;
		default:
			return 37;
	}
}

void editorSelectSyntaxHighlight(){
	E.syntax = NULL;
	if (E.filename == NULL) return;
	
	//returns the text after the '.'
	char* ext = strrchr(E.filename, '.');

	for (unsigned int j=0; j< HLDB_ENTRIES; j++){
		struct editorSyntax* s = &HLDB[j];
		unsigned int i = 0;
		while (s->filematch[i]) {
			int is_ext = (s->filematch[i][0] == '.');
			if ((is_ext && ext && !strcmp(ext, s->filematch[i])) ||
				(!is_ext && strstr(E.filename, s->filematch[i]))) {
			  E.syntax = s;

			  int filerow;
			  for (filerow = 0; filerow < E.numrows; filerow++) {
				editorUpdateSyntax(&E.row[filerow]);
			  }
			  return;
			}
			i++;
		}
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

int editorRowRxToCx(erow* row, int rx){
	int cur_rx = 0;
	int cx;
	for (cx = 0; cx < row->size; cx++){
		if (row->chars[cx] == '\t'){
			cur_rx += (WYNAUT_TAB_STOP - 1) - (cur_rx % WYNAUT_TAB_STOP);
		}
		cur_rx++;

		if (cur_rx > rx) return cx;
	}
	return cx;
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

	editorUpdateSyntax(row);
}

void editorInsertRow(int at,char* s, size_t len){
	if (at < 0 || at > E.numrows) return;

	E.row = realloc(E.row, sizeof(erow)*(E.numrows+1));
	memmove(&E.row[at + 1], &E.row[at], sizeof(erow)*(E.numrows-at));


	E.row[at].size = len;
	E.row[at].chars = malloc(len+1);
	memcpy(E.row[at].chars,s,len);
	E.row[at].chars[len] = '\0';

	E.row[at].rsize = 0;
	E.row[at].render = NULL;
	E.row[at].hl = NULL;
	editorUpdateRow(&E.row[at]);

	E.numrows++;
	E.dirty++;
}

// frees from memory a given row and the chars in it
void editorFreeRow(erow* row){
	free(row->render);
	free(row->chars);
	free(row->hl);
}

// handles deleting a char if it happens to be the start of the row
void editorDelRow(int at){
	if (at < 0 || at >= E.numrows) return;
	editorFreeRow(&E.row[at]);
	memmove(&E.row[at],&E.row[at+1],sizeof(erow)*(E.numrows-at-1));
	E.numrows--;
	E.dirty++;
}

// deletes characters given position
void editorRowDelChar(erow* row, int at){
	if(at<0 || at >= row->size) return;
	// move all characters that come after the character one step back
	memmove(&row->chars[at],&row->chars[at+1],row->size - at);
	row->size--;
	editorUpdateRow(row);
	E.dirty++;
}

// Deals with how to modify a row and adds a char in a specific place
void editorRowInsertChar(erow* row, int at, int c){
	// checks if input point is in bounds
	if (at < 0 || at > row->size) at = row->size;
	// we add 2 bytes as 1 for the character and one for the null character
	// but does there not already exist a null char?
	row->chars = realloc(row->chars,row->size + 2);
	// All the characters from 'at' onwards are moved one step ahead to
	// make space for the inserted character
	memmove(&row->chars[at+1], &row->chars[at], row->size - at +1);
	row->size++;
	row->chars[at] = c;
	editorUpdateRow(row);
	E.dirty++;
}

void editorRowAppendString(erow* row, char* s, size_t len){
	row->chars = realloc(row->chars,row->size +len+1);
	memcpy(&row->chars[row->size],s,len);
	row->size += len;
	row->chars[row->size] = '\0';
	editorUpdateRow(row);
	E.dirty++;
}

/** editor operations **/

// Deals with where the cursor is and adds a char
void editorInsertChar(int c){
	// checks if cursor is at end of line,and then adds a new row
	if(E.cy == E.numrows) {
		editorInsertRow(E.numrows, "",0);
	}
	editorRowInsertChar(&E.row[E.cy],E.cx,c);
	E.cx++; // move cursor forward
}

// mapped to return key
void editorInsertNewline() {
	if (E.cx == 0){
		editorInsertRow(E.cy, "", 0);
	}
	else{
		erow* row = &E.row[E.cy];
		editorInsertRow(E.cy +1, &row->chars[E.cx],row->size - E.cx);
		row = &E.row[E.cy]; //editorInsertRow calls realloc and might invalidate the pointer
		row->size = E.cx;
		row->chars[row->size] = '\0';
		editorUpdateRow(row);
	}
	E.cy++;
	E.cx = 0;
}
// Mapped to Backspace character
void editorDelChar(){
	if(E.cy == E.numrows) return;
	if(E.cx==0 && E.cy == 0)return;
	erow* row = &E.row[E.cy];
	if (E.cx >0){
		editorRowDelChar(row,E.cx-1);
		E.cx--;
	}
	else{ // moves everything to the end of the last line and deletes current row
		E.cx = E.row[E.cy-1].size;
		editorRowAppendString(&E.row[E.cy-1],row->chars,row->size);
		editorDelRow(E.cy);
		E.cy--;
	}
}

/** file i/o **/

// returns the entire file as a char*
char* editorRowsToString(int* buflen){
	int totlen = 0;
	int j;
	for (j =0; j < E.numrows; j++){
		totlen += E.row[j].size +1; // +1 for \n
	}
	*buflen = totlen; // num of chars in file

	char* buf = malloc(totlen);
	char* p = buf;
	for (j = 0; j<E.numrows; j++){
		memcpy(p, E.row[j].chars, E.row[j].size);//copies each row to string
		p += E.row[j].size;
		*p = '\n';
		p++;
	}
	return buf; // expect caller to free memory
}

// opens a file and loads it into editorConfig
void editorOpen(char* filename) {
	free(E.filename);
	E.filename = strdup(filename); // copies given string and dynamically allocates memory

	editorSelectSyntaxHighlight();

	FILE* fp = fopen(filename,"r");
	if (!fp) die("fopen");

	char* line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	while ((linelen = getline(&line, &linecap, fp)) != -1){
		while (linelen>0 && (line[linelen-1] == '\n' || line[linelen-1] == '\r'))
			linelen--;
		editorInsertRow(E.numrows, line, linelen);
	}
	free(line);
	fclose(fp);
	E.dirty = 0;
}

// saves to file
void editorSave(){
	if(E.filename == NULL){
        E.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
        if (E.filename == NULL){
            editorSetStatusMessage("Save aborted");
            return;
        }
		editorSelectSyntaxHighlight();
    }

	int len;
	char* buf = editorRowsToString(&len); //gets the entire file

	// open creates a new file if it doesnt exist O_CREAT and 
	//opens it for reading and writing O_RDWR
	// 0644 is standard permissions
	int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
	if (fd != -1){
		if (ftruncate(fd,len)!= -1){// sets file size to specific length
		// we manually truncate so that in case write fails, we still have some data
			if (write(fd, buf, len) == len){
				close(fd);
				free(buf);
				E.dirty=0;
				editorSetStatusMessage("%d bytes written to disk", len);
				return;
			}
		}
		close(fd);
	}
	free(buf);
	editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/** find **/

void editorFindCallback(char* query, int key){
	static int last_match = -1;
	static int direction = 1;

	static int saved_hl_line;
	static char* saved_hl = NULL;
	
	// Saves the line which is highlighted and its original highlight
	if (saved_hl) {
		memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize);
		free(saved_hl);
		saved_hl = NULL;
	}

	if(key == '\r' || key == '\x1b'){
		last_match = -1;
		direction = 1;
		return;
	} else if (key == ARROW_RIGHT || key == ARROW_DOWN){
		direction = 1;
	} else if (key == ARROW_LEFT || key == ARROW_UP){
		direction = -1;
	} else {
		last_match = -1;
		direction = 1;
	}

	if (last_match == -1) direction =1;
	int current = last_match;
	int i;
	for (i = 0; i < E.numrows; i++){
		current += direction;
		if (current == -1) current = E.numrows - 1;
		else if (current == E.numrows) current = 0;

		erow* row = &E.row[current];
		char* match = strstr(row->render,query);
		if (match){
			last_match = current;
			E.cy = current;
			E.cx = editorRowRxToCx(row, match - row->render);
			E.rowoff = E.numrows;
			
			// restores original highlight
			saved_hl_line = current;
			saved_hl = malloc(row->rsize);
			memcpy(saved_hl, row->hl, row->rsize);

			memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
			break;
		}
	}
}

// Implements the search function, asks for a query and returns first instance of it in file
void editorFind(){
	// Saving current cursor position incase user cancels search
	int saved_cx = E.cx;
	int saved_cy = E.cy;
	int saved_coloff = E.coloff;
	int saved_rowoff = E.rowoff;

	char* query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)", editorFindCallback);
	
	if (query){
		free(query);
	}
	else{
		E.cx = saved_cx;
		E.cy = saved_cy;
		E.coloff = saved_coloff;
		E.rowoff = saved_rowoff;
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

// Prints each line reading from a file
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
			char* c = &E.row[filerow].render[E.coloff];
			unsigned char* hl = &E.row[filerow].hl[E.coloff];
			int current_color = -1;
			int j;
			// Prints char by char, if its a digit, then changes color and adds it
			for (j=0; j<len; j++){
				if (hl[j] == HL_NORMAL){
					if (current_color != -1){
						abAppend(ab, "\x1b[39;1m",7); // code reset colors
						current_color = -1;
					}
					abAppend(ab, &c[j], 1);
				} else {
					int color = editorSyntaxToColor(hl[j]);
					if (color != current_color){
						current_color = color;
						char buf[16];
						int clen = snprintf(buf, sizeof(buf), "\x1b[%d;1m",color);
						abAppend(ab, buf, clen);
					}
					abAppend(ab, &c[j], 1);
				}
			}
			abAppend(ab, "\x1b[39;1m",7);
		}
		// 4 -> writing 4 bytes to terminal
		// \x1b is the escape character
		// Escape sequences always start with escape character and [
		// K command erases part of current line
		// 0 is the default argument to K, clear entire line to right of cursor
		abAppend(ab,"\x1b[K",3);
		// Does not print new line on last line
		abAppend(ab,"\r\n",2);
	}
}

// Creates a status bar at the end of the page
void editorDrawStatusBar(struct abuf* ab){
	abAppend(ab, "\x1b[7m",4); // Inverts colors
	char status[80], rstatus[80];
	int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
		 E.filename ? E.filename : "[No name]", E.numrows,E.dirty?"(modified)":""); // Copies filename to status and returns size to len, if doesnt exist puts "[No Name]"
	int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d",
		E.syntax ? E.syntax->filetype : "no ft", E.cy+1,E.numrows); // Outputs filetype and line num
	if (len > E.screencols) len = E.screencols; // Truncates the size to screenwidth
	abAppend(ab,status,len);
	while (len < E.screencols) {
		if (E.screencols - len == rlen){ // when enough spaces have been printed that rstatus can be printed, print it and break
			abAppend(ab,rstatus,rlen);
			break;
		}
		else{
			abAppend(ab, " ", 1);	// Prints blank lines till the end so that entire bar has white background
			len++;
		}
	}
	abAppend(ab, "\x1b[m",3); // Reverts colors back to normal
	abAppend(ab, "\r\n", 2); // Making space for further status messages
}

// Displays a message at the bottom of the screen
void editorDrawMessageBar(struct abuf* ab){
	abAppend(ab, "\x1b[K",3); // clear the message bar

	// ensure that message fits the screen
	int msglen = strlen(E.statusmsg);
	if (msglen > E.screencols - 20) msglen = E.screencols;

	//make sure message is less than 5 seconds old
	if (msglen && (time(NULL) - E.statusmsg_time < 5)){
		abAppend(ab, E.statusmsg, msglen);
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
	editorDrawStatusBar(&ab);
	editorDrawMessageBar(&ab);

	// Repositions cursor
	char buf[32];
	snprintf(buf,sizeof(buf),"\x1b[%d;%dH",E.cy-E.rowoff + 1,E.rx - E.coloff+1); // add 1 to conform to 1 based indexing of terminal
	abAppend(&ab, buf, strlen(buf));

	abAppend(&ab, "\x1b[?25h",6);	// shows the cursor

	write(STDOUT_FILENO, ab.b,ab.len);
	abFree(&ab);
}

/** Sets a message to be set in status bar 
 * '...' means this function can take any number of arguments
 * These are stored in a va_list and you can cycle through them by calling
 * va_start and va_end
 * vsnprintf creates printf-style function	
 */
void editorSetStatusMessage(const char* fmt, ...){
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
	va_end(ap);
	E.statusmsg_time = time(NULL);
}

/** input **/

char* editorPrompt(char* prompt, void (*callback)(char*, int)){
	size_t bufsize = 128;
	char* buf = malloc(bufsize);

	size_t buflen = 0;
	buf[0] = '\0';

    // Inf loop, keeps asking user to enter a char
    // Whenever it detects a key press, if it isnt a special key
    // it updates status display and waits for next key
    // if it is enter, it returns the string entered
	while (1){
		editorSetStatusMessage(prompt,buf);
		editorRefreshScreen();

		int c = editorReadKey();
        // backspacing text
        if (c == DEL_KEY || c == CTRL_KEYS('h') || c ==  BACKSPACE){
            if (buflen != 0) buf[--buflen] = '\0';
        }
        // press ESC to exit
        else if (c=='\x1b'){
            editorSetStatusMessage("");
			if (callback) callback(buf, c);
            free(buf);
            return NULL;
        }
		else if (c=='\r'){
			if(buflen !=0){
				editorSetStatusMessage("");
				if (callback) callback(buf, c);
				return buf; 
			}
		}
		else if (!iscntrl(c) && c <128){
            // doubles memory allocated to str in case its not enough
			if (buflen == bufsize-1){
				bufsize *= 2;
				buf = realloc(buf, bufsize);
			}
			buf[buflen++] = c;
			buf[buflen] = '\0';
		}

		if (callback) callback(buf, c);
	}
}

// allows user to move using arrow keys
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
	static int quit_times = WYNAUT_QUIT_TIMES;

	int c = editorReadKey();

	switch(c){
		case '\r':
			editorInsertNewline();
			break;

		case CTRL_KEYS('q'):
			if(E.dirty && quit_times >0){
				editorSetStatusMessage("WARNING!!! File has unsaved changes.Press Ctrl-Q %d more times to quit.",quit_times);
				quit_times--;
				return;
			}
			// Clears the screen and resets the cursor. See editorRefreshScreen for details
			write(STDOUT_FILENO,"\x1b[2J",4);
			write(STDOUT_FILENO,"\x1b[H", 3);
			exit(0);
			break;
			
		case CTRL_KEYS('s'):
			editorSave();
			break;

		case HOME_KEY:
			E.cx = 0;
			break;

		case END_KEY:
			if (E.cy < E.numrows)
				E.cx = E.row[E.cy].size;
			break;

		case CTRL_KEYS('f'):
			editorFind();
			break;

		case BACKSPACE:
		case CTRL_KEYS('h'): //Ctrl+H sends same code as what backspace used to
		case DEL_KEY:
			if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
			editorDelChar();
			break;

		case PAGE_UP:
		case PAGE_DOWN: // We create a block of code as otherwise we cant initialize a new variable in a switch statement
			{
				if (c == PAGE_UP){
					E.cy = E.rowoff;
				}
				else if (c == PAGE_DOWN){
					E.cy = E.rowoff + E.screenrows - 1; // adds screen number of rows to rowoff
					if (E.cy > E.numrows) E.cy = E.numrows;
				}
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

		case CTRL_KEYS('l'):
		case '\x1b': // <esc> Not doing anything as we are not handling
			break;

		default:
		// printf("%c", c);
		editorInsertChar(c);
		break;
	}

	quit_times = WYNAUT_QUIT_TIMES;
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
	E.dirty = 0;
	E.filename = NULL;
	E.statusmsg[0] = '\0';
	E.statusmsg_time = 0;
	E.syntax = NULL;

	if (getWindowsSize(&E.screenrows, &E.screencols) ==-1){
		die("getWindowsSize");
	}

	// Makes way for status bars at the bottom
	E.screenrows -= 2;
}

int main(int argc, char* argv[]) {
	enableRawMode();
	initEditor();
	if (argc >= 2){
		editorOpen(argv[1]);
	}

	editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

	while (1){	//Empty while loop that keeps taking input till user enters 'q'
		editorRefreshScreen();
		editorProcessKeypress();
	}	
	return 0;
}
