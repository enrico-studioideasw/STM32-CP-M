/*
ENRICO
Ho su blackpill_heartbeat una console e uno schermo 80x25 non perfetto ma funzionante.
Ora voglio includere il cp/m.
Quindi devo modificare questo progetto perchè:
- integri tutto il mio codice (da fare)
- non usi il led di disco, o almeno lo usi sul pin PC13 (Cerca la #define LED se esiste, se no a posto così)
- usi la flash sui pin che dico io (riga 92, questo file)
- non usi la seriale ma la mia console per input e output  (abstraction_arduino.h, linea 503)
Fatto questo il cp/m dovrebbe girare..
Al momento è definito CPU1, internal CCP, 64K ram 
*/
#define LED PC13
char _LASTC=0;
char _SHIFT=0; 
char _ALT=0; 
char _C_VALID=0;

#include "globals.h"
#include <SPI.h>
#define SDFAT_FILE_TYPE 1 // Uncomment for Due and Teensy
#include <SdFat.h>  // One SD library to rule them all - Greinman SdFat from Library Manager

SdFat SD; 
char LEDinv=1; 
//#include "hardware/arduino/due_sd_tf.h"
#define sDELAY 50
#define DELAY 100
#include "abstraction_arduino.h"

// PUN: device configuration
#ifdef USE_PUN
File pun_dev;
int pun_open = FALSE;
#endif

// LST: device configuration
#ifdef USE_LST
File lst_dev;
int lst_open = FALSE;
#endif
#include "ram.h"
#include "console.h"
#include CPU
#include "disk.h"
#include "host.h"
#include "cpm.h"
#ifdef CCP_INTERNAL
#include "ccp.h"
#endif

/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
#include "fonts8x8.h"
/*
  Partiamo da qui: se posso generare il sync di riga poi procedo in qualche modo..
  PAL -> schermo 50 hz, semischermo 100hz
  312 righe->trigger di riga 31.2khz
  56 vuote / 200 di dati / 56 vuote
  Quasi 4000 clock per riga.. Ho tempo per fare un quadro decente.. e anche i colori se capisco come mosulare la sottoportante.
  Se riesco a fare una macro di attesa con i tempi che voglio io sono a posto.
  Comincia a vedersi un quadro. Fa abbastanza schifo ma almeno si vede.
  Le prime righe sono sballate.. perchè?  Sembra un problema di sync di quadro.
  Copia da qui:
  http://www.batsocks.co.uk/readme/video_timing.htm
  - prendi l'oscilloschioppo per capire se il segnale video dell'altro progetto è decente.
  Il segnale di sincronismo verticale è composto di 15 impulsi, tutti intervallati di mezza riga: 
· 5 impulsi brevi (impulsi secondari anteriori), ciascuno di durata pari al 4,5% di quella di una riga intera, 
· 5 impulsi più larghi (impulsi principali), pari ciascuno al 41% della durata di una riga, 
· 5 impulsi brevi (impulsi secondari posteriori), pari ciascuno al 4,5% della durata di una riga. 
  Durata della riga -> 64uS impulso di 3uS circa 

  Diamolo per funzionante anche se non è fantastico..
*/
#define HSCR 80
#define VSCR 25
#define Audio  PA12
//#define Video   PA5
//#define VideoA  PA5  // video  B/N
//#define VideoB  PA6  // sync
#define Video   PA4
#define VideoA  PA4  // video  B/N
#define VideoB  PA5  // sync
#define VideoC  PA6  // gray

#define videoUp   GPIOA -> BSRR = (1 << 4); 
#define videoDown GPIOA -> BSRR = (1 << 4+16); 
#define videocol0 GPIOA -> BSRR = (1 << 4+16) | (1 << 5+16) | (1 << 6+16);
#define videocol1 GPIOA -> BSRR = (1 << 4+16) | (1 << 5+16) |  (1 << 6);
#define videocol2 GPIOA -> BSRR = (1 << 4+16) | (1 << 5)    |  (1 << 6+16);
#define videocol3 GPIOA -> BSRR = (1 << 4+16) | (1 << 5)    |  (1 << 6);
#define videocol4 GPIOA -> BSRR = (1 << 4)    | (1 << 5+16) |  (1 << 6+16);
#define videocol5 GPIOA -> BSRR = (1 << 4)    | (1 << 5+16) |  (1 << 6);
#define videocol6 GPIOA -> BSRR = (1 << 4)    | (1 << 5)    |  (1 << 6+16);
#define videocol7 GPIOA -> BSRR = (1 << 4)    | (1 << 5)    | (1 << 6);
PROGMEM const unsigned long v_colors[]={ (1 << 4+16) | (1 << 5+16) | (1 << 6+16), 
                                         (1 << 4)    | (1 << 5+16) |  (1 << 6),  
                                         (1 << 4)    | (1 << 5)    |  (1 << 6+16),  
                                         (1 << 4)    | (1 << 5)    | (1 << 6) };


#define syncUp    GPIOA -> BSRR = (1 << 5);
#define syncDown  GPIOA -> BSRR = (1 << 5+16);
#define grayUp    GPIOA -> BSRR = (1 << 6);
#define grayDown  GPIOA -> BSRR = (1 << 6+16);


//Qui le linee di SPI e SIM 
#define SPIMISO PB14 
#define SPIMOSI PB15
#define SPICLK  PB13
#define SPISEL  PB12 //Selettore per SD

//Qui le linee di scansione della tastiera. Finalmente va.
#define UPSCAN0 PB9
#define UPSCAN1 PB1 
#define UPSCAN2 PB2 
#define UPSCAN3 PC13
#define UPSCAN4 PC14
#define DOWNSCAN0 PA7 
#define DOWNSCAN1 PB0 
#define DOWNSCAN2 PB3
#define DOWNSCAN3 PB4
#define DOWNSCAN4 PB5
#define DOWNSCAN5 PB6
#define DOWNSCAN6 PB7
#define DOWNSCAN7 PB8

int _line = 0;
int _screen = 0;
int _vmode=0; 

unsigned char screen[HSCR * VSCR*8]; //16K sprecati, ma ho la grafica HGA e CGA oltre al testuale. 

void hSync() {
    videoDown;  
    delayMicroseconds(1);  // Front porch
    syncDown;
    delayMicroseconds(4);  // Horizontal Sync
    syncUp;
    delayMicroseconds(7);  // Back porch
}

void vSync() {  //! THIS IS A FAKE PROGRESSIVE SCAN FOR PAL. TESTING SHOWS THIS
                //! DOES NOT REALLY WORK FOR NTSC. THIS MAY CAUSE DAMAGE TO AN
                //! OLD STYLE CRT BUT THAT MIGHT BE CONJECTURE.
                // TODO: Add support for NTSC.
                //* There are some strange timing issues with this and I'm not
                // sure as to why
    if (_line < 3) {
        videoDown;
        syncUp;
        delayMicroseconds(28);
        syncDown;
        delayMicroseconds(4);
        syncUp;
        delayMicroseconds(28);
        syncDown;
        _line++;
        return;
    } else if (_line == 3) {
        videoDown;
        syncUp;
        delayMicroseconds(27);
        //delayMicroseconds(27);
        syncDown;
        delayMicroseconds(4);
        syncUp;
        delayMicroseconds(2);
        syncDown;
        _line = 4;
        return;
    } else if (_line < 6) {
        videoDown;
        syncUp;
        delayMicroseconds(2);
        syncDown;
        delayMicroseconds(29);
        syncUp;
        delayMicroseconds(2);
        syncDown;
        _line++;
        return;
    }  else if (_line > 309) 
    {
        videoDown;
        syncUp;
        delayMicroseconds(2);
        syncDown;
        delayMicroseconds(29);
        syncUp;
        delayMicroseconds(2);
        syncDown;
        _line++;
        if (_line >= 313) {
            _line = 0;
        }
        return;
    }
}

