/*
 * pyon — home screen
 * Layout: topbar + lista centralizada em retângulo enfeitado
 * Cada board: slug colorido · título · tags · desc no rodapé
 */
#include "home.h"
#include "ui.h"
#include "boards.h"
#include "config.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>

static int cat_attr(BoardCat c) {
    switch(c) {
        case CAT_PADRAO:  return COLOR_PAIR(C_WARN)  |A_BOLD;
        case CAT_ANIME:   return COLOR_PAIR(C_BOARD) |A_BOLD;
        case CAT_UNI:     return COLOR_PAIR(C_UNI)   |A_BOLD;
        case CAT_CULTURA: return COLOR_PAIR(C_CULT)  |A_BOLD;
        case CAT_DIRETO:  return COLOR_PAIR(C_DIRETO)|A_BOLD;
        default:          return COLOR_PAIR(C_NORMAL);
    }
}

static int flt[NUM_BOARDS];
static int flt_n = 0;

static void board_filter(const char *q) {
    flt_n = 0;
    for (int i=0;i<NUM_BOARDS;i++) {
        if (!q[0]) { flt[flt_n++]=i; continue; }
        char h[256], ql[64];
        snprintf(h,sizeof(h),"%s %s %s %s %s %s",
            BOARDS[i].slug,BOARDS[i].title,BOARDS[i].desc,
            BOARDS[i].tags[0],BOARDS[i].tags[1],BOARDS[i].tags[2]);
        int qi=0,hi=0;
        for(const char*p=q;*p&&qi<63;p++)  ql[qi++]=tolower((unsigned char)*p); ql[qi]=0;
        for(const char*p=h;*p&&hi<255;p++) h[hi++]=tolower((unsigned char)*p);  h[hi]=0;
        if(strstr(h,ql)) flt[flt_n++]=i;
    }
}

#define BOX_W 64
static int box_w(int cols) {
    int w=cols-8; if(w>BOX_W)w=BOX_W; if(w<40)w=40; if(w>cols-2)w=cols-2; return w;
}
static int box_x(int cols,int w){ return (cols-w)/2; }

static void draw_box_border(int y0,int x0,int w,int h,const char*title){
    attron(COLOR_PAIR(C_DIM));
    mvaddch(y0,x0,ACS_ULCORNER); mvhline(y0,x0+1,ACS_HLINE,w-2); mvaddch(y0,x0+w-1,ACS_URCORNER);
    for(int r=1;r<h-1;r++){mvaddch(y0+r,x0,ACS_VLINE);mvaddch(y0+r,x0+w-1,ACS_VLINE);}
    mvaddch(y0+h-1,x0,ACS_LLCORNER); mvhline(y0+h-1,x0+1,ACS_HLINE,w-2); mvaddch(y0+h-1,x0+w-1,ACS_LRCORNER);
    attroff(COLOR_PAIR(C_DIM));
    if(title&&title[0]){
        int tl=(int)strlen(title),tx=x0+(w-tl-4)/2;
        attron(COLOR_PAIR(C_TITLE)|A_BOLD); mvprintw(y0,tx,"[ %s ]",title); attroff(COLOR_PAIR(C_TITLE)|A_BOLD);
    }
}

static void draw_cat_sep(int y,int x0,int w,const char*label,int attr){
    int ll=(int)strlen(label)+2,left=(w-ll-2)/2,right=w-ll-left-2;
    attron(COLOR_PAIR(C_DIM)); mvhline(y,x0+1,ACS_HLINE,left); attroff(COLOR_PAIR(C_DIM));
    attron(attr); mvprintw(y,x0+1+left," %s ",label); attroff(attr);
    attron(COLOR_PAIR(C_DIM)); mvhline(y,x0+1+left+ll,ACS_HLINE,right); attroff(COLOR_PAIR(C_DIM));
}

/* calcula linha lógica de cada fi — usado para scroll */
static int fi_to_line[NUM_BOARDS];
static int total_lines;

