/*
 * pyon — profile.c
 * Página de perfil: nome, bio (2500 chars), sorte do dia, avaliação 1-5.
 * Próprio perfil: editável. Perfil alheio: somente leitura + pode avaliar.
 */
#include "profile.h"
#include "ui.h"
#include "relay.h"
#include "config.h"
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

/* ── JSON minimalista (sem dependência externa) ─────────────────── */
static void jget(const char *json, const char *key, char *out, int n) {
    char needle[64]; snprintf(needle,sizeof(needle),"\"%s\":",key);
    const char *p = strstr(json,needle); if(!p){out[0]=0;return;}
    p += strlen(needle); while(*p==' ')p++;
    if(strncmp(p,"null",4)==0){out[0]=0;return;}
    if(*p=='"'){
        p++;int i=0;
        while(*p&&*p!='"'&&i<n-1){
            if(*p=='\\'&&*(p+1)=='n'){out[i++]='\n';p+=2;continue;}
            if(*p=='\\'&&*(p+1)=='"'){out[i++]='"';p+=2;continue;}
            if(*p=='\\'&&*(p+1)=='\\'){out[i++]='\\';p+=2;continue;}
            out[i++]=*p++;
        }out[i]=0;
    } else {
        int i=0; while(*p&&*p!=','&&*p!='}'&&i<n-1)out[i++]=*p++; out[i]=0;
    }
}

static void json_escape(const char *src, char *dst, int dsz) {
    int i=0;
    for(;*src&&i<dsz-2;src++){
        if(*src=='"')  { if(i<dsz-3){dst[i++]='\\';dst[i++]='"';} }
        else if(*src=='\n'){ if(i<dsz-3){dst[i++]='\\';dst[i++]='n';} }
        else if(*src=='\\'){ if(i<dsz-3){dst[i++]='\\';dst[i++]='\\';} }
        else dst[i++]=*src;
    }dst[i]=0;
}

static char *profile_dir(void) {
    static char d[256];
    const char *h=getenv("HOME"); if(!h)h=".";
    snprintf(d,sizeof(d),"%s/.pyon",h);
    return d;
}

static void profile_path(const char *pubkey, char *out, int n) {
    const char *h=getenv("HOME"); if(!h)h=".";
    if(pubkey&&pubkey[0])
        snprintf(out,n,"%s/.pyon/profiles/%.64s.json",h,pubkey);
    else
        snprintf(out,n,"%s/.pyon/profile.json",h);
}

/* ── load / save ────────────────────────────────────────────────── */
void profile_load(Profile *p, const Config *cfg) {
    memset(p,0,sizeof(*p));
    snprintf(p->name,sizeof(p->name),"%s",cfg->my_name[0]?cfg->my_name:"anon");
    snprintf(p->pubkey,sizeof(p->pubkey),"%s",cfg->my_pubkey);

    char path[256]; profile_path(NULL,path,sizeof(path));
    FILE *f=fopen(path,"r"); if(!f)return;
    char *buf=(char*)malloc(8192); if(!buf){fclose(f);return;}
    int len=(int)fread(buf,1,8191,f); buf[len]=0; fclose(f);

    jget(buf,"name",      p->name,   sizeof(p->name));
    jget(buf,"bio",       p->bio,    sizeof(p->bio));
    jget(buf,"fortune",   p->fortune,sizeof(p->fortune));
    char tmp[32];
    jget(buf,"fortune_day",tmp,sizeof(tmp)); p->fortune_day=atol(tmp);
    jget(buf,"rating_sum",tmp,sizeof(tmp)); p->rating_sum=atoi(tmp);
    jget(buf,"rating_count",tmp,sizeof(tmp)); p->rating_count=atoi(tmp);
    jget(buf,"pubkey",    p->pubkey, sizeof(p->pubkey));
    free(buf);
}

