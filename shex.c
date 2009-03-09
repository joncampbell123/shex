/* simple hex editor
 * (C) 2004 Jonathan Campbell */

#include <sys/stat.h>
#include <sys/types.h>
#include <linux/fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>

#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#warning O_LARGEFILE not present, you will not be able to edit files > 2GB
#endif

#ifndef _FILE_OFFSET_BITS
#warning _FILE_OFFSET_BITS not present, 64-bit file offset extensions unavailable. Files > 2GB will not edit correctly.
#endif

/* file variables */
int			file_fd = -1;
int			file_mode = 0;
unsigned long long	file_size = 0;
unsigned long long	file_cursor = 0;

/* view variables */
unsigned long long	view_offset = 0;
unsigned long long	view_ofs_x = 0;
unsigned long long	view_ofs_y = 0;
unsigned long long	view_columns = 16;
unsigned long long	view_scrcols = 16;
unsigned long long	view_rows = 0;
unsigned long long	view_colofs = 0;
int			view_tab = 1;
int			view_with_hex = 1;
int			view_with_asc = 1;
int			viewcon_x = 0;
int			viewcon_y = 0;
int			view_modifymode = 0;

/* console vars */
int			con_width;
int			con_height;

/* view update vars */
char			viewup_all = 0;
char			viewup_scroll = 0;
char			viewup_cursor = 0;
enum {				VUPS_UP=1,
				VUPS_DOWN=2 };

/* converts (file cursor + view offset) -> coordinates */
void ViewOfsToCoord()
{
	unsigned long long o;
	int x,y;

	if (view_with_hex && view_with_asc)	view_scrcols = (con_width - 19) / 4;
	else if (view_with_hex)			view_scrcols = (con_width - 19) / 3;
	else if (view_with_asc)			view_scrcols = (con_width - 19);
	else					view_scrcols = 1;

	if (file_cursor < view_offset) {
		o = view_offset - file_cursor;
		x = o % view_columns;
		y = o / view_columns;
		if (x != 0) y++;
		if (view_offset < view_columns) {
			view_offset = 0;
			viewup_all = 1;
		}
		else if (y == 1) {
			view_offset -= view_columns;
			viewup_scroll = VUPS_DOWN;
		}
		else {
			view_offset -= y * view_columns;
			viewup_all = 1;
		}
	}
	else {
		o = file_cursor - view_offset;
		x = o % view_columns;
		y = o / view_columns;
		if (y > view_rows) {
			view_offset += (y - (view_rows - 1)) * view_columns;
			viewup_all = 1;
		}
		else if (y == view_rows) {
			viewup_scroll = VUPS_UP;
			view_offset += view_columns;
		}
	}

	/* from this point on it should be that view_offset >= file_cursor */
	o = file_cursor - view_offset;
	x = o % view_columns;
	y = o / view_columns;

	if (x < view_colofs) {
		view_colofs = x;
		viewup_all = 1;
	}
	else if ((x-view_colofs) >= view_scrcols) {
		view_colofs = x - (view_scrcols - 1);
		viewup_all = 1;
	}

	/* update the cursor if it moved */
	if (x != view_ofs_x || y != view_ofs_y) {
		view_ofs_x = x;
		view_ofs_y = y;
		viewup_cursor = 1;
	}
}

/* file abstraction */
void FaClose()
{
	if (file_fd >= 0) close(file_fd);
	file_fd = -1;
	file_size = 0;
	file_cursor = 0;
	view_offset = 0;
}

int FaOpen(char *path,int mode)
{
	FaClose();

	file_mode = mode;
	file_fd = open(path,mode | O_LARGEFILE);
	if (file_fd < 0) return 0;
	file_size = lseek(file_fd,0,SEEK_END);
	if (file_size == ((unsigned long long)(-1))) {
		fprintf(stderr,"FaOpen(): descriptor can't seek!\n");
		FaClose();
		return 0;
	}
	
	file_cursor = 0;
	return 1;
}