static void calc_lines(void){
    int cur=0; BoardCat cc=(BoardCat)-1;
    total_lines=0;
    for(int fi=0;fi<flt_n;fi++){
        int i=flt[fi];
        if(BOARDS[i].cat!=cc){
            if(cc!=(BoardCat)-1){cur++;} /* espaço */
            cc=BOARDS[i].cat; cur++;     /* separador */
        }
        fi_to_line[fi]=cur;
        cur+=2;
    }
    total_lines=cur;
}

static void draw_boards(int y0,int x0,int w,int h,int sel_fi,int scroll_line){
    int scr_row=y0+1, max_row=y0+h-1;
    BoardCat cc=(BoardCat)-1; int cur=0;

    for(int fi=0;fi<flt_n;fi++){
        int i=flt[fi];
        const Board*b=&BOARDS[i];

        if(b->cat!=cc){
            if(cc!=(BoardCat)-1){
                cur++;
                if(cur>scroll_line&&scr_row<max_row) scr_row++;
            }
            cc=b->cat;
            if(cur>=scroll_line&&scr_row<max_row){
                draw_cat_sep(scr_row,x0,w,cat_label(cc),cat_attr(cc));
                scr_row++;
            }
            cur++;
        }

        int is_sel=(fi==sel_fi);

        /* linha 1: num + slug + título */
        if(cur>=scroll_line&&scr_row<max_row){
            if(is_sel){
                attron(COLOR_PAIR(C_HEADER)|A_BOLD);
                mvhline(scr_row,x0+1,' ',w-2);
                attroff(COLOR_PAIR(C_HEADER)|A_BOLD);
            }
            /* num */
            attron(is_sel?(COLOR_PAIR(C_HEADER)|A_BOLD):(COLOR_PAIR(C_DIM)|A_BOLD));
            mvprintw(scr_row,x0+2,"%2d",b->num);
            attrset(A_NORMAL);
            /* slug */
            if (is_sel) attron(COLOR_PAIR(C_HEADER)|A_BOLD);
            else        attron(cat_attr(b->cat));
            mvprintw(scr_row,x0+5,"  /%-7s",b->slug);
            attrset(A_NORMAL);
            /* título — C_HEADER dá preto-sobre-ciano = legível quando sel */
            int tw=w-18;
            attron(is_sel?(COLOR_PAIR(C_HEADER)|A_BOLD):COLOR_PAIR(C_NORMAL));
            mvprintw(scr_row,x0+15,"%-*.*s",tw>0?tw:0,tw>0?tw:0,b->title);
            attrset(A_NORMAL);
            scr_row++;
        }
        cur++;

        /* linha 2: tags */
        if(cur>=scroll_line&&scr_row<max_row){
            mvprintw(scr_row,x0+15,"");
            for(int t=0;t<3;t++){
                if(!b->tags[t][0]) break;
                attron(is_sel?COLOR_PAIR(C_HEADER):(COLOR_PAIR(C_DIM)|A_BOLD));
                printw(" #%s",b->tags[t]);
                attrset(A_NORMAL);
            }
            scr_row++;
        }
        cur++;
    }
}