void profile_save(const Profile *p) {
    char dir[256]; snprintf(dir,sizeof(dir),"%s/.pyon",getenv("HOME")?getenv("HOME"):".");
    mkdir(dir,0700);

    char path[256]; profile_path(NULL,path,sizeof(path));
    FILE *f=fopen(path,"w"); if(!f)return;

    char ename[256],ebio[8192],efort[512];
    json_escape(p->name,ename,sizeof(ename));
    json_escape(p->bio, ebio, sizeof(ebio));
    json_escape(p->fortune,efort,sizeof(efort));

    fprintf(f,
        "{\n"
        "  \"name\": \"%s\",\n"
        "  \"bio\": \"%s\",\n"
        "  \"fortune\": \"%s\",\n"
        "  \"fortune_day\": %ld,\n"
        "  \"rating_sum\": %d,\n"
        "  \"rating_count\": %d,\n"
        "  \"pubkey\": \"%s\"\n"
        "}\n",
        ename, ebio, efort,
        p->fortune_day, p->rating_sum, p->rating_count, p->pubkey);
    fclose(f);
}

/* salva perfil alheio recebido pela rede */
static void profile_save_foreign(const Profile *p) {
    if(!p->pubkey[0])return;
    char dir[256]; snprintf(dir,sizeof(dir),"%s/.pyon/profiles",getenv("HOME")?getenv("HOME"):".");
    mkdir(dir,0700);

    char path[256]; profile_path(p->pubkey,path,sizeof(path));
    FILE *f=fopen(path,"w"); if(!f)return;

    char ename[256],ebio[8192],efort[512];
    json_escape(p->name,ename,sizeof(ename));
    json_escape(p->bio, ebio, sizeof(ebio));
    json_escape(p->fortune,efort,sizeof(efort));

    fprintf(f,
        "{\n"
        "  \"name\": \"%s\",\n"
        "  \"bio\": \"%s\",\n"
        "  \"fortune\": \"%s\",\n"
        "  \"fortune_day\": %ld,\n"
        "  \"rating_sum\": %d,\n"
        "  \"rating_count\": %d,\n"
        "  \"pubkey\": \"%s\"\n"
        "}\n",
        ename, ebio, efort,
        p->fortune_day, p->rating_sum, p->rating_count, p->pubkey);
    fclose(f);
}

static Profile *profile_load_foreign(const char *pubkey) {
    char path[256]; profile_path(pubkey,path,sizeof(path));
    FILE *f=fopen(path,"r"); if(!f)return NULL;
    Profile *p=(Profile*)calloc(1,sizeof(Profile));
    char *buf=(char*)malloc(8192); if(!buf){free(p);fclose(f);return NULL;}
    int len=(int)fread(buf,1,8191,f); buf[len]=0; fclose(f);
    jget(buf,"name",      p->name,   sizeof(p->name));
    jget(buf,"bio",       p->bio,    sizeof(p->bio));
    jget(buf,"fortune",   p->fortune,sizeof(p->fortune));
    char tmp[32];
    jget(buf,"fortune_day",tmp,sizeof(tmp)); p->fortune_day=atol(tmp);
    jget(buf,"rating_sum",tmp,sizeof(tmp)); p->rating_sum=atoi(tmp);
    jget(buf,"rating_count",tmp,sizeof(tmp)); p->rating_count=atoi(tmp);
    jget(buf,"pubkey",    p->pubkey, sizeof(p->pubkey));
    free(buf);
    if(!p->name[0]) snprintf(p->name,sizeof(p->name),"anon:%.8s",pubkey);
    if(!p->pubkey[0]) snprintf(p->pubkey,sizeof(p->pubkey),"%s",pubkey);
    return p;
}

/* ── helpers de renderização ─────────────────────────────────────── */