unsigned int k_tick=0; 
unsigned int last_scan; 
uint32 LINE_TICKS, t_next; 
HardwareTimer *MyTim; 
void LineHandler2()
{ noInterrupts();
  t_next += LINE_TICKS;
  MyTim->setCaptureCompare(2, t_next, TICK_COMPARE_FORMAT);
  vSync();
  hSync();
  if (_line==267)
  { k_tick++; 
    if (k_tick%40==0) //cursore che lampeggia ogni 40 intervalli.
    { vid_blink(); 
    };
  } 
  if (_line>33) 
  { //Funziona, in C ottengo uno schermo di circa 80x25 su un blackpill. 
    videoUp; 
    if (_line>=66 && _line<266)
    { 
      if (_vmode==0) //Testo, 80x25
      { unsigned char l=(unsigned char)(_line-66); //La riga corrente 
        unsigned int t;  for (t=0; t<50; t++ ) __asm__("nop");
        unsigned int sc=(l & 0x07); //La linea di scansione corrente. Da 0 a 7. Mentre (l & 0xF8) >>3 è la riga.  
        unsigned int scpos= HSCR*((l & 0xF8)>> 3); //posizione carattere sullo schermo  
        unsigned int scl; unsigned int i=HSCR; 
        while (i>0)
        { t=screen[ scpos ];
          scl=font8x8[sc + ( (t & 0x7F) << 3)];
          if  (!(t & 0x80)) scl= scl ^ 0xFF; 
          if (scl &  1<<7) { videoDown;  } else  videoUp;
          __asm__("nop\n nop\n");      
          if (scl &  1<<6) { videoDown;  } else  videoUp;   
          __asm__("nop\n nop\n");
          if (scl &  1<<5) { videoDown;  } else  videoUp;
          __asm__("nop\n nop\n");
          if (scl &  1<<4) { videoDown;  } else  videoUp;   
          __asm__("nop\n nop\n");
          if (scl &  1<<3) { videoDown;  } else  videoUp;    
          __asm__("nop\n nop\n");
          if (scl &  1<<2) { videoDown;  } else  videoUp;    
          __asm__("nop\n nop\n");
          if (scl &  1<<1) { videoDown;  } else  videoUp;    
          __asm__("nop\n nop\n");
          if (scl &  1<<0) { videoDown;  } else  videoUp;    
          i--; scpos++; 
        }; 
      } else if (_vmode==1) //HGA, 640X200 B/N
      { unsigned char l=(unsigned char)(_line-66); //La riga corrente 
        unsigned int scl; unsigned int i=HSCR; 
        unsigned int scpos=l*80; 
        unsigned int t;  for (t=0; t<50; t++ ) __asm__("nop");
        while (i>0)
        { scl=screen[ scpos ];
          if (scl &  1<<7) { videoUp;  } else  videoDown;
          __asm__("nop\n nop\n nop\n");      
          if (scl &  1<<6) { videoUp;  } else  videoDown;   
          __asm__("nop\n nop\n nop\n");
          if (scl &  1<<5) { videoUp;  } else  videoDown;
          __asm__("nop\n nop\n nop\n");
          if (scl &  1<<4) { videoUp;  } else  videoDown;   
          __asm__("nop\n nop\n nop\n");
          if (scl &  1<<3) { videoUp;  } else  videoDown;    
          __asm__("nop\n nop\n nop\n");
          if (scl &  1<<2) { videoUp;  } else  videoDown;    
          __asm__("nop\n nop\n nop\n");
          if (scl &  1<<1) { videoUp;  } else  videoDown;    
          __asm__("nop\n nop\n nop\n");
          if (scl &  1<<0) { videoUp;  } else  videoDown;
          i--; scpos++;      
        };
      } else if (_vmode==2) //CGA, 340X200 4col
      { //SCREEN è array 80x200, ciascun byte descrive 4 punti. Quindi 4*80=320, 200 per verticale. 
        //Uso un array che definisce i colori che voglio in termini di azioni in bit su BSRR  
        unsigned char l=(unsigned char)(_line-66); //La riga corrente 
        unsigned char scl; unsigned int i=HSCR; //- mezzo schermo, non ci stiamo nei tempi 
        unsigned int scpos=l*HSCR; 
        unsigned int t;  for (t=0; t<50; t++ ) __asm__("nop");        
        while (i>0)
        { scl=screen[ scpos ];
          GPIOA -> BSRR = v_colors[(scl & 0xC0) >> 6];
          __asm__("nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n");      
          GPIOA -> BSRR = v_colors[(scl & 0x30) >> 4];
          __asm__("nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n");      
          GPIOA -> BSRR = v_colors[(scl & 0x0C) >> 2];
          __asm__("nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n");      
          GPIOA -> BSRR = v_colors[(scl & 0x03) >> 0];
          __asm__("nop\n nop\n nop\n nop\n nop\n");      
          i--; scpos++;       
        };
      }; 
    };
  };
  GPIOA -> BSRR = v_colors[0];
  syncUp; //Prova se va bene.. 
  videoUp;
  _line++; 
  interrupts();
};
//Qui devo gestire il flusso caratteri in uscita. Putchar e scrolling.
//Fine del video, qui gestiamo la tastiera.

void k_init()
{ pinMode(UPSCAN0, OUTPUT);   //Stesso PIN 
  digitalWrite(UPSCAN0, HIGH);
  pinMode(UPSCAN1, OUTPUT);  //B10->B1
  digitalWrite(UPSCAN1, HIGH);
  pinMode(UPSCAN2, OUTPUT); //B11->B2
  digitalWrite(UPSCAN2, HIGH);
  pinMode(UPSCAN3, OUTPUT); //stesso
  digitalWrite(UPSCAN3, HIGH);
  pinMode(UPSCAN4, OUTPUT); //stesso
  digitalWrite(UPSCAN4, HIGH);
  
  pinMode(DOWNSCAN0, INPUT_PULLUP); //Pin 8, era B0 diventa A7
  pinMode(DOWNSCAN1, INPUT_PULLUP); //Pin 7 era B1 diventa B0 
  pinMode(DOWNSCAN2, INPUT_PULLUP); //stesso
  pinMode(DOWNSCAN3, INPUT_PULLUP); //stesso
  pinMode(DOWNSCAN4, INPUT_PULLUP); //stesso
  pinMode(DOWNSCAN5, INPUT_PULLUP); //stesso
  pinMode(DOWNSCAN6, INPUT_PULLUP); //stesso
  pinMode(DOWNSCAN7, INPUT_PULLUP); //stesso
  last_scan=0;
};