unsigned long long FaSeek(unsigned long long ofs)
{
	if (file_fd < 0) return 0;
	return lseek(file_fd,ofs,SEEK_SET);
}

unsigned long long FaTell()
{
	if (file_fd < 0) return 0;
	return lseek(file_fd,0,SEEK_CUR);
}

/* console setup code */
int TermSetup()
{
	struct termios t;
	const char *nowrap = "\x1B[?7l";

	/* get STDIN to send us all input UNBUFFERED */
	if (tcgetattr(0,&t) < 0) return 0;
	t.c_lflag &= ~(ICANON | ECHO | ECHOE);
	if (tcsetattr(0,TCSAFLUSH,&t) < 0) return 0;

	/* disable wrap-around */
	write(1,nowrap,strlen(nowrap));

	return 1;
}

int TermReset()
{
	struct termios t;

	if (tcgetattr(0,&t) < 0) return 0;
	t.c_lflag |= ICANON | ECHO | ECHOE;
	if (tcsetattr(0,TCSAFLUSH,&t) < 0) return 0;
	return 1;
}

int TermEcho(int x)
{
	struct termios t;

	if (tcgetattr(0,&t) < 0) return 0;
	if (x)	t.c_lflag |= ECHO | ECHOE;
	else	t.c_lflag &= ~(ECHO | ECHOE);
	if (tcsetattr(0,TCSADRAIN,&t) < 0) return 0;
	return 1;
}

static char TermBuf[32];
char *TermRead()
{
	char c;
	int i;

	/* read char */
	i=0;
	TermBuf[0]=0;
	if (read(0,TermBuf,1) < 1) return TermBuf;
	TermBuf[++i] = 0;
	
	/* escape sequence? */
	if (TermBuf[0] == 27) {
		read(0,TermBuf+1,1); i++;
		/* ESC ESC means ESC */
		if (TermBuf[1] == 27) {
			TermBuf[2] = 0;
			return TermBuf;
		}
		/* ESC[ means a VT100 scan code */
		else if (TermBuf[1] == '[') {		
			do {
				read(0,&c,1);
				TermBuf[i++] = c;
			} while (i < 31 && (isdigit(c) || c == ';'));

			TermBuf[i]=0;
			return TermBuf;
		}
		/* ESC whatever? */
		else {
			TermBuf[2]=0;
			return TermBuf;
		}
	}

	TermBuf[1]=0;
	return TermBuf;
}

int TermPosCurs(int y,int x)
{
	char buf[16];

	if (x < 1) x=1;
	if (y < 1) y=1;
	if (x > con_width) x=con_width;
	if (y > con_height) y=con_height;

	sprintf(buf,"\x1b[%d;%df",y,x);
	write(1,buf,strlen(buf));
	return 1;
}

int TermSize()
{
	const char *rq = "\x1b[6n";		/* "ESC [ 6n" cursor position request */
	const char *rqcm = "\x1b[255;255f";	/* tell it to put the cursor down to 255,255 or as far as it goes */
	char *r,*r2;
	char buf[32];
	int ox,oy;

	/* print notice */
	fprintf(stderr,"querying terminal..."); fflush(stderr);

	/* current cursor position? */
	write(1,rq,strlen(rq));
	do { r=TermRead(); } while (r[0] != 27);
	oy=atoi(r+2); r2=strstr(r,";");
	ox=r2 ? atoi(r2+1) : 1;

	/* how far can we go? */
	write(1,rqcm,strlen(rqcm));
	write(1,rq,strlen(rq));
	do { r=TermRead(); } while (r[0] != 27);
	con_height = atoi(r+2); r2=strstr(r,";");
	con_width = r2 ? atoi(r2+1) : 1;

	/* restore cursor */
	sprintf(buf,"\x1b[%d;%dH",oy,ox);
	write(1,buf,strlen(buf));

	/* sanity check */
	if (con_width < 16) con_width = 16;
	if (con_height < 4) con_height = 4;

	/* print it to the console */
	fflush(stderr);
	fprintf(stderr,"\x0D");
	fflush(stderr);
	fprintf(stderr,"console size: %d rows x %d cols\n",con_height,con_width);
	view_rows = con_height - 1;

	return 1;
}