/* desenha borda do perfil */
static void draw_profile_border(int y0, int x0, int w, int h, const char *title) {
    attron(COLOR_PAIR(C_DIM));
    mvaddch(y0,x0,ACS_ULCORNER);
    mvhline(y0,x0+1,ACS_HLINE,w-2);
    mvaddch(y0,x0+w-1,ACS_URCORNER);
    for(int r=1;r<h-1;r++){mvaddch(y0+r,x0,ACS_VLINE);mvaddch(y0+r,x0+w-1,ACS_VLINE);}
    mvaddch(y0+h-1,x0,ACS_LLCORNER);
    mvhline(y0+h-1,x0+1,ACS_HLINE,w-2);
    mvaddch(y0+h-1,x0+w-1,ACS_LRCORNER);
    attroff(COLOR_PAIR(C_DIM));
    if(title&&title[0]){
        int tl=(int)strlen(title);
        int tx=x0+(w-tl-4)/2; if(tx<x0+1)tx=x0+1;
        attron(COLOR_PAIR(C_TITLE)|A_BOLD);
        mvprintw(y0,tx,"[ %.*s ]",w-6,title);
        attroff(COLOR_PAIR(C_TITLE)|A_BOLD);
    }
}

/* renderiza estrelas de avaliação */
static void draw_stars(int y, int x, float rating, int count) {
    int full=(int)(rating+0.5f);
    attron(COLOR_PAIR(C_WARN)|A_BOLD);
    for(int i=0;i<5;i++) addstr(i<full?"★":"☆");
    attroff(COLOR_PAIR(C_WARN)|A_BOLD);
    attron(COLOR_PAIR(C_DIM));
    if(count>0) printw("  %.1f  (%d avaliação%s)",rating,count,count==1?"":"ões");
    else        addstr("  sem avaliações ainda");
    attroff(COLOR_PAIR(C_DIM));
    (void)y;(void)x;
}

/* renderiza bio com quebra de linha e scroll */
static int draw_bio(int y, int x0, int w, int max_y,
                    const char *bio, int scroll) {
    int bw=w-4; if(bw<10)bw=10;
    int len=(int)strlen(bio), off=0, line=0, drawn=0;
    while(off<=len){
        int end=off,lw=0;
        while(end<len&&bio[end]!='\n'&&lw<bw){end++;lw++;}
        if(line>=scroll&&y<max_y){
            attron(COLOR_PAIR(C_NORMAL));
            mvprintw(y,x0+2,"%.*s",bw,bio+off);
            attroff(COLOR_PAIR(C_NORMAL));
            y++; drawn++;
        }
        line++;
        if(bio[end]=='\n')end++;
        else if(end>=len)break;
        off=end;
    }
    return drawn;
}

/* sorte do dia — dia desde epoch */
static long today_day(void) { return (long)(time(NULL)/86400); }

/* ═══════════════════════════════════════════════════════════════
 * EDIÇÃO DO PRÓPRIO PERFIL
 * ═══════════════════════════════════════════════════════════════ */