void k_scan() //12usec + codice 
{ //colonne:  b9,b10, b11, c13, c14 le imposto come uscite 
  //righe: B0, B1,  B3, B4, B5, B6, B7, B8 le imposto come entrate
  //k_init(); //Reinizializzo a ogni scansione, qualcosa a intervalli da fastidio.
  int i;
  _LASTC=0;
  digitalWrite(UPSCAN0, HIGH);
  digitalWrite(UPSCAN1, HIGH);
  digitalWrite(UPSCAN2, HIGH);
  digitalWrite(UPSCAN3, HIGH);
  digitalWrite(UPSCAN4, HIGH);
  digitalWrite(UPSCAN0, LOW);
  _SHIFT=digitalRead(DOWNSCAN6)==0; 
  if (digitalRead(DOWNSCAN0)==0) _LASTC='1';
  if (digitalRead(DOWNSCAN1)==0) _LASTC='6'; 
  if (digitalRead(DOWNSCAN2)==0) _LASTC='q'; 
  if (digitalRead(DOWNSCAN3)==0) _LASTC='y'; 
  if (digitalRead(DOWNSCAN4)==0) _LASTC='a'; 
  if (digitalRead(DOWNSCAN5)==0) _LASTC='h';
  //Qui manca shift 
  if (digitalRead(DOWNSCAN7)==0) _LASTC='b'; 
  digitalWrite(UPSCAN0, HIGH);
  digitalWrite(UPSCAN1, LOW);
  //for (i=0; i<10; i++) __asm__("nop\n nop\n nop\n nop\n");
  delay(1);
  if (digitalRead(DOWNSCAN0)==0) _LASTC='2'; 
  if (digitalRead(DOWNSCAN1)==0) _LASTC='7'; 
  if (digitalRead(DOWNSCAN2)==0) _LASTC='w'; 
  if (digitalRead(DOWNSCAN3)==0) _LASTC='u'; 
  if (digitalRead(DOWNSCAN4)==0) _LASTC='s'; 
  if (digitalRead(DOWNSCAN5)==0) _LASTC='j'; 
  if (digitalRead(DOWNSCAN6)==0) _LASTC='z'; 
  if (digitalRead(DOWNSCAN7)==0) _LASTC='n'; 
  digitalWrite(UPSCAN1, HIGH);
  digitalWrite(UPSCAN2, LOW);
  //for (i=0; i<10; i++) __asm__("nop\n nop\n nop\n nop\n");
  delay(1);
  if (digitalRead(DOWNSCAN0)==0) _LASTC='3'; 
  if (digitalRead(DOWNSCAN1)==0) _LASTC='8'; 
  if (digitalRead(DOWNSCAN2)==0) _LASTC='e'; 
  if (digitalRead(DOWNSCAN3)==0) _LASTC='i'; 
  if (digitalRead(DOWNSCAN4)==0) _LASTC='d'; 
  if (digitalRead(DOWNSCAN5)==0) _LASTC='k'; 
  if (digitalRead(DOWNSCAN6)==0) _LASTC='x'; 
  if (digitalRead(DOWNSCAN7)==0) _LASTC='m'; 
  digitalWrite(UPSCAN2, HIGH);
  digitalWrite(UPSCAN3, LOW);
  //__asm__("nop\n nop\n nop\n nop\n");
  delay(1);
  if (digitalRead(DOWNSCAN0)==0) _LASTC='4'; 
  if (digitalRead(DOWNSCAN1)==0) _LASTC='9'; 
  if (digitalRead(DOWNSCAN2)==0) _LASTC='r'; 
  if (digitalRead(DOWNSCAN3)==0) _LASTC='o'; 
  if (digitalRead(DOWNSCAN4)==0) _LASTC='f'; 
  if (digitalRead(DOWNSCAN5)==0) _LASTC='l'; 
  if (digitalRead(DOWNSCAN6)==0) _LASTC='c'; 
  _ALT=digitalRead(DOWNSCAN7)==0; 
  digitalWrite(UPSCAN3, HIGH);
  digitalWrite(UPSCAN4, LOW);
  //__asm__("nop\n nop\n nop\n nop\n");
  delay(1);
  if (digitalRead(DOWNSCAN0)==0) _LASTC='5'; 
  if (digitalRead(DOWNSCAN1)==0) _LASTC='0'; 
  if (digitalRead(DOWNSCAN2)==0) _LASTC='t'; 
  if (digitalRead(DOWNSCAN3)==0) _LASTC='p'; 
  if (digitalRead(DOWNSCAN4)==0) _LASTC='g'; 
  if (digitalRead(DOWNSCAN5)==0) _LASTC=13; 
  if (digitalRead(DOWNSCAN6)==0) _LASTC='v'; 
  if (digitalRead(DOWNSCAN7)==0) _LASTC=' '; 
  digitalWrite(UPSCAN4, HIGH);
  //__asm__("nop\n nop\n nop\n nop\n");
};
//Qui implementare timing di scansione, antirimbalzo e shft. 
char oldchar=0; char countchar=0; 

char c_queue[10]; int q_pos=0; 
void add_char_queue(char c)
{ c_queue[q_pos]=c;
  q_pos++; 
}
char k_getc() 
{ if (q_pos>0) //Ho una coda di caratteri, contiene ad esempio la risposta del VT52 alla richiesta del modello.
  { char c=c_queue[0]; 
    int i; for (i=0; i<q_pos; i++) { c_queue[i]=c_queue[i+1]; };
    q_pos--; 
    delay(100);
    return c; 
  };
  if (k_tick>(last_scan + 8) || k_tick < last_scan)  
  { last_scan=k_tick; 
    k_scan(); 
    char c; 
    c=_LASTC;
    if (c==oldchar) 
    { countchar++; 
      if (countchar==1 || countchar==2) return 0; //Se trovo il tasto per due periodi non lo conto, poi arriva a raffica. 
    } else countchar=0;   
    oldchar=c; 
    if (_SHIFT)
    { if (c>='a' && c<='z') c=c - 'a' + 'A';
      //5,6,7,8 sono left, down, up, right
      if (c=='1') c='!';
      if (c=='2') c='"'; 
      if (c=='3') c='#'; 
      if (c=='4') c='$'; 
      if (c=='5') c=8;
      if (c=='6') c=10;
      if (c=='7') c=11;
      if (c=='8') c=9; 
      if (c=='0') c=127;
    } else if (_ALT)
    { if (c=='1') c='|';
      if (c=='2') c='\\';
      if (c=='3') c='+';
      if (c=='4') c='-';
      if (c=='5') c='%';
      if (c=='6') c='&';
      if (c=='7') c='/';
      if (c=='8') c='(';
      if (c=='9') c=')';
      if (c=='0') c='=';
      if (c=='q') c='\t';
      if (c=='i') c='[';
      if (c=='o') c=']';
      if (c=='p') c='*';
      if (c=='k') c='{';
      if (c=='l') c='}';
      if (c=='z') c='<';
      if (c=='x') c='>';
      if (c=='c') c='@';
      if (c=='v') c=',';
      if (c=='w') c='.';
      if (c=='e') c=';';
      if (c=='r') c=':';
      if (c=='t') c='_';
      if (c=='f') c='`';
    };  
    return c;
  } else return 0;  
}

int _scrpos;
void setScreen(char mode) //Cambia la modalità grafica 0 text, 1 HGA, 2 CGA 
{ _scrpos=0;
  unsigned int i;
  unsigned int max=HSCR * VSCR; if (mode>0) max=HSCR * VSCR*8;
  for (i=0; i<max;i++) screen[i]=0; 
  _vmode=mode; 
};