int home_run(Config *cfg) {
    board_filter("");
    keypad(stdscr,TRUE);

    int sel=0, num=0, nlen=0, scroll_line=0, searching=0;
    char srch[64]={0};

    static const char*greets[]={"(◕‿◕✿)","(ﾉ◕ヮ◕)ﾉ","(˶ᵔ ᵕ ᵔ˶)","♡(˘▽˘>ԅ( ˘⌣˘)","(｡◕‿◕｡)"};
    int gi=(int)(time(NULL)%5);

    while(1){
        int rows,cols;
        getmaxyx(stdscr,rows,cols);
        clear();
        calc_lines();

        int w=box_w(cols),x0=box_x(cols,w);
        int h=rows-4; if(h<4)h=4;
        int y0=2;

        /* scroll para manter sel visível */
        if(flt_n>0&&sel<flt_n){
            int sl=fi_to_line[sel];
            int vis=h-2;
            if(sl<scroll_line) scroll_line=sl;
            if(sl+2>scroll_line+vis) scroll_line=sl+2-vis;
            if(scroll_line<0) scroll_line=0;
        }

        /* topbar */
        char tl[80];
        snprintf(tl,sizeof(tl),"✦ PYON  %s  %s",greets[gi],cfg->my_name);
        ui_topbar(cols,tl,"v0.2-alpha");

        /* hint */
        if(searching){
            attron(COLOR_PAIR(C_WARN)|A_BOLD);
            mvhline(1,0,' ',cols);
            mvprintw(1,x0,"/ %s_  [%d boards]",srch,flt_n);
            attroff(COLOR_PAIR(C_WARN)|A_BOLD);
        } else {
            attron(COLOR_PAIR(C_DIM));
            mvprintw(1,x0,"[↑↓] nav  [Enter] abrir  [/] buscar  [s] nome  [r] relay  [q] sair");
            attroff(COLOR_PAIR(C_DIM));
        }

        /* box */
        char btitle[32];
        if(searching&&srch[0]) snprintf(btitle,sizeof(btitle),"busca: %s",srch);
        else                   snprintf(btitle,sizeof(btitle),"boards");
        draw_box_border(y0,x0,w,h,btitle);
        draw_boards(y0,x0,w,h,sel,scroll_line);

        /* rodapé */
        if(flt_n>0&&sel<flt_n){
            const Board*b=&BOARDS[flt[sel]];
            attron(COLOR_PAIR(C_SYS)|A_BOLD);
            mvhline(rows-2,0,' ',cols);
            mvprintw(rows-2,x0,"/%s/ — %s",b->slug,b->desc);
            attroff(COLOR_PAIR(C_SYS)|A_BOLD);
        }
        if(nlen>0){
            attron(COLOR_PAIR(C_WARN)|A_BOLD);
            mvprintw(rows-1,x0,"→ board %d",num);
            attroff(COLOR_PAIR(C_WARN)|A_BOLD);
        }
        refresh();

        int ch=ui_key();

        if(searching){
            if(ch==27||ch=='\n'||ch==KEY_ENTER){
                if(ch==27){srch[0]=0;board_filter("");}
                searching=0;sel=0;scroll_line=0;
            } else if(ch==KEY_BACKSPACE||ch==127){
                int l=(int)strlen(srch);if(l>0)srch[l-1]=0;
                board_filter(srch);sel=0;scroll_line=0;
            } else if(ch>=32&&ch<127){
                int l=(int)strlen(srch);if(l<63){srch[l]=ch;srch[l+1]=0;}
                board_filter(srch);sel=0;scroll_line=0;
            }
            continue;
        }

        if(ch>='1'&&ch<='9'){num=num*10+(ch-'0');nlen++;if(num>NUM_BOARDS){num=0;nlen=0;}continue;}
        if(ch=='0'&&nlen>0) {num=num*10;           nlen++;if(num>NUM_BOARDS){num=0;nlen=0;}continue;}

        switch(ch){
            case KEY_UP:case'k': num=0;nlen=0;sel=(sel>0)?sel-1:flt_n-1; break;
            case KEY_DOWN:case'j': num=0;nlen=0;sel=(sel<flt_n-1)?sel+1:0; break;
            case KEY_PPAGE: sel=(sel>5)?sel-5:0; break;
            case KEY_NPAGE: sel=(sel<flt_n-6)?sel+5:flt_n-1; break;
            case'\n':case KEY_ENTER:
                if(nlen>0&&num>=1&&num<=NUM_BOARDS){
                    for(int fi=0;fi<flt_n;fi++)if(BOARDS[flt[fi]].num==num){sel=fi;break;}
                    num=0;nlen=0;
                }
                if(flt_n>0)return flt[sel];
                break;
            case'/':searching=1;break;
            case's':{
                char nn[64]; snprintf(nn,sizeof(nn),"%s",cfg->my_name);
                if(ui_modal("novo nome: ",nn,sizeof(nn))&&nn[0]){
                    snprintf(cfg->my_name,sizeof(cfg->my_name),"%s",nn);
                    char cmd[192];
                    snprintf(cmd,sizeof(cmd),"pyon-core set-name '%s' 2>/dev/null || txtboard-core set-name '%s' 2>/dev/null",nn,nn);
                    system(cmd); gi=(int)(time(NULL)%5);
                }
                break;
            }
            case'r':return -2;
            case 27:num=0;nlen=0;break;
            case'q':case'Q':return -1;
        }
    }
}