void TermSizeSet(int w,int h)
{
	con_width = w;
	con_height = h;
	view_rows = con_height - 1;
}

static char			PrtTmp[256];
static unsigned char		RowTmp[256];
static unsigned long long	LastRowTmp;
static int			LastRowTmpI=0;

void DrawRowCacheFlush()
{
	LastRowTmp=0;
	LastRowTmpI=0;
}

void DrawRow(int y,unsigned long long o)
{
	int x,w;
	unsigned char c;

	if (y == view_ofs_y)	printf("\x1B[0;1;37m");
	else			printf("\x1B[0;36m");

	w = view_scrcols;
	printf("%016LX",o);
	if (view_colofs != 0)	printf("<");
	else			printf(" ");

	if (view_with_hex) {
		FaSeek(o+view_colofs);
		if (o != LastRowTmp || !LastRowTmpI) {
			memset(RowTmp,0,w);
			read(file_fd,RowTmp,w);
			LastRowTmp = o;
			LastRowTmpI = 1;
		}
		
		for (x=0;x < w && (x+view_colofs) < view_columns && (o+x+view_colofs) < file_size;x++)
			printf("%02X ",RowTmp[x]);

		for (;x < w;x++)
			printf("   ");

		if ((w+view_colofs) < view_columns)	printf(">");
		else					printf(" ");
	}

	if (view_with_asc) {
		FaSeek(o+view_colofs);
		fflush(stdout);
		if (o != LastRowTmp || !LastRowTmpI) {
			memset(RowTmp,0,w);
			read(file_fd,RowTmp,w);
			LastRowTmp = o;
			LastRowTmpI = 1;
		}
		
		for (x=0;x < w && (x+view_colofs) < view_columns && (o+x+view_colofs) < file_size;x++) {
			c=RowTmp[x];
			if (c < 32 || c >= 127) c = '.';
			printf("%c",c);
		}

		c=' '; for (;x < w;x++) printf(" ");
		if ((w+view_colofs) < view_columns)	printf(">");
		else					printf(" ");
	}

	printf("\x1B[K");
	fflush(stdout);
}