void profile_edit(Config *cfg) {
    Profile p;
    profile_load(&p,cfg);

    /* campos editáveis */
    /* 0=nome 1=sorte 2=bio */
    int field=0, bio_scroll=0;
    int bio_cur=(int)strlen(p.bio);
    int name_cur=(int)strlen(p.name);
    int fort_cur=(int)strlen(p.fortune);

    while(1){
        relay_tick();
        int rows,cols;
        getmaxyx(stdscr,rows,cols);
        erase();

        int w=cols-8; if(w>72)w=72; if(w<40)w=40;
        int x0=(cols-w)/2;
        int h=rows-4; if(h<10)h=10;
        int y0=2;

        ui_topbar(cols,"✦ perfil — editar",
            "[Tab] campo  [Enter] confirmar  [ESC] sair");

        attron(COLOR_PAIR(C_DIM)|A_BOLD);
        mvprintw(1,2,"[Tab] alternar campo  [Enter] salvar campo  [ESC] salvar e sair");
        attroff(COLOR_PAIR(C_DIM)|A_BOLD);

        draw_profile_border(y0,x0,w,h,"meu perfil");

        int y=y0+1;
        int inner_w=w-4;

        /* ── nome ── */
        attron(COLOR_PAIR(C_DIM)|A_BOLD);
        mvprintw(y,x0+2,"nome:");
        attroff(COLOR_PAIR(C_DIM)|A_BOLD);
        if(field==0){
            attron(COLOR_PAIR(C_SEL)|A_BOLD);
            mvprintw(y,x0+8,"%-*.*s",inner_w-6,inner_w-6,p.name);
            attroff(COLOR_PAIR(C_SEL)|A_BOLD);
        } else {
            attron(COLOR_PAIR(C_TITLE)|A_BOLD);
            mvprintw(y,x0+8,"%-*.*s",inner_w-6,inner_w-6,p.name);
            attroff(COLOR_PAIR(C_TITLE)|A_BOLD);
        }
        y++;

        /* ── avaliação (somente leitura) ── */
        float avg = p.rating_count>0 ? (float)p.rating_sum/p.rating_count : 0.f;
        mvprintw(y,x0+2,""); draw_stars(y,x0+2,avg,p.rating_count);
        y+=2;

        /* ── sorte do dia ── */
        int fortune_stale = (today_day() != p.fortune_day);
        attron(COLOR_PAIR(C_DIM)|A_BOLD);
        mvprintw(y,x0+2,"sorte: ");
        attroff(COLOR_PAIR(C_DIM)|A_BOLD);
        if(fortune_stale){
            attron(COLOR_PAIR(C_WARN));
            mvprintw(y,x0+9,"(atualizar para hoje)");
            attroff(COLOR_PAIR(C_WARN));
        }
        y++;
        if(field==2){
            attron(COLOR_PAIR(C_SEL)|A_BOLD);
            mvprintw(y,x0+2,"%-*.*s",inner_w,inner_w,p.fortune);
            attroff(COLOR_PAIR(C_SEL)|A_BOLD);
        } else {
            attron(COLOR_PAIR(C_WARN)|A_BOLD);
            mvprintw(y,x0+2,"\"%-*.*s\"",inner_w-2,inner_w-2,p.fortune);
            attroff(COLOR_PAIR(C_WARN)|A_BOLD);
        }
        y+=2;

        /* ── bio ── */
        attron(COLOR_PAIR(C_DIM)|A_BOLD);
        mvprintw(y,x0+2,"bio: (%d/2500)",
            (int)strlen(p.bio));
        if(field==1){
            attron(COLOR_PAIR(C_TITLE)|A_BOLD);
            addstr("  ← editando");
            attroff(COLOR_PAIR(C_TITLE)|A_BOLD);
        }
        attroff(COLOR_PAIR(C_DIM)|A_BOLD);
        y++;

        if(field==1){
            /* destaque da bio em edição */
            attron(COLOR_PAIR(C_SEL));
            mvhline(y,x0+1,' ',w-2);
            attroff(COLOR_PAIR(C_SEL));
        }

        int bio_area = h - (y - y0) - 3;
        if(bio_area<3)bio_area=3;
        draw_bio(y,x0,w,y+bio_area,p.bio,bio_scroll);

        /* contagem e hint */
        attron(COLOR_PAIR(C_DIM));
        mvprintw(y0+h-2,x0+2,"[PgUp/Dn] rolar bio  [s] salvar  [q/ESC] sair");
        attroff(COLOR_PAIR(C_DIM));

        wnoutrefresh(stdscr);
        doupdate();

        int ch=ui_key_timeout(3);
        if(ch==ERR){ relay_tick(); continue; }

        /* campo ativo recebe input */
        char *tgt = field==0 ? p.name : field==2 ? p.fortune : NULL;
        int  *cur_p = field==0 ? &name_cur : field==2 ? &fort_cur : &bio_cur;
        int   maxsz = field==0 ? (int)sizeof(p.name)-1 :
                      field==2 ? (int)sizeof(p.fortune)-1 : -1;

        switch(ch){
            case 27: /* ESC — salva e sai */
            case 'q': case 'Q':
                profile_save(&p);
                snprintf(cfg->my_name,sizeof(cfg->my_name),"%s",p.name);
                config_save_name(p.name);
                return;

            case '\t': /* Tab — próximo campo */
                field=(field+1)%3;
                break;

            case KEY_PPAGE:
                if(field==1) bio_scroll=(bio_scroll>3)?bio_scroll-3:0;
                break;
            case KEY_NPAGE:
                if(field==1) bio_scroll+=3;
                break;

            case KEY_BACKSPACE: case 127:
                if(field==1){ /* bio */
                    int l=(int)strlen(p.bio);
                    if(bio_cur>0){memmove(p.bio+bio_cur-1,p.bio+bio_cur,l-bio_cur+1);bio_cur--;}
                } else if(tgt&&cur_p&&*cur_p>0){
                    int l=(int)strlen(tgt);
                    memmove(tgt+*cur_p-1,tgt+*cur_p,l-*cur_p+1);
                    (*cur_p)--;
                }
                break;

            default:
                if(ch==10||ch==13||ch==KEY_ENTER){
                    if(field==1){ /* Enter na bio = nova linha */
                        int l=(int)strlen(p.bio);
                        if(bio_cur<(int)sizeof(p.bio)-2){
                            memmove(p.bio+bio_cur+1,p.bio+bio_cur,l-bio_cur+1);
                            p.bio[bio_cur++]='\n';
                        }
                    }
                    /* nos outros campos, Enter = confirma (próximo campo) */
                    else { field=(field+1)%3; }
                } else if(ch>=32&&ch<127){
                    if(field==1){ /* bio */
                        int l=(int)strlen(p.bio);
                        if(bio_cur<(int)sizeof(p.bio)-2){
                            memmove(p.bio+bio_cur+1,p.bio+bio_cur,l-bio_cur+1);
                            p.bio[bio_cur++]=(char)ch;
                            if(bio_cur>sizeof(p.bio)-2)bio_cur=sizeof(p.bio)-2;
                        }
                    } else if(tgt&&cur_p&&maxsz>0){
                        int l=(int)strlen(tgt);
                        if(*cur_p<maxsz){
                            memmove(tgt+*cur_p+1,tgt+*cur_p,l-*cur_p+1);
                            tgt[(*cur_p)++]=(char)ch;
                        }
                    }
                }
                /* sorte do dia: ao editar, marca como hoje */
                if(field==2) p.fortune_day=today_day();
                break;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
 * VISUALIZAÇÃO DO PERFIL ALHEIO
 * ═══════════════════════════════════════════════════════════════ */
void profile_view(const char *pubkey, const char *name, Config *cfg) {
    Profile *p = profile_load_foreign(pubkey);
    Profile local; /* fallback se não tiver cache */
    if(!p){
        p=&local; memset(p,0,sizeof(*p));
        snprintf(p->name,sizeof(p->name),"%s",name?name:"anon");
        snprintf(p->pubkey,sizeof(p->pubkey),"%s",pubkey?pubkey:"");
    }

    int my_rating=0; /* avaliação que o usuário vai dar (0=não avaliou) */
    int bio_scroll=0;

    while(1){
        relay_tick();
        int rows,cols;
        getmaxyx(stdscr,rows,cols);
        erase();

        int w=cols-8; if(w>72)w=72; if(w<40)w=40;
        int x0=(cols-w)/2;
        int h=rows-4; if(h<10)h=10;
        int y0=2;

        char title[64]; snprintf(title,sizeof(title),"perfil de %s",p->name);
        ui_topbar(cols,title,"[1-5] avaliar  [PgUp/Dn] rolar  [ESC] voltar");

        attron(COLOR_PAIR(C_DIM)|A_BOLD);
        mvprintw(1,2,"[1-5] avaliar  [PgUp/Dn] rolar bio  [ESC] voltar");
        attroff(COLOR_PAIR(C_DIM)|A_BOLD);

        draw_profile_border(y0,x0,w,h,title);

        int y=y0+1;
        int inner_w=w-4;

        /* nome */
        attron(COLOR_PAIR(C_NICK)|A_BOLD);
        mvprintw(y,x0+2,"%-*.*s",inner_w,inner_w,p->name);
        attroff(COLOR_PAIR(C_NICK)|A_BOLD);
        attron(COLOR_PAIR(C_DIM));
        printw("  %.12s…",p->pubkey);
        attroff(COLOR_PAIR(C_DIM));
        y++;

        /* estrelas */
        float avg = p->rating_count>0 ? (float)p->rating_sum/p->rating_count : 0.f;
        mvprintw(y,x0+2,""); draw_stars(y,x0+2,avg,p->rating_count);
        y++;

        /* avaliação do usuário atual */
        if(my_rating>0){
            attron(COLOR_PAIR(C_TITLE)|A_BOLD);
            mvprintw(y,x0+2,"sua avaliação: ");
            attroff(COLOR_PAIR(C_TITLE)|A_BOLD);
            attron(COLOR_PAIR(C_WARN)|A_BOLD);
            for(int i=0;i<5;i++) addstr(i<my_rating?"★":"☆");
            attroff(COLOR_PAIR(C_WARN)|A_BOLD);
        } else {
            attron(COLOR_PAIR(C_DIM));
            mvprintw(y,x0+2,"pressione [1-5] para avaliar");
            attroff(COLOR_PAIR(C_DIM));
        }
        y+=2;

        /* sorte do dia */
        if(p->fortune[0]){
            attron(COLOR_PAIR(C_DIM)|A_BOLD);
            mvprintw(y,x0+2,"✦ sorte: ");
            attroff(COLOR_PAIR(C_DIM)|A_BOLD);
            attron(COLOR_PAIR(C_WARN)|A_BOLD);
            mvprintw(y,x0+11,"\"%-*.*s\"",inner_w-11,inner_w-11,p->fortune);
            attroff(COLOR_PAIR(C_WARN)|A_BOLD);
            y+=2;
        }

        /* bio */
        attron(COLOR_PAIR(C_DIM)|A_BOLD);
        mvprintw(y,x0+2,"bio:");
        attroff(COLOR_PAIR(C_DIM)|A_BOLD);
        y++;

        int bio_area = h-(y-y0)-3;
        if(bio_area<2)bio_area=2;
        if(p->bio[0])
            draw_bio(y,x0,w,y+bio_area,p->bio,bio_scroll);
        else{
            attron(COLOR_PAIR(C_DIM));
            mvprintw(y,x0+2,"(sem bio)");
            attroff(COLOR_PAIR(C_DIM));
        }

        wnoutrefresh(stdscr);
        doupdate();

        int ch=ui_key_timeout(3);
        if(ch==ERR){ relay_tick(); continue; }

        switch(ch){
            case 27: case 'q': case 'Q':
                if(p!=&local)free(p);
                return;
            case KEY_PPAGE: bio_scroll=(bio_scroll>3)?bio_scroll-3:0; break;
            case KEY_NPAGE: bio_scroll+=3; break;
            case '1': case '2': case '3': case '4': case '5':{
                int stars=ch-'0';
                my_rating=stars;
                p->rating_sum+=stars;
                p->rating_count++;
                profile_save_foreign(p);
                /* anuncia avaliação via relay se conectado */
                if(relay_connected()&&pubkey&&pubkey[0]){
                    char json[256];
                    snprintf(json,sizeof(json),
                        "{\"type\":\"rate\",\"to\":\"%s\",\"stars\":%d}",
                        pubkey,stars);
                    relay_send(json);
                }
                break;
            }
            default: break;
        }
    }
}