char _blinked=0; 
void vid_blink()
{ if (_vmode==0) 
  { screen[_scrpos]=screen[_scrpos] ^ 0x80; 
  } else if (_vmode==1)
  { for (int i=0; i<8; i++)
    { unsigned int col=_scrpos%80; 
      unsigned int line=i + (_scrpos-col)/10; // diviso 80*8 
      screen[col + line*80] = screen[col + line*80]^0xFF;  
    };
  } else if (_vmode=2) 
  { for (int i=0; i<8; i++)
    { unsigned int col=2*(_scrpos%40); 
       unsigned int line=i + (_scrpos-_scrpos%80)/10; // diviso 80*8    
      screen[col + line*80] = screen[col + line*80] ^ 0xFF;  
     screen[col + line*80 +1] = screen[col + line*80 +1] ^0xFF; 
    };
  }; 
  _blinked=_blinked^1;
};
unsigned char _color=2; 
unsigned char _background=0; 
void setColor(unsigned char col, unsigned char bg) { _color=col; _background=bg; }; 


/*******************QUI VT52+VT100***********************/
/*******************QUI VT52+VT100***********************/
/*******************QUI VT52+VT100***********************/

unsigned int _saved_scrpos=0;
unsigned char _vt52_state=0;
unsigned char _vt52_y=0;
unsigned char _vt52_wrap=1;
unsigned char _vt52_cursor_enabled=1;
unsigned char _ansi_state=0;
unsigned char _ansi_private=0;
unsigned char _ansi_qmark=0;
unsigned int _ansi_params[4]={0,0,0,0};
unsigned char _ansi_param_count=0;

static inline unsigned char term_cols()
{ return (_vmode==2) ? 40 : 80;
}

static inline unsigned char term_rows()
{ return 25;
}

static inline unsigned int term_cells()
{ return (unsigned int)term_cols() * term_rows();
}

void clear_cell(unsigned int pos)
{ if (_vmode==0)
  { screen[pos]=' ';
  } else if (_vmode==1 || _vmode==2)
  { unsigned int cols=term_cols();
    unsigned int base=(pos/cols) * 8 * 80 + ((_vmode==2) ? (pos%cols)*2 : (pos%cols));
    for (int i=0; i<8; i++)
    { screen[base + i*80] = 0;
      if (_vmode==2) screen[base + i*80 + 1] = 0;
    }
  }
}

void clear_range(unsigned int from, unsigned int to)
{ unsigned int cells=term_cells();
  if (from>=cells) return;
  if (to>=cells) to=cells-1;
  for (unsigned int p=from; p<=to; p++) clear_cell(p);
}

void scroll_up_one()
{ unsigned int cols=term_cols();
  if (_vmode==0)
  { for (unsigned int i=0; i<24*cols; i++) screen[i]=screen[i+cols];
    for (unsigned int i=24*cols; i<25*cols; i++) screen[i]=' ';
  } else
  { for (unsigned int i=0; i<24*8*80; i++) screen[i]=screen[i+8*80];
    for (unsigned int i=24*8*80; i<25*8*80; i++) screen[i]=0;
  }
}

void scroll_down_one()
{ unsigned int cols=term_cols();
  if (_vmode==0)
  { for (int i=25*cols-1; i>=cols; i--) screen[i]=screen[i-cols];
    for (unsigned int i=0; i<cols; i++) screen[i]=' ';
  } else
  { for (int i=25*8*80-1; i>=8*80; i--) screen[i]=screen[i-8*80];
    for (unsigned int i=0; i<8*80; i++) screen[i]=0;
  }
}

void insert_line_at_cursor()
{ unsigned int cols=term_cols();
  unsigned int row=_scrpos/cols;
  if (_vmode==0)
  { for (int i=25*cols-1; i>=(int)((row+1)*cols); i--) screen[i]=screen[i-cols];
    for (unsigned int i=row*cols; i<(row+1)*cols; i++) screen[i]=' ';
  } else
  { unsigned int rowBytes = 8*80;
    unsigned int start = row * rowBytes;
    for (int i=25*rowBytes-1; i>=(int)(start + rowBytes); i--) screen[i]=screen[i-rowBytes];
    for (unsigned int i=start; i<start + rowBytes; i++) screen[i]=0;
  }
  _scrpos = row * cols;
}

void delete_line_at_cursor()
{ unsigned int cols=term_cols();
  unsigned int row=_scrpos/cols;
  if (_vmode==0)
  { for (unsigned int i=row*cols; i<24*cols; i++) screen[i]=screen[i+cols];
    for (unsigned int i=24*cols; i<25*cols; i++) screen[i]=' ';
  } else
  { unsigned int rowBytes = 8*80;
    unsigned int start = row * rowBytes;
    for (unsigned int i=start; i<24*rowBytes; i++) screen[i]=screen[i+rowBytes];
    for (unsigned int i=24*rowBytes; i<25*rowBytes; i++) screen[i]=0;
  }
  _scrpos = row * cols;
}