static int VRlastrow = -1;
void ViewRefresh()
{
	int y;
	int w;

	w = view_scrcols;

	if (viewup_all) {
		unsigned long long of;
//		const char *rv = "\x1B[2J";
		
		of = view_offset;
		viewup_cursor = 1;
//		write(1,rv,strlen(rv));

		for (y=0;y < view_rows;y++) {
			TermPosCurs(y+1,1);
			DrawRow(y,of);
			of += view_columns;
		}

		viewup_all=0;
		viewup_scroll=0;
		VRlastrow = view_ofs_y;
	}

	if (viewup_scroll == VUPS_DOWN) {
		unsigned long long of;
		const char *scrd = "\x1B[L";	/* insert one line, make the rest scroll down */
		char buf[18];

		if (VRlastrow >= 0) {
			TermPosCurs(VRlastrow+1,1);
			of = view_offset + ((VRlastrow+1) * view_columns);
			DrawRow(-1,of);
		}

		sprintf(buf,"\x1B[1;%dr",view_rows);
		write(1,buf,strlen(buf));
		TermPosCurs(1,1);
		write(1,scrd,strlen(scrd));
		TermPosCurs(1,1);
		of = view_offset;
		DrawRow(0,of);
		sprintf(buf,"\x1B[1;%dr",con_height);
		write(1,buf,strlen(buf));

		viewup_cursor = 1;
		viewup_scroll = 0;
		viewup_all = 0;
		VRlastrow = view_ofs_y;
	}

	if (viewup_scroll == VUPS_UP) {
		unsigned long long of;
		char buf[18];

		if (VRlastrow >= 0) {
			TermPosCurs(VRlastrow+1,1);
			of = view_offset + ((VRlastrow-1) * view_columns);
			DrawRow(-1,of);
		}

		sprintf(buf,"\x1B[1;%dr",view_rows);
		write(1,buf,strlen(buf));
		TermPosCurs(view_rows,1);
		write(1,"\r\n",2);
		sprintf(buf,"\x1B[1;%dr",con_height);
		write(1,buf,strlen(buf));
		TermPosCurs(view_rows,1);
		of = view_offset + ((view_rows-1)*view_columns);
		DrawRow(view_rows-1,of);

		viewup_cursor = 1;
		viewup_scroll = 0;
		viewup_all = 0;
		VRlastrow = view_ofs_y;
	}

	if (viewup_cursor) {
		int x,y;

		switch (view_tab) {
			case 0:		/* offset column */
				y=view_ofs_y;
				x=0;
				break;

			case 1:		/* hex dump column */
				y=view_ofs_y;
				x=((view_ofs_x-view_colofs)*3)+17;
				break;

			case 2:		/* ASCII dump column */
				y=view_ofs_y;
				if (view_with_hex)	x=(view_ofs_x-view_colofs)+(w*3)+18;
				else			x=(view_ofs_x-view_colofs)+17;
				break;

			default:
				x=y=0;
				break;
		}

		viewcon_x = x;
		viewcon_y = y;
		viewup_cursor = 0;

		if (VRlastrow != viewcon_y) {
			unsigned long long of;

			if (VRlastrow >= 0) {
				TermPosCurs(VRlastrow+1,1);
				of = view_offset + (VRlastrow * view_columns);
				DrawRow(VRlastrow,of);
			}

			TermPosCurs(viewcon_y+1,1);
			of = view_offset + (viewcon_y * view_columns);
			DrawRow(viewcon_y,of);
			VRlastrow = viewcon_y;
		}
	}
}

void ReadInLine(char *buf,int len)
{
	int i,so;
	char *r;
	char act;
	const char *erase = "\x1B[D \x1B[D";

	TermEcho(0);

	i=0;
	so=0;
	act=1;
	while (act) {
		r=TermRead();
		if (r[0] >= 32 && r[0] < 127) {	/* non-escape code */
			if (i < len && i < (con_width-1)) {
				buf[i++] = r[0];
				write(1,r,1);
			}
		}
		else if (r[0] == 8 || r[0] == 127) {	/* backspace? */
			if (i > 0) {
				buf[--i] = 0;
				write(1,erase,strlen(erase));
			}
		}
		else if (r[0] == 13 || r[0] == 10) {	/* enter? */
			act=0;
			buf[i]=0;
		}
		else if (!strcmp(r,"\x1B\x1B")) {
			act=0;
			i=0;
			buf[i]=0;
		}
	}
}

