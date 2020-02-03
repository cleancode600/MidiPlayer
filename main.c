/* Read and play MIDI files
 *
 * Original release by cleancode600
 *
 * 3rd february 2020
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "beep.h"
struct header{
int format;
int ntrks;
union{
int ppq;
int fps;
};
};
struct event{
long time;
float note;
};
int noteindex=0,Msgindex = 0;/* index of next available location in Msg */
char *Msgbuff = NULL;	/* message buffer */
long currtime = 0L,rbytes = 0L;		/* current time in delta-time units */
float tick;
long readvarinum();
int readmt(char s[]);
long int read32bit();
long int to32bit(int, int, int, int);
short int read16bit();
short int to16bit(int, int);
void readheader();
void badbyte(int);
void metaevent(int);
int readtrack(struct event *);		 /* read a track chunk */
void sysex(void);
void chanmessage(int,int,int,struct event *);
int egetc();
void mferror(char *);
void meta_text(int);
void meta_offest();
void noteadd(float,int,struct event *);
void sing(struct event *);
void getfiledir(void);
FILE *midi,*outfile;
struct header mthd;
int main(){
	struct event note[10000];
	Msgbuff=(char *) malloc( (unsigned)(sizeof(char)*128) );
	getfiledir();
	outfile=fopen("outfile.txt","w");
	readheader();
    while(readtrack(note)){
		if(noteindex!=0)
		note[noteindex].time=note[noteindex-1].time;
	};
	sing(note);
}
void noteadd(float freq,int v,struct event *note)
{
	if(noteindex==0 && v==0){
		mferror("No note is on");
	}
	if(v==0){
		note[noteindex].note= 0;
		note[noteindex++].time=currtime;
	}	
	else {
		note[noteindex].note= freq;
		note[noteindex++].time=currtime;
	}	
}
void sing(struct event *note ){
	int i,length;
	for(i=0;i<noteindex;i++){
		length=(int)(tick*((float)(note[i+1].time-note[i].time)));
		if(length!=0){
			if(length<5){
				length=5;  //this beep library cant play less than this length
			}
		printf("%d  %d\n",((int)(note[i].note)),length);
		beep(((int)(note[i].note)),length);
		}

	}
}
void chanmessage(int status,int c1,int c2,struct event *note)
{
	if(127<c1 || 127<c2)
		mferror("incorrect data byte");
	else{
	int chan = status & 0xf;
	float freq;
	switch ( status & 0xf0 ) {
	case 0x80:
		freq=440*pow(2,(float)(c1-69)/12);
		fprintf(outfile,"Off-%d/Note(freq)=%d_%.2f/V=%d\n",chan,c1,freq,c2);
		noteadd(freq,0,note);
		break;
	case 0x90:
		freq=440*pow(2,(float)(c1-69)/12);
		fprintf(outfile,"On-%d/Note(freq)=%d_%.2f/V=%d\n",chan,c1,freq,c2);
		noteadd(freq,c2,note);
		break;
	case 0xa0:
		fprintf(outfile,"Aftertouch-%d/controller number=%d/pressure value=%d\n",chan,c1,c2);
		break;
	case 0xb0:
		fprintf(outfile,"Control Change-%d/Controller=%d/Controller value=%d\n",chan,c1,c2);
		break;
	case 0xc0:
		fprintf(outfile,"Program Change-%d/instrument=%d\n",chan,c1);
		break;
	case 0xd0:
		fprintf(outfile,"After-touch-%d/pressure value=%d\n",chan,c1);
		break;
	case 0xe0:
		fprintf(outfile,"Control Change-%d/Pitch Wheel(semitone)=%f\n",chan,2*((double)((c2<<7)+c1-8192))/8191);
		break;
	}
	}
}
int readmt(char s[])		/* read through the "MThd" or "MTrk" header string */
{
	static int i=1;
	int n = 0;
	char *p = s;
	int c;

	while ( n++<4 && (c=(egetc())) != EOF ){
		if ( c != *p++ ) {
			char buff[32];
			(void) strcpy(buff,"expecting ");
			(void) strcat(buff,s);
			mferror(buff);
		}
	}
	if(n==5 && strcmp(s,"MTrk")==0){
		fprintf(outfile,"MTrk%d:",i);
	}
	return(c);
}
void readheader()		/* read a header chunk */
{
	int div;

	if ( readmt("MThd") == EOF )
		return;

	read32bit();
	mthd.format = read16bit();
	mthd.ntrks = read16bit();
	div=read16bit();
	if (div & 0x80==0){
		mthd.fps=(div & 0xf)*(div>>3);
		tick==1000/((float)mthd.fps);
		printf("fps:%d\n",mthd.fps);
		printf("tick=%f\n",tick);
	}
	else{
		mthd.ppq=div;
	}
	fprintf(outfile,"format %d, %d tracks, division: %d ticks / 1/4 note\n",mthd.format,mthd.ntrks,mthd.fps);  
}
int readtrack(struct event *note)		 /* read a track chunk */
{

	static int chantype[] = {0,0,0,0,0,0,0,0,2,2,2,2,1,1,2,0};
	long lookfor,dltime;
	int c, c1, type;
	int sysexcontinue = 0;	/* 1 if last message was an unfinished sysex */
	int running = 0;	/* 1 when running status used */
	int status = 0;		/* status value (e.g. 0x90==note-on) */
	int needed;

	if(readmt("MTrk")==EOF)
		return(0);
	rbytes  =read32bit();fprintf(outfile,"%ld bytes\n",rbytes);
	currtime = 0;

	while ( rbytes > 0 ) {
		dltime =readvarinum();
		fprintf(outfile,"Delta time =%ld\n",dltime);
		currtime += dltime;	

		c = egetc();

		if ( sysexcontinue && c != 0xf7 )
			mferror("didn't find expected continuation of a sysex");

		if ( (c & 0x80) == 0 ) {	 /* running status? */
			if ( status == 0 )
			mferror("unexpected running status");
			running = 1;
			c1 = c;
			c = status;
		}
		else if (c < 0xf0) {
			status = c;
			running = 0;
		}

		needed = chantype[ (c>>4) & 0xf ];

		if ( needed ) {		/* ie. is it a channel message? */

			if ( !running ){
				c1 = egetc();
			}
			chanmessage( status, c1, (needed>1) ? egetc() : 0 ,note);
			continue;;
		}

		switch ( c ) {

		case 0xff:			/* meta event */

			type = egetc();
			lookfor=rbytes-1-readvarinum();
			Msgindex=0;

			while ( rbytes > lookfor )
				Msgbuff[Msgindex++] = egetc();

			metaevent(type);
			break;

		case 0xf0:		/* start of system exclusive */

			lookfor = rbytes-1 - readvarinum();
			Msgindex=0;

			while ( rbytes > lookfor ){
				Msgbuff[Msgindex++] = (c=egetc());
				if(c & 0x80 !=0)
				mferror("incorrect sys message");
			}	
			if ( c==0xf7){
				Msgindex--;
				sysex();				
			}
			else
				sysex();
				sysexcontinue = 1;  /* merge into next msg */
			break;

		case 0xf7:	/* sysex continuation or arbitrary stuff */

			lookfor = rbytes -1- readvarinum();

			if ( ! sysexcontinue )
				Msgindex=0;

			while ( rbytes > lookfor )
				Msgbuff[Msgindex++] = (c=egetc());

			if ( ! sysexcontinue ) {
				fprintf(outfile,"%x\n",Msgbuff[0]); // scaped event
			}
			else{
				if( c == 0xf7 ){
					Msgindex--;
				}
				sysex();
				sysexcontinue = 0;
			}
			break;		
		default:
			badbyte(c);
			break;
		}
	}
	return(1);
}
void sysex(void){
	fprintf(outfile,"Manufacturer=%d/Command=%x %x\n",Msgbuff[0],Msgbuff[1],Msgbuff[2]);
}
void metaevent(int type)
{
	int i,leng = Msgindex;
	char *m = Msgbuff;
	long int mspqn;
	switch  ( type ) {
	case 0x00:
		fprintf(outfile,"Sequence Number=%d\n",to16bit(m[0],m[1]));
		break;
	case 0x01:
	case 0x02:	
	case 0x03:	
	case 0x04:
	case 0x05:	
	case 0x06:	
	case 0x07:	
	case 0x08:
	case 0x09:
	case 0x0a:
	case 0x0b:
	case 0x0c:
	case 0x0d:
	case 0x0e:
	case 0x0f:
		meta_text(type);
		break;
	case 0x20:
		fprintf(outfile,"Midi channel prefix:%d\n",Msgbuff[0]);
		break;
	case 0x21:
		fprintf(outfile,"Midi port:%d\n",Msgbuff[0]);
		break;
	case 0x2f:	
		if ( rbytes==0 )
			fprintf(outfile,"There are no more byte in trak\n");
		else{
			mferror("There are more byte in trak");
		}
		break;
	case 0x51:
		mspqn=to32bit(0,m[0],m[1],m[2]);
		fprintf(outfile,"Set Tempo (ms/qn)=%ld\n",mspqn);
		tick=(float)mspqn/(float)mthd.ppq/1000;
		break;
	case 0x54:
		meta_offest();
		break;
	case 0x58:
		fprintf(outfile,"Time signature=%d/Midi clock=%d/Beat=%dnote\n",Msgbuff[0]/Msgbuff[1],Msgbuff[2],Msgbuff[3]);
		break;
	case 0x59:
		fprintf(outfile,"Key signature=%d/scale=%d\n",Msgbuff[0],Msgbuff[1]);
		break;
	case 0x7f:
			fprintf(outfile,"ID=%d/command=\n",Msgbuff[0]);
			for(i=1;i<Msgindex;i++){
			fprintf(outfile,"%d /\n",Msgbuff[i]);
			}
		break;
	default:
			fprintf(outfile,"Unknown type\n");
	}
}
void meta_text(int type){
	int i;
	switch(type){
		case 0x01:
		fprintf(outfile,"Text Event:");
		break;
	case 0x02:	
		fprintf(outfile,"Copyright Notice:");
		break;
	case 0x03:	
		fprintf(outfile,"Track Name:");
		break;
	case 0x04:
		fprintf(outfile,"Instrument Name:");
		break;
	case 0x05:	
		fprintf(outfile,"Lyric;");
		break;
	case 0x06:	
		fprintf(outfile,"Marker:");
		break;
	case 0x07:	
		fprintf(outfile,"Cue Point:");
		break;
	break;
	case 0x08:
	case 0x09:
	case 0x0a:
	case 0x0b:
	case 0x0c:
	case 0x0d:
	case 0x0e:
	case 0x0f:
		fprintf(outfile,"Some Text:");
	}
	for(i=0;i<Msgindex;i++){
		fprintf(outfile,"%c",Msgbuff[i]);
	}
	fprintf(outfile,"\n");
}
void meta_offest(){
	int hour,leng=Msgindex;
	float fps;
	float smpte=
	hour=Msgbuff[0] & 0x1f;
	switch(Msgbuff[0]>>5){
		case 0:
			fps=24;
		break;
		case 1:
			fps=25;
		break;
		case 2:
			fps=29.97;
		break;
		case 3:
			fps=30;
		break;
		default:
		mferror("Invalid SMPTE");
	}
	fprintf(outfile,"SMPTE Offset:");
	if(leng>0)
			fprintf(outfile,"hour=%d",hour);
	if(leng>1)
			fprintf(outfile,"/minute=%d",Msgbuff[1]);
	if(leng=3)
			fprintf(outfile,"/second=%d",Msgbuff[2]);
	if(leng=4)
			fprintf(outfile,"/second=%f",Msgbuff[2]+fps*(double)Msgbuff[3]);
	if(leng=5){
		fprintf(outfile,"/second=%f",Msgbuff[2]+fps*((double)Msgbuff[3]+(double)Msgbuff[4]/100));
	}
	fprintf(outfile,"\n");
}
void badbyte(int c)
{
	char buff[32];

	(void) sprintf(buff,"unexpected byte: 0x%02x",c);
	mferror(buff);
}
int egetc()			/* read a single character and abort on EOF */
{
	int c = fgetc(midi);
	if ( c == EOF && rbytes!=0)
		mferror("premature EOF");
		rbytes--;
	return(c);
}
long readvarinum()
{
	long value;
	int c;

	c = egetc();
	value = c;
	if ( c & 0x80 ) {
		value &= 0x7f;
		do {
			c = egetc();
			value = (value << 7) + (c & 0x7f);
		} while (c & 0x80);
	}
	return (value);
}
long int read32bit()
{
	int c1, c2, c3, c4;

	c1 = egetc();
	c2 = egetc();
	c3 = egetc();
	c4 = egetc();
	return to32bit(c1,c2,c3,c4);
}
short int read16bit()
{
	int c1, c2;
	c1 = egetc();
	c2 = egetc();
	return to16bit(c1,c2);
}
long to32bit(int c1,int c2,int c3,int c4)
{
	long value = 0L;

	value = (c1 & 0xff);
	value = (value<<8) + (c2 & 0xff);
	value = (value<<8) + (c3 & 0xff);
	value = (value<<8) + (c4 & 0xff);
	return (value);
}
short int to16bit(int c1,int c2){
	return ((c1 & 0xff ) << 8) + (c2 & 0xff);
}
void mferror(char *s)
{
	fprintf(outfile,"%s\n",s);
	exit(1);
}
void getfiledir(void){
    char dir[30];
    printf("Enter directory file:\n");
    scanf("%s",dir);
    midi = fopen(dir,"r");
    if(midi == NULL) mferror("not found");
}