void cursor_to(unsigned char row, unsigned char col)
{ unsigned char cols=term_cols();
  unsigned char rows=term_rows();
  if (row>=rows) row=rows-1;
  if (col>=cols) col=cols-1;
  _scrpos = (unsigned int)row * cols + col;
}
void k_putc(char c)
{ int i;
  unsigned int scl;
  unsigned char cols=term_cols();
  unsigned char row=(unsigned char)(_scrpos / cols);
  unsigned char col=(unsigned char)(_scrpos % cols);

  if (_blinked) vid_blink(); // tolgo il cursore visibile prima di modificare lo schermo

  // Parser ANSI / VT100 CSI
  if (_ansi_state!=0)
  { if (_ansi_state==1) // dopo ESC [
    { if (c=='?') { _ansi_qmark=1; return; }
      if (c>='0' && c<='9')
      { _ansi_params[0]=(unsigned int)(c-'0');
        _ansi_param_count=1;
        _ansi_state=2;
        return;
      }
      if (c==';')
      { _ansi_param_count=1;
        _ansi_params[0]=0;
        _ansi_state=2;
        return;
      }
      _ansi_param_count=0;
      _ansi_state=2;
      // continua col final byte
    }

    if (_ansi_state==2)
    { if (c>='0' && c<='9')
      { if (_ansi_param_count==0) _ansi_param_count=1;
        if (_ansi_param_count>4) _ansi_param_count=4;
        unsigned char idx=_ansi_param_count-1;
        _ansi_params[idx]=_ansi_params[idx]*10 + (unsigned int)(c-'0');
        return;
      }
      if (c==';')
      { if (_ansi_param_count<4) _ansi_param_count++;
        _ansi_params[_ansi_param_count-1]=0;
        return;
      }

      unsigned int p1 = (_ansi_param_count>=1) ? _ansi_params[0] : 0;
      unsigned int p2 = (_ansi_param_count>=2) ? _ansi_params[1] : 0;
      unsigned int n  = (p1==0) ? 1 : p1;

      switch (c)
      { case 'A': // CUU
          if (n>row) row=0; else row-=n;
          cursor_to(row,col);
          break;
        case 'B': // CUD
          row = (unsigned char)((row + n >= term_rows()) ? (term_rows()-1) : (row+n));
          cursor_to(row,col);
          break;
        case 'C': // CUF
          col = (unsigned char)((col + n >= cols) ? (cols-1) : (col+n));
          cursor_to(row,col);
          break;
        case 'D': // CUB
          if (n>col) col=0; else col-=n;
          cursor_to(row,col);
          break;
        case 'H': // CUP
        case 'f': // HVP
        { unsigned int rr = (p1==0) ? 1 : p1;
          unsigned int cc = (p2==0) ? 1 : p2;
          cursor_to((unsigned char)(rr-1), (unsigned char)(cc-1));
          break;
        }
        case 'G': // CHA
        { unsigned int cc = (p1==0) ? 1 : p1;
          cursor_to(row, (unsigned char)(cc-1));
          break;
        }
        case 'd': // VPA
        { unsigned int rr = (p1==0) ? 1 : p1;
          cursor_to((unsigned char)(rr-1), col);
          break;
        }
        case 'J': // ED
          if (p1==2) { setScreen(_vmode); cursor_to(0,0); }
          else if (p1==1) clear_range(0, _scrpos);
          else clear_range(_scrpos, term_cells()-1);
          break;
        case 'K': // EL
          if (p1==2) clear_range(row*cols, row*cols + cols-1);
          else if (p1==1) clear_range(row*cols, _scrpos);
          else clear_range(_scrpos, row*cols + cols-1);
          break;
        case 's': _saved_scrpos=_scrpos; break;
        case 'u': _scrpos=_saved_scrpos; break;
        case 'h':
          if (_ansi_qmark && p1==25) _vt52_cursor_enabled=1;
          else if (!_ansi_qmark && p1==7) _vt52_wrap=1;
          break;
        case 'l':
          if (_ansi_qmark && p1==25)
          { _vt52_cursor_enabled=0;
            if (_blinked) vid_blink();
          } else if (!_ansi_qmark && p1==7) _vt52_wrap=0;
          break;
        default:
          break;
      }
      _ansi_state=0;
      _ansi_qmark=0;
      for (i=0; i<4; i++) _ansi_params[i]=0;
      _ansi_param_count=0;
      goto done;
    }
  }

  // Parser VT52
  if (_vt52_state!=0)
  { switch (_vt52_state)
    { case 1: // dopo ESC
        switch (c)
        { case '[':
            _ansi_state=1;
            _ansi_qmark=0;
            for (i=0; i<4; i++) _ansi_params[i]=0;
            _ansi_param_count=0;
            _vt52_state=0;
            return;
          case 'A': if (row>0) row--; cursor_to(row,col); break;                 // cursor up
          case 'B': if (row<term_rows()-1) row++; cursor_to(row,col); break;    // cursor down
          case 'C':
            if (col<cols-1) cursor_to(row,col+1);
            else if (_vt52_wrap && row<term_rows()-1) cursor_to(row+1,0);
            break;
          case 'D': if (col>0) cursor_to(row,col-1); break;                     // cursor left
          case 'E': setScreen(_vmode); cursor_to(0,0); break;                   // clear + home
          case 'F': break;                                                      // graphics mode: non implementato
          case 'G': break;                                                      // exit graphics mode
          case 'H': cursor_to(0,0); break;                                      // home
          case 'I':                                                             // reverse line feed
            if (row>0) cursor_to(row-1,col);
            else { scroll_down_one(); cursor_to(0,col); }
            break;
          case 'J': clear_range(_scrpos, term_cells()-1); break;                // erase to end of screen
          case 'K': clear_range(_scrpos, row*cols + (cols-1)); break;           // erase to end of line
          case 'L': insert_line_at_cursor(); break;                             // common extension
          case 'M': delete_line_at_cursor(); break;                             // common extension
          case 'Y': _vt52_state=2; return;                                      // direct cursor address
          case 'Z': /* identify: qui servirebbe inviare ESC / K verso l'host */ break;
          case '=': break;                                                      // alternate keypad mode: ignore
          case '>': break;                                                      // numeric keypad mode: ignore
          case '<': break;                                                      // enter ANSI mode: ignore
          case '7': _saved_scrpos=_scrpos; break;                               // DECSC
          case '8': _scrpos=_saved_scrpos; break;                               // DECRC
          case 'b': _vt52_state=4; return;                                      // Atari/TOS extension: fg color
          case 'c': _vt52_state=5; return;                                      // Atari/TOS extension: bg color
          case 'd': clear_range(0, _scrpos); break;                             // clear to start of screen
          case 'e': _vt52_cursor_enabled=1; break;
          case 'f': _vt52_cursor_enabled=0; if (_blinked) vid_blink(); break;
          case 'j': _saved_scrpos=_scrpos; break;
          case 'k': _scrpos=_saved_scrpos; break;
          case 'l': clear_range(row*cols, row*cols + cols-1); cursor_to(row,0); break;
          case 'o': clear_range(row*cols, _scrpos); break;
          case 'p': setColor(_background, _color); break;                       // reverse video
          case 'q': setColor(2,0); break;                                       // normal video (default locale)
          case 'v': _vt52_wrap=1; break;
          case 'w': _vt52_wrap=0; break;
          default: break;
        }
        _vt52_state=0;
        goto done;
      case 2: // ESC Y row
        _vt52_y = (unsigned char)((c>=32) ? (c-32) : 0);
        _vt52_state=3;
        return;
      case 3: // ESC Y col
        cursor_to(_vt52_y, (unsigned char)((c>=32) ? (c-32) : 0));
        _vt52_state=0;
        goto done;
      case 4: // ESC b <fg>
        setColor((unsigned char)(c & 0x03), _background);
        _vt52_state=0;
        goto done;
      case 5: // ESC c <bg>
        setColor(_color, (unsigned char)(c & 0x03));
        _vt52_state=0;
        goto done;
    }
  }

  // Controlli ASCII / VT52 base / VT100 base
  if (c==27) { _vt52_state=1; return; }                                        // ESC
  if (c==127) { if (_scrpos>0) _scrpos--; clear_cell(_scrpos); goto done; }    // DEL rubout
  if (c==8)   { if (col>0) _scrpos--; goto done; }                              // BS
  if (c==9)   { unsigned char next=(unsigned char)((col+8) & ~7);               // HT
                if (next>=cols) next=cols-1;
                cursor_to(row,next); goto done; }
  if (c==10)  { if (row<term_rows()-1) _scrpos += cols;                         // LF
                else scroll_up_one(); cursor_to(row+1,0);
                goto done; }
  if (c==11)  { if (row>0) _scrpos -= cols; goto done; }                        // VT
  if (c==12)  { setScreen(_vmode); cursor_to(0,0); goto done; }                 // FF
  if (c=='\n'){ cursor_to(row,0); goto done; }                                  // CR
  if (c==7)   { beep(400,300); delayMicroseconds(100000); goto done; }          // BEL
  if ((unsigned char)c < 32) goto done;                                         // altri controlli ignorati

  // carattere stampabile
  if (_vmode==0)
  { screen[_scrpos]=c;
  } else if (_vmode==1)
  { unsigned int fi=( (c & 0x7F) << 3);
    for (i=0; i<8; i++)
    { scl=font8x8[fi + i];
      if ((c & 0x80)) scl=scl ^ 0xFF;
      if (_color==0 && _background==1) scl=scl ^ 0xFF;
      if (_color==0 && _background==0) scl=0;
      if (_color==1 && _background==1) scl=0xFF;
      unsigned int col80=_scrpos%80;
      unsigned int line=i + (_scrpos-col80)/10; // row*8
      screen[col80 + line*80] = (unsigned char)scl;
    };
  } else if (_vmode==2)
  { unsigned int fi=( (c & 0x7F) << 3);
    unsigned char ca, cb, bg;
    bg= _background | (_background<<2) | (_background<<4) | (_background<<6);
    for (i=0; i<8; i++)
    { scl=font8x8[fi + i];
      ca=bg; cb=bg;
      if (scl&0x80) ca=ca & 0x3F | _color<<6;
      if (scl&0x40) ca=ca & 0xCF | _color<<4;
      if (scl&0x20) ca=ca & 0xF3 | _color<<2;
      if (scl&0x10) ca=ca & 0xFC | _color;
      if (scl&0x08) cb=cb & 0x3F | _color<<6;
      if (scl&0x04) cb=cb & 0xCF | _color<<4;
      if (scl&0x02) cb=cb & 0xF3 | _color<<2;
      if (scl&0x01) cb=cb & 0xFC | _color;
      unsigned int colpix=2*(_scrpos%40);
      unsigned int row40=_scrpos/40;
      unsigned int line=i + row40*8;
      screen[colpix + line*80] = ca;
      screen[colpix + line*80 +1] = cb;
    };
  }

  if (col<cols-1) _scrpos++;
  else if (_vt52_wrap)
  { if (row<term_rows()-1) cursor_to(row+1,0);
    else { scroll_up_one(); cursor_to(term_rows()-1,0); }
  }

done:
  if (_scrpos>=term_cells()) _scrpos=term_cells()-1;
  if (_vt52_cursor_enabled) vid_blink();
}