/* main */
int main(int argc,char **argv)
{
	int mainloop;
	int act;
	int i;
	char stt[68];
	char *r;
	char *fn;
	int fnmod;
	
	if (!isatty(0) || !isatty(1)) {
		fprintf(stderr,"%s: STDIN/STDOUT must not be redirected!\n",argv[0]);
		return 1;
	}

	if (!TermSetup()) {
		fprintf(stderr,"%s: Unable to reconfigure terminal\n",argv[0]);
		return 0;
	}

	if (!TermSize()) {
		fprintf(stderr,"%s: cannot determine terminal size\n",argv[0]);
		TermSizeSet(80,25);
	}

	fn=NULL;
	fnmod=O_RDONLY;
	for (i=1;i < argc;i++) {
		if (argv[i][0] == '-') {
			if (!strcmp(argv[i]+1,"ro")) {
				fnmod=O_RDONLY;
			}
			else if (!strcmp(argv[i]+1,"rw")) {
				fnmod=O_RDWR;
			}
			/* -h or --help works */
			else if (!strcmp(argv[i]+1,"h") || !strcmp(argv[i]+1,"-help")) {
				TermReset();
				printf("%s [options] [file]\n",argv[0]);
				printf("Simple Hex editor (C) 2004 Jonathan Campbell\n");
				printf("where options can be:\n");
				printf("  -ro    open read-only (default)\n");
				printf("  -rw    open in read-write mode\n");
				printf("  -h     help\n");
				exit(0);
			}
			else {
				fprintf(stderr,"%s: unknown option %s\n",argv[0],argv[i]);
			}
		}
		else if (!fn) {
			fn=argv[i];
		}
		else {
			fprintf(stderr,"%s: ignoring param %s\n",argv[0],argv[i]);
		}
	}

	if (fn) {
		if (!FaOpen(fn,fnmod)) {
			fprintf(stderr,"%s: unable to open file %s\n",argv[0],fn);
			do { r=TermRead(); } while (r[0] != 10);
		}
	}

	viewup_all=1;
	mainloop=1;
	DrawRowCacheFlush();
	while (mainloop) {
		/* update screen */
		ViewOfsToCoord();
		ViewRefresh();

		/* status */
		sprintf(stt,"\x1B[0;7m" "%016LX ",file_cursor);
		if (view_tab == 0)		strcat(stt,"ofs ");
		else if (view_tab == 1)		strcat(stt,"hex ");
		else if (view_tab == 2)		strcat(stt,"asc ");
		if (file_mode & O_RDWR)		strcat(stt,"[rw] ");
		else				strcat(stt,"[ro] ");
		if (view_modifymode)		strcat(stt," [EDIT]");
		else				strcat(stt,"       ");
		strcat(stt,"\x1B[0m" "\x1B[K");
		TermPosCurs(con_height,1);
		write(1,stt,strlen(stt));
		TermPosCurs(viewcon_y+1,viewcon_x+1);

		/* input */
		act=0;
		do {
			r=TermRead();
			if (!strcmp(r,"\x1B[5~")) {		/* page up */
				if (view_ofs_y > 0) {
					file_cursor -= view_ofs_y * view_columns;
					act = 1;
				}
				else {
					unsigned long long step;

					step = (view_rows - 1) * view_columns;
					if (file_cursor >= step)	file_cursor -= step;
					else				file_cursor  = view_ofs_x;
					act = 1;
				}
			}
			else if (!strcmp(r,"\x1B[6~")) {	/* page down */
				if (view_ofs_y < (view_rows-1)) {
					file_cursor += ((view_rows-1) - view_ofs_y) * view_columns;
					act = 1;
				}
				else {
					file_cursor += (view_rows - 1) * view_columns;
					act = 1;
				}

				/* keep it from going past EOF */
				if (file_cursor >= file_size) {
					if (file_size == 0)	file_cursor = 0;
					else			file_cursor = file_size - 1;
				}
			}
			else if (!strcmp(r,"\x1B[A")) {		/* up arrow */
				act = 1;
				if (view_ofs_y > 0) {
					file_cursor -= view_columns;
				}
				else if (file_cursor >= view_columns) {
					file_cursor -= view_columns;
				}
			}
			else if (!strcmp(r,"\x1B[B")) {		/* down arrow */
				file_cursor += view_columns;
				act = 1;

				if (file_cursor >= file_size) {
					if (file_size == 0)	file_cursor = 0;
					else			file_cursor = file_size - 1;
				}
			}
			else if (!strcmp(r,"\x1B[D")) {		/* left arrow */
				if (view_tab != 0) {
					if (view_ofs_x > 0) {
						file_cursor--;
					}
					else {
						if (view_ofs_y > 0) {
							file_cursor--;
						}
						else {
							if (file_cursor > 0) {
								file_cursor--;
							}
						}
					}
				}
				
				act = 1;
			}
			else if (!strcmp(r,"\x1B[C")) {		/* right arrow */
				if (view_tab != 0) {
					if (file_size == 0) {
						file_cursor = 0;
					}
					else if (file_cursor < (file_size-1)) {
						file_cursor++;
					}
				}

				act = 1;
			}
			else if (!strcmp(r,"\x1B[1~")) {	/* HOME */
				if (view_ofs_x > 0) {
					file_cursor -= view_ofs_x;
					viewup_cursor = 1;
					act = 1;
				}
			}
			else if (!strcmp(r,"\x1B[4~")) {	/* END */
				if (view_ofs_x < (view_columns-1)) {
					file_cursor += (view_columns-1) - view_ofs_x;
					viewup_cursor = 1;
					act = 1;
				}

				if (file_cursor >= file_size) {
					if (file_size == 0)	file_cursor = 0;
					else			file_cursor = file_size - 1;
				}
			}
			else if (!strcmp(r,"\x09")) {		/* TAB */
				view_tab = (view_tab + 1) % 3;
				if (view_tab == 1 && !view_with_hex) view_tab = 2;
				if (view_tab == 2 && !view_with_asc) view_tab = 0;
				viewup_cursor = 1;
				act = 1;
			}
			else if (!strcmp(r,"\x1B\x1B")) {	/* ESC+ESC */
				char *r2;
				
				TermPosCurs(con_height,1);
				printf("\x1B[?25l" "\x1B[0;1;43;31;7m" "Are you sure you want to quit?" "\x1B[0m" "\x1B[K");
				fflush(stdout);
				r2=TermRead();
				if (!strcasecmp(r2,"y")) {
					mainloop = 0;
					act = 1;
				}
				else {
					act = 1;
				}

				/* unhide cursor */
				printf("\x1B[?25h");
				fflush(stdout);
			}
			else if (!strcmp(r,":") && !view_modifymode) {		/* user is entering command */
				char buf[255];
				char *args[32];
				int argsc=0;
				int i;
				int good=0;

				TermPosCurs(con_height,1);
				printf("\x1B[K" "command: ");
				fflush(stdout);
				ReadInLine(buf,254);
				act = 1;
				i = 0;

				while (argsc < 31 && buf[i] != 0) {
					while (buf[i] == ' ') i++;
					if (buf[i] == '\"') {
						buf[++i] = 0;
						args[argsc++] = buf+i;
						while (buf[i] && buf[i] != '\"') i++;
						if (buf[i] == '\"') buf[i++] = 0;
					}
					else {
						args[argsc++] = buf+i;
						while (buf[i] && buf[i] != ' ') i++;
						if (buf[i] == ' ') buf[i++]=0;
					}
				}
				
				while (argsc < 32)
					args[argsc++] = "";
				
				if (!strcasecmp(args[0],"column")) {
					if (!strcasecmp(args[1],"width")) {
						view_columns = strtol(args[2],NULL,0);
						if (view_columns < 1) view_columns = 1;
						viewup_all = 1;
						good = 1;
					}
				}
				else if (!strcasecmp(args[0],"view")) {
					if (!strcasecmp(args[1],"sync")) {
						good = 1;
						view_offset = file_cursor;
						viewup_all = 1;
					}
				}
				else if (!strcasecmp(args[0],"truncate")) {
					if (!strcasecmp(args[1],"here")) {
						/* ok */
						if (ftruncate(file_fd,file_cursor) < 0) {
							TermPosCurs(con_height,1);
							printf("\x1B[K" "ERROR TRUNCATING FILE!!");
							fflush(stdout);
							do { r=TermRead(); } while (r[0] != 10);
						}

						file_size = file_cursor;
						if (file_size > 0) file_cursor--;
						viewup_all = 1;
						good = 1;
					}
					else if (!strcasecmp(args[1],"at") || !strcasecmp(args[1],"to")) {
						unsigned long long pt;

						pt = strtoull(args[2],NULL,0);
						if (ftruncate(file_fd,file_cursor) < 0) {
							TermPosCurs(con_height,1);
							printf("\x1B[K" "ERROR TRUNCATING FILE!!");
							fflush(stdout);
							do { r=TermRead(); } while (r[0] != 10);
						}

						file_size = pt;
						if (file_cursor >= file_size) {
							if (file_size == 0)	file_cursor = 0;
							else			file_cursor = file_size - 1;
						}
						good = 1;
					}
				}
				else if (!strcasecmp(args[0],"go")) {
					if (!strcasecmp(args[1],"to")) {
						if (isdigit(args[2][0])) {
							file_cursor = strtoll(args[2],NULL,0);
							good = 1;
						}
						else if (args[2][0] == '+') {
							unsigned long long delta;

							good = 1;
							delta = strtoull(args[2]+1,NULL,0);
							if ((0xFFFFFFFFFFFFFFFFLL - file_cursor) < delta)
								file_cursor = 0xFFFFFFFFFFFFFFFFLL;
							else
								file_cursor += delta;
						}
						else if (args[2][0] == '-') {
							unsigned long long delta;

							good = 1;
							delta = strtoull(args[2]+1,NULL,0);
							if (file_cursor < delta)
								file_cursor = 0;
							else
								file_cursor -= delta;
						}
						else if (!strcasecmp(args[2],"end")) {
							if (file_size == 0)	file_cursor = 0;
							else			file_cursor = file_size - 1;
							good = 1;
						}
					}
				}
				else if (!strcasecmp(args[0],"show")) {
					if (!strcasecmp(args[1],"hex")) {
						good = 1;
						if (!view_with_hex) {
							view_with_hex = 1;
							viewup_all = 1;
						}
					}
					else if (!strcasecmp(args[1],"asc")) {
						good = 1;
						if (!view_with_asc) {
							view_with_asc = 1;
							viewup_all = 1;
						}
					}
				}
				else if (!strcasecmp(args[0],"hide")) {
					if (!strcasecmp(args[1],"hex")) {
						good = 1;
						if (view_with_hex) {
							view_with_hex = 0;
							viewup_all = 1;
							if (view_tab == 1) view_tab = 2;
							if (view_tab == 2 && !view_with_asc) view_tab = 0;
						}
					}
					else if (!strcasecmp(args[1],"asc")) {
						good = 1;
						if (view_with_asc) {
							view_with_asc = 0;
							viewup_all = 1;
							if (view_tab == 2) view_tab = 0;
						}
					}
				}
				/* "quit" or "q" (bad VIM habits die hard) */
				else if (!strcasecmp(args[0],"quit") || !strcasecmp(args[0],"q")) {
					mainloop = 0;
					good = 1;
				}
				else if (!strcasecmp(args[0],"help")) {
					printf("\x1B[2J\x1B[1;1H");
					printf("KEYS:\n");
					printf("ARROW KEYS            CONTROLS THE CURSOR POSITION.\n");
					printf("PAGE UP, PAGE DOWN    JUMPS SEVERAL ROWS.\n");
					printf("HOME, END             JUMPS THE CURSOR TO THE BEGINNING OR END OF A ROW.\n");
					printf("ESC,ESC               QUITS THIS PROGRAM.\n");
					printf(":                     GOES INTO COMMAND MODE.\n");
					printf("ESC,M                 GOES INTO MODIFY MODE.\n");
					printf("ESC,S                 EXITS MODIFY MODE.\n");
					printf("\n");
					printf("COMMAND SUMMARY\n");
					printf("quit                  QUITS THE PROGRAM.\n");
					printf("open                  OPENS A FILE FOR PEEKING.\n");
					printf("openrw                OPENS A FILE FOR MODIFICATION.\n");
					printf("column width <n>      SETS THE COLUMN WIDTH TO <n> BYTES/ROW\n");
					printf("view sync             SETS THE VIEWPORT TO THE CURSOR POSITION\n");
					printf("truncate here         TRUNCATES THE FILE AT THE CURSOR POSITION\n");
					printf("truncate <at|to> <n>  TRUNCATES THE FILE AT THE GIVEN OFFSET\n");
					printf("go to <-|+><n>        JUMPS THE CURSOR TO OFFSET <n> OR RELATIVE OFS IF +/-<n>\n");
					printf("go to end             JUMPS TO THE END OF THE FILE\n");
					printf("show <panel>          SHOWS THE SPECIFIED PANEL. 'PANEL' CAN BE 'asc' or 'hex'\n");
					printf("hide <panel>          HIDES THE SPECIFIED PANEL.\n");
					printf("\n");
					printf("HIT RETURN TO CONTINUE.\n");

					do { r=TermRead(); } while (r[0] != 10);
					viewup_all = 1;
					good = 1;
				}
				else if (!strcasecmp(args[0],"open")) {
					if (!FaOpen(args[1],O_RDONLY)) {
						TermPosCurs(con_height,1);
						printf("\x1B[K" "Unable to open file");
						fflush(stdout);
						do { r=TermRead(); } while (r[0] != 10);
					}
					
					file_cursor = 0;
					view_offset = 0;
					viewup_all = 1;
					good = 1;
				}
				else if (!strcasecmp(args[0],"openrw")) {
					if (!FaOpen(args[1],O_RDWR)) {
						TermPosCurs(con_height,1);
						printf("\x1B[K" "Unable to open file");
						fflush(stdout);
						do { r=TermRead(); } while (r[0] != 10);
					}

					file_cursor = 0;
					view_offset = 0;
					viewup_all = 1;
					good = 1;
				}
				else if (!strlen(args[0])) {
					good = 1;
				}

				if (!good) {
					TermPosCurs(con_height,1);
					printf("\x1B[K" "UNKNOWN COMMAND");
					fflush(stdout);
					do { r=TermRead(); } while (r[0] != 10);
				}
			}
			else if (r[0] >= 32 && r[0] < 127 && view_modifymode && file_cursor < file_size) {
				char *r2;
				char buft[3];
				char cc;
				
				/* modify! */
				if (view_tab == 1) {
					if (isxdigit(r[0])) {
						TermPosCurs(con_height,1);
						printf("\x1B[K" "%c?",r[0]);
						fflush(stdout);
						
						buft[0] = r[0];
						r2=TermRead();
						if (isxdigit(r2[0])) {
							buft[1] = r2[0];
							buft[2] = 0;
							cc = (char)strtol(buft,NULL,16);	// hexadecimal
							FaSeek(file_cursor);
							write(file_fd,&cc,1);
							viewup_cursor = 1;
							VRlastrow = -1;
							if (file_cursor < (file_size-1)) file_cursor++;
							DrawRowCacheFlush();
						}
					}
				}
				else if (view_tab == 2) {
					FaSeek(file_cursor);
					write(file_fd,r,1);
					viewup_cursor = 1;
					VRlastrow = -1;
					if (file_cursor < (file_size-1)) file_cursor++;
					DrawRowCacheFlush();
				}

				act = 1;
			}
			else if (!strcmp(r,"\x1Bm")) {		/* command to enter modify mode */
				act = 1;
				if (file_mode & O_RDWR) {
					view_modifymode = 1;
				}
				else {
					TermPosCurs(con_height,1);
					printf("\x1B[K" "Can't modify a file in read-only mode");
					fflush(stdout);
					do { r=TermRead(); } while (r[0] != 10);
				}
			}
			else if (!strcmp(r,"\x1Bs")) {		/* command to exit modify mode */
				view_modifymode = 0;
				act = 1;
			}
		} while (!act);
	}

	TermPosCurs(255,1);
	printf("\x1B[0m" "\x1B[K");

	if (!TermReset())
		fprintf(stderr,"%s: Unable to restore terminal!\n",argv[0]);

	return 0;
}