/*******************QUI VT52+VT100***********************/
/*******************QUI VT52+VT100***********************/
/*******************QUI VT52+VT100***********************/


/*********************SEZIONE VT52************************/
/*********************SEZIONE VT52************************/
/*********************SEZIONE VT52************************/
/*
unsigned int _saved_scrpos=0;
unsigned char _vt52_state=0;
unsigned char _vt52_y=0;
unsigned char _vt52_wrap=1;
unsigned char _vt52_cursor_enabled=1;

static inline unsigned char term_cols()
{ return (_vmode==2) ? 40 : 80;
}

static inline unsigned char term_rows()
{ return 25;
}

static inline unsigned int term_cells()
{ return (unsigned int)term_cols() * term_rows();
}

void clear_cell(unsigned int pos)
{ if (_vmode==0)
  { screen[pos]=' ';
  } else if (_vmode==1 || _vmode==2)
  { unsigned int cols=term_cols();
    unsigned int base=(pos/cols) * 8 * 80 + ((_vmode==2) ? (pos%cols)*2 : (pos%cols));
    for (int i=0; i<8; i++)
    { screen[base + i*80] = 0;
      if (_vmode==2) screen[base + i*80 + 1] = 0;
    }
  }
}

void clear_range(unsigned int from, unsigned int to)
{ unsigned int cells=term_cells();
  if (from>=cells) return;
  if (to>=cells) to=cells-1;
  for (unsigned int p=from; p<=to; p++) clear_cell(p);
}

void scroll_up_one()
{ unsigned int cols=term_cols();
  if (_vmode==0)
  { for (unsigned int i=0; i<24*cols; i++) screen[i]=screen[i+cols];
    for (unsigned int i=24*cols; i<25*cols; i++) screen[i]=' ';
  } else
  { for (unsigned int i=0; i<24*8*80; i++) screen[i]=screen[i+8*80];
    for (unsigned int i=24*8*80; i<25*8*80; i++) screen[i]=0;
  }
}

void scroll_down_one()
{ unsigned int cols=term_cols();
  if (_vmode==0)
  { for (int i=25*cols-1; i>=cols; i--) screen[i]=screen[i-cols];
    for (unsigned int i=0; i<cols; i++) screen[i]=' ';
  } else
  { for (int i=25*8*80-1; i>=8*80; i--) screen[i]=screen[i-8*80];
    for (unsigned int i=0; i<8*80; i++) screen[i]=0;
  }
}

void insert_line_at_cursor()
{ unsigned int cols=term_cols();
  unsigned int row=_scrpos/cols;
  if (_vmode==0)
  { for (int i=25*cols-1; i>=(int)((row+1)*cols); i--) screen[i]=screen[i-cols];
    for (unsigned int i=row*cols; i<(row+1)*cols; i++) screen[i]=' ';
  } else
  { unsigned int rowBytes = 8*80;
    unsigned int start = row * rowBytes;
    for (int i=25*rowBytes-1; i>=(int)(start + rowBytes); i--) screen[i]=screen[i-rowBytes];
    for (unsigned int i=start; i<start + rowBytes; i++) screen[i]=0;
  }
  _scrpos = row * cols;
}

void delete_line_at_cursor()
{ unsigned int cols=term_cols();
  unsigned int row=_scrpos/cols;
  if (_vmode==0)
  { for (unsigned int i=row*cols; i<24*cols; i++) screen[i]=screen[i+cols];
    for (unsigned int i=24*cols; i<25*cols; i++) screen[i]=' ';
  } else
  { unsigned int rowBytes = 8*80;
    unsigned int start = row * rowBytes;
    for (unsigned int i=start; i<24*rowBytes; i++) screen[i]=screen[i+rowBytes];
    for (unsigned int i=24*rowBytes; i<25*rowBytes; i++) screen[i]=0;
  }
  _scrpos = row * cols;
}

void cursor_to(unsigned char row, unsigned char col)
{ unsigned char cols=term_cols();
  unsigned char rows=term_rows();
  if (row>=rows) row=rows-1;
  if (col>=cols) col=cols-1;
  _scrpos = (unsigned int)row * cols + col;
}
void k_putc(char c)
{ int i;
  unsigned int scl;
  unsigned char cols=term_cols();
  unsigned char row=(unsigned char)(_scrpos / cols);
  unsigned char col=(unsigned char)(_scrpos % cols);

  if (_blinked) vid_blink(); // tolgo il cursore visibile prima di modificare lo schermo

//Comunicazioni bidirezionali: 
//  ESC Z (0x1B 0x5A)  -> Chi sei ? Rispondi con ESC / Z -> 0x1B 0x2F 0x5A che significa sono un VT52.
//  ESC [ 6 n -> Posizione cursore: rispondi con esc [ riga ; colonna R 

  // Parser VT52
  if (_vt52_state!=0)
  { switch (_vt52_state)
    { case 1: // dopo ESC
        switch (c)
        { case 'A': if (row>0) row--; cursor_to(row,col); break;                // cursor up
          case 'B': if (row<term_rows()-1) row++; cursor_to(row,col); break;    // cursor down
          case 'C':
            if (col<cols-1) cursor_to(row,col+1);
            else if (_vt52_wrap && row<term_rows()-1) cursor_to(row+1,0);
            break;
          case 'D': if (col>0) cursor_to(row,col-1); break;                     // cursor left
          case 'E': setScreen(_vmode); cursor_to(0,0); break;                   // clear + home
          case 'F': break;                                                      // graphics mode: non implementato
          case 'G': break;                                                      // exit graphics mode
          case 'H': cursor_to(0,0); break;                                      // home
          case 'I':                                                             // reverse line feed
            if (row>0) cursor_to(row-1,col);
            else { scroll_down_one(); cursor_to(0,col); }
            break;
          case 'J': clear_range(_scrpos, term_cells()-1); break;                // erase to end of screen
          case 'K': clear_range(_scrpos, row*cols + (cols-1)); break;           // erase to end of line
          case 'L': insert_line_at_cursor(); break;                             // common extension
          case 'M': delete_line_at_cursor(); break;                             // common extension
          case 'Y': _vt52_state=2; return;                                      // direct cursor address
          case '=': break;                                                      // alternate keypad mode: ignore
          case '>': break;                                                      // numeric keypad mode: ignore
          case '<': break;                                                      // enter ANSI mode: ignore
          case 'b': _vt52_state=4; return;                                      // Atari/TOS extension: fg color
          case 'c': _vt52_state=5; return;                                      // Atari/TOS extension: bg color
          case 'd': clear_range(0, _scrpos); break;                             // clear to start of screen
          case 'e': _vt52_cursor_enabled=1; break;
          case 'f': _vt52_cursor_enabled=0; if (_blinked) vid_blink(); break;
          case 'j': _saved_scrpos=_scrpos; break;
          case 'k': _scrpos=_saved_scrpos; break;
          case 'l': clear_range(row*cols, row*cols + cols-1); cursor_to(row,0); break;
          case 'o': clear_range(row*cols, _scrpos); break;
          case 'p': setColor(_background, _color); break;                       // reverse video
          case 'q': setColor(2,0); break;                                       // normal video (default locale)
          case 'v': _vt52_wrap=1; break;
          case 'w': _vt52_wrap=0; break;
          //case 'Z': identify: qui servirebbe inviare ESC / K verso l'host break; 
          case 'Z': delay(100); add_char_queue(0x1B); add_char_queue(0x2F);add_char_queue(0x5A); break;  
          default: break;
        }
        _vt52_state=0;
        goto done;
      case 2: // ESC Y row
        _vt52_y = (unsigned char)((c>=32) ? (c-32) : 0);
        _vt52_state=3;
        return;
      case 3: // ESC Y col
        cursor_to(_vt52_y, (unsigned char)((c>=32) ? (c-32) : 0));
        _vt52_state=0;
        goto done;
      case 4: // ESC b <fg>
        setColor((unsigned char)(c & 0x03), _background);
        _vt52_state=0;
        goto done;
      case 5: // ESC c <bg>
        setColor(_color, (unsigned char)(c & 0x03));
        _vt52_state=0;
        goto done;
    }
  }
  // Controlli ASCII / VT52 base
  if (c==27) { _vt52_state=1; return; }                                        // ESC
  if (c==127) { if (_scrpos>0) _scrpos--; clear_cell(_scrpos); goto done; }    // DEL rubout
  if (c==8)   { if (col>0) _scrpos--; goto done; }                              // BS
  if (c==9)   { unsigned char next=(unsigned char)((col+8) & ~7);               // HT
                if (next>=cols) next=cols-1;
                cursor_to(row,next); goto done; }
  if (c==10)  { if (row<term_rows()-1) _scrpos += cols;                         // LF
                else scroll_up_one(); cursor_to(row+1,0);
                goto done; }
  if (c==11)  { if (row>0) _scrpos -= cols; goto done; }                        // VT
  if (c==12)  { setScreen(_vmode); cursor_to(0,0); goto done; }                 // FF
  if (c=='\n'){ cursor_to(row,0); goto done; }                                  // CR
  if (c==7)   { beep(400,300); delayMicroseconds(100000); goto done; }          // BEL
  if ((unsigned char)c < 32) goto done;                                         // altri controlli ignorati

  // carattere stampabile
  if (_vmode==0)
  { screen[_scrpos]=c;
  } else if (_vmode==1)
  { unsigned int fi=( (c & 0x7F) << 3);
    for (i=0; i<8; i++)
    { scl=font8x8[fi + i];
      if ((c & 0x80)) scl=scl ^ 0xFF;
      if (_color==0 && _background==1) scl=scl ^ 0xFF;
      if (_color==0 && _background==0) scl=0;
      if (_color==1 && _background==1) scl=0xFF;
      unsigned int col80=_scrpos%80;
      unsigned int line=i + (_scrpos-col80)/10; // row*8
      screen[col80 + line*80] = (unsigned char)scl;
    };
  } else if (_vmode==2)
  { unsigned int fi=( (c & 0x7F) << 3);
    unsigned char ca, cb, bg;
    bg= _background | (_background<<2) | (_background<<4) | (_background<<6);
    for (i=0; i<8; i++)
    { scl=font8x8[fi + i];
      ca=bg; cb=bg;
      if (scl&0x80) ca=ca & 0x3F | _color<<6;
      if (scl&0x40) ca=ca & 0xCF | _color<<4;
      if (scl&0x20) ca=ca & 0xF3 | _color<<2;
      if (scl&0x10) ca=ca & 0xFC | _color;
      if (scl&0x08) cb=cb & 0x3F | _color<<6;
      if (scl&0x04) cb=cb & 0xCF | _color<<4;
      if (scl&0x02) cb=cb & 0xF3 | _color<<2;
      if (scl&0x01) cb=cb & 0xFC | _color;
      unsigned int colpix=2*(_scrpos%40);
      unsigned int row40=_scrpos/40;
      unsigned int line=i + row40*8;
      screen[colpix + line*80] = ca;
      screen[colpix + line*80 +1] = cb;
    };
  }

  if (col<cols-1) _scrpos++;
  else if (_vt52_wrap)
  { if (row<term_rows()-1) cursor_to(row+1,0);
    else { scroll_up_one(); cursor_to(term_rows()-1,0); }
  }
done:
  if (_scrpos>=term_cells()) _scrpos=term_cells()-1;
  if (_vt52_cursor_enabled) vid_blink();
}
*/
/*********************SEZIONE VT52************************/
/*********************SEZIONE VT52************************/
/*********************SEZIONE VT52************************/

/*
void k_putc(char c)
{ int i; int del=0; 
  unsigned int scl;
  if (_blinked) vid_blink(); //Se c'e' evidenziazione torno al cursore standard.  
  char maxs=80; if (_vmode==2) maxs=40;
  //Qui emulazione ADM-£A
if (c==127)
  { if (_scrpos>0) _scrpos--;
    c=' '; del=1; 
  } else if (c=='\r')
  { _scrpos=_scrpos - _scrpos % maxs;
  } else if (c==7)
  { beep(400,300);     delayMicroseconds(100000);
  } else if (c==8)
  { _scrpos++; 
  } else if (c==9)
  { if (_scrpos>0) _scrpos--; 
  } else if (c==11)
  { if (_scrpos>maxs) _scrpos=_scrpos-maxs;  
  } else if (c=='\n')
  { _scrpos=_scrpos + maxs;   
  } else if (c==12) //Clear screen 
  { setScreen(_vmode);
  } else 

  //qui inserire emulazione VT52 

 
  { if (_vmode==0) 
    { screen[_scrpos]=c; //Testuale, 
    } else if (_vmode==1)
    { unsigned int fi=( (c & 0x7F) << 3); 
      for (i=0; i<8; i++)
      { scl=font8x8[fi + i];
        if ((c & 0x80)) scl=scl ^ 0xFF;
        if (_color==0 && _background==1) scl=scl ^ 0xFF;
        if (_color==0 && _background==0) scl=0;
        if (_color==1 && _background==1) scl=0xFF;
        unsigned int col=_scrpos%80; 
        unsigned int line=i + (_scrpos-col)/10; // diviso 80*8 
        screen[col + line*80] = (unsigned char)scl;  
      }; 
    } else if (_vmode==2)
    { unsigned int fi=( (c & 0x7F) << 3); 
      unsigned char ca, cb, bg;
      bg= _background | (_background<<2) | (_background<<4) | (_background<<6);
      for (i=0; i<8; i++)
      { scl=font8x8[fi + i]; //Devo suddividerli in due parti e associarli ai colori. 
        ca=bg; cb=bg; 
        if (scl&0x80) ca=ca & 0x3F | _color<<6;
        if (scl&0x40) ca=ca & 0xCF | _color<<4;
        if (scl&0x20) ca=ca & 0xF3 | _color<<2;
        if (scl&0x10) ca=ca & 0xFC | _color;
        if (scl&0x08) cb=cb & 0x3F | _color<<6;
        if (scl&0x04) cb=cb & 0xCF | _color<<4;
        if (scl&0x02) cb=cb & 0xF3 | _color<<2;
        if (scl&0x01) cb=cb & 0xFC | _color;     
        unsigned int col=2*(_scrpos%40); 
        unsigned int line=i + (_scrpos-_scrpos%80)/10; // diviso 80*8    
        screen[col + line*80] = ca;  
        screen[col + line*80 +1] = cb;  
      }; 
    };
    if (del==0) _scrpos++;  
  };
  //Scrolling. Diverso tra testo e schermi video.  
  if (_scrpos>=25*maxs)
  { if (_vmode==0)
    { for (i=0; i<24*maxs; i++) screen[i]=screen[i+maxs];
    } if (_vmode==1 || _vmode==2)
    { for (i=0; i<24*8*maxs; i++) screen[i]=screen[i+8*maxs]; //Ogni riga 8*80 caratteri..
    }
    for (i=24*8*maxs; i<25*8*maxs; i++) screen[i]=0;
    _scrpos=_scrpos - maxs;
  };
}
*/

void beep(int freq, int millisec)
{ int delay=1000000/(2*freq); //Ritardo in microdescondi 
  int length=(millisec*1000)/delay; 
  while (length>0)
  { digitalWrite(Audio, HIGH);
    delayMicroseconds(delay);
    digitalWrite(Audio, LOW);
    delayMicroseconds(delay);
    length--; 
  }
}
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/

void setup(void) 
{ _scrpos=0; _blinked=0;
#if defined(TIM1)
  TIM_TypeDef *Instance = TIM1;
#else
  TIM_TypeDef *Instance = TIM2;
#endif

  // Instantiate HardwareTimer object. Thanks to 'new' instanciation, HardwareTimer is not destructed when setup() function is finished.
  MyTim = new HardwareTimer(Instance);

  // configure pin in output mode
  pinMode(VideoA, OUTPUT);
  pinMode(VideoB, OUTPUT);
  pinMode(VideoC, OUTPUT);
  pinMode(Audio, OUTPUT);
  pinMode(PC13, OUTPUT);

/*
  MyTim->setMode(2, TIMER_OUTPUT_COMPARE);  // In our case, channekFalling is configured but not really used. Nevertheless it would be possible to attach a callback to channel compare match.
  MyTim->setOverflow(15625, HERTZ_FORMAT); // 15650hz 
  MyTim->attachInterrupt(LineHandler2);
  MyTim->resume();
*/
//Questa la proposta di chatty per uno schermo stabile.. sarà vero ? 
//NOTA: dice di togliere anche il nointerrupt
  uint32_t freq = MyTim->getTimerClkFreq();
  LINE_TICKS=freq * 0.000064; // = 5376
  MyTim->setOverflow(0xFFFFFFFF); // free running
  t_next = MyTim->getCount() + LINE_TICKS;
  MyTim->setCaptureCompare(2, t_next, TICK_COMPARE_FORMAT);
  MyTim->attachInterrupt(2, LineHandler2);
  MyTim->resume();

  
  k_init();  
  beep(1000,200);
  delay(500);
  setScreen(1);

#ifdef DEBUGLOG
  _sys_deletefile((uint8 *)LogName);
#endif
  //_clrscr();
  _puts("CP/M 2.2 Emulator v" VERSION " by Marcelo Dantas\r\n");
  _puts("Arduino read/write support by Krzysztof Klis\r\n");
  _puts("      Built " __DATE__ " - " __TIME__ "\r\n");
  _puts("--------------------------------------------\r\n");
  _puts("CPU is ");
  _puts(CPU_IS);
  _puts("\r\n");
  Z80estimateClock();
	_puts("BIOS at 0x");
	_puthex16(BIOSjmppage);
	_puts(" - ");
	_puts("BDOS at 0x");
	_puthex16(BDOSjmppage);
	_puts("\r\n");
	_puts("CCP " CCPname " at 0x");
	_puthex16(CCPaddr);
	_puts("\r\n");

  _puts("Initializing SPI.\r\n");
  pinMode(SPISEL, OUTPUT);
  SPI.setMISO(SPIMISO); //Avvio SPI e SD  
  SPI.setMOSI(SPIMOSI);
  SPI.setSCLK(SPICLK);
  SPI.setSSEL(SPISEL);
  SPI.begin() ;
  _puts("Initializing SD card.\r\n");
  if (SD.begin(SPISEL)) {
    if (VersionCCP >= 0x10 || SD.exists(CCPname)) {
#ifdef ABDOS
      _PatchBIOS();
#endif
      while (true) {
        _puts(CCPHEAD);
        _PatchCPM();
	Status = STATUS_RUNNING;
#ifdef CCP_INTERNAL
        _ccp();
#else
        if (!_RamLoad((uint8 *)CCPname, CCPaddr ,0)) {
          _puts("Unable to load the CCP.\r\nCPU halted.\r\n"); k_putc(7); k_putc(7); k_putc(7); 
          break;
        }
     		// Loads an autoexec file if it exists and this is the first boot
		    // The file contents are loaded at ccpAddr+8 up to 126 bytes then the size loaded is stored at ccpAddr+7
		    if (firstBoot) {
			    if (_sys_exists((uint8*)AUTOEXEC)) {
				    uint16 cmd = CCPaddr + 8;
				    uint8 bytesread = (uint8)_RamLoad((uint8*)AUTOEXEC, cmd, 125);
				    uint8 blen = 0;
				    while (blen < bytesread && _RamRead(cmd + blen) > 31)
				    	blen++;
				    _RamWrite(cmd + blen, 0x00);
				    _RamWrite(--cmd, blen);
			    }
			    if (BOOTONLY)
				    firstBoot = FALSE;
		    }
        Z80reset();
        SET_LOW_REGISTER(BC, _RamRead(DSKByte));
        PC = CCPaddr;
        Z80run(cpuDelayInstructions);
#endif
        if (Status == STATUS_EXIT)
#ifdef DEBUG
	#ifdef DEBUGONHALT
    			Debug = 1;
		    	Z80debug();
	#endif
#endif
          break;
#ifdef USE_PUN
        if (pun_dev)
          _sys_fflush(pun_dev);
#endif
#ifdef USE_LST
        if (lst_dev)
          _sys_fflush(lst_dev);
#endif
      }
    } else {
      _puts("Unable to load CP/M CCP.\r\nCPU halted.\r\n"); k_putc(7); 
    }
  } else {
    _puts("Unable to initialize SD card.\r\nCPU halted.\r\n"); k_putc(7);  k_putc(7); 
  }
}

void loop(void) { 
   for (;;) delay(10000);
/*  digitalWrite(LED, HIGH^LEDinv);
  delay(DELAY);
  digitalWrite(LED, LOW^LEDinv);
  delay(DELAY);
  digitalWrite(LED, HIGH^LEDinv);
  delay(DELAY);
  digitalWrite(LED, LOW^LEDinv);
  delay(DELAY * 4);
*/
}
