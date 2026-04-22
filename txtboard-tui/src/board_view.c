/*
 * pyon — board view + thread view
 * board_view_run: lista de posts com navegação
 * thread_view_run: página separada de thread com replies aninhados
 */
#include "board_view.h"
#include "relay.h"
#include "ui.h"
#include "boards.h"
#include "config.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>

/* ── dados ───────────────────────────────────────────────────── */
#define MAX_POSTS  256
#define MAX_BODY   4096
#define MAX_SUBJ   128
#define MAX_IMGS   4
#define MAX_DEPTH  8    /* profundidade máxima de aninhamento */

typedef struct {
    int  id;
    char author[32];
    char subj[MAX_SUBJ];
    char body[MAX_BODY];
    char imgs[MAX_IMGS][64];
    int  img_n;
    int  reply_to;
    char ts[20];
} Post;

static Post posts[MAX_POSTS];
static int  post_n = 0;

static int flt[MAX_POSTS];
static int flt_n = 0;

/* ── helpers de carregamento ─────────────────────────────────── */
static void add_post(int id,const char*auth,const char*subj,
                     const char*body,int reply,const char*img){
    if(post_n>=MAX_POSTS)return;
    Post*p=&posts[post_n++];
    memset(p,0,sizeof(*p));
    p->id=id;p->reply_to=reply;
    snprintf(p->author,sizeof(p->author),"%s",auth?auth:"anon");
    snprintf(p->subj,  sizeof(p->subj),  "%s",subj?subj:"");
    snprintf(p->body,  sizeof(p->body),  "%s",body?body:"");
    time_t t=time(NULL)-(post_n*240);
    struct tm*tm=localtime(&t);
    strftime(p->ts,sizeof(p->ts),"%d/%m %H:%M",tm);
    if(img){snprintf(p->imgs[0],64,"%s",img);p->img_n=1;}
}

static void jget(const char*json,const char*key,char*out,int n){
    char needle[64]; snprintf(needle,sizeof(needle),"\"%s\":",key);
    const char*p=strstr(json,needle);if(!p){out[0]=0;return;}
    p+=strlen(needle);while(*p==' ')p++;
    if(*p=='"'){
        p++;int i=0;
        while(*p&&*p!='"'&&i<n-1){
            if(*p=='\\'&&*(p+1)=='n'){out[i++]='\n';p+=2;continue;}
            if(*p=='\\'&&*(p+1)=='"'){out[i++]='"';p+=2;continue;}
            if(*p=='\\'&&*(p+1)=='\\'){out[i++]='\\';p+=2;continue;}
            out[i++]=*p++;
        }out[i]=0;
    }else{
        int i=0;
        while(*p&&*p!=','&&*p!='}'&&i<n-1)out[i++]=*p++;
        out[i]=0;
    }
}

static void load_ndjson(const char*slug){
    post_n=0;
    const char*home=getenv("HOME");if(!home)return;
    char path[256];
    snprintf(path,sizeof(path),"%s/.txtboard/db/posts.ndjson",home);
    {FILE*touch=fopen(path,"a");if(touch)fclose(touch);}
    FILE*f=fopen(path,"r");if(!f)return;
    char line[16384];
    char needle[32];snprintf(needle,sizeof(needle),"\"board\":\"%s\"",slug);
    while(fgets(line,sizeof(line),f)&&post_n<MAX_POSTS){
        if(!strstr(line,needle))continue;
        Post*p=&posts[post_n];
        memset(p,0,sizeof(*p));
        char tmp[MAX_BODY];
        jget(line,"id",tmp,sizeof(tmp));p->id=atoi(tmp);
        jget(line,"body",p->body,MAX_BODY);
        jget(line,"subject",p->subj,MAX_SUBJ);
        jget(line,"reply_to",tmp,sizeof(tmp));p->reply_to=atoi(tmp);
        char pk[128]={0},nm[64]={0};
        jget(line,"author_pubkey",pk,sizeof(pk));
        jget(line,"author_name",nm,sizeof(nm));
        if(nm[0])snprintf(p->author,sizeof(p->author),"%s",nm);
        else     snprintf(p->author,sizeof(p->author),"anon:%.8s",pk);
        char iso[32]={0};
        jget(line,"created_at",iso,sizeof(iso));
        if(strlen(iso)>=16)
            snprintf(p->ts,sizeof(p->ts),"%.5s %.5s",iso+5,iso+11);
        else strncpy(p->ts,iso,sizeof(p->ts)-1);
        post_n++;
    }
    fclose(f);
}

static void load_demo(const Board*b){
    post_n=0;
    add_post(1,"anon:4f2a8c",b->title,
        "bem-vindos!\nregras: respeito mutuo, sem spam. (◕‿◕✿)",0,NULL);
    add_post(2,"yumeiri",NULL,
        ">>1 finalmente um board decente por aqui uwu",1,NULL);
    add_post(3,"misakichan","duvida rapida",
        "alguem sabe onde achar boa resolucao do artbook?\nprocurei no pixiv mas nada recente",
        0,"artbook.jpg");
    add_post(4,"anon:9b3c11",NULL,
        ">>3 tenta no sadpanda, geralmente tem scan de qualidade",3,NULL);
    add_post(5,"yumeiri",NULL,
        ">>3 tenho o arquivo, posso upar\n/fan/5",3,NULL);
    add_post(6,"neko-chan","fanart nova",
        "fiz essa fan hoje de manha, ainda aprendendo digital\naceito criticas! (˶ᵔ ᵕ ᵔ˶)",
        0,"fanart.png");
    add_post(7,"misakichan",NULL,
        ">>6 muito fofa!! os olhos ficaram perfeitos\nso o cabelo que ta pesado no sombreado",6,NULL);
    add_post(8,"anon:4f2a8c",NULL,
        ">>6 >>7 concordo, estilo muito bom pra iniciante",6,NULL);
}

static void build_filter(const char*q){
    flt_n=0;
    for(int i=0;i<post_n;i++){
        if(!q[0]){flt[flt_n++]=i;continue;}
        char h[MAX_BODY+MAX_SUBJ+40],ql[64];
        snprintf(h,sizeof(h),"%s %s %s",posts[i].subj,posts[i].body,posts[i].author);
        int qi=0,hi=0;
        for(const char*p=q;*p&&qi<63;p++)ql[qi++]=tolower((unsigned char)*p);ql[qi]=0;
        for(const char*p=h;*p&&hi<(int)sizeof(h)-1;p++)h[hi++]=tolower((unsigned char)*p);h[hi]=0;
        if(strstr(h,ql))flt[flt_n++]=i;
    }
}

/* ── encontra Post por ID ────────────────────────────────────── */
static Post*find_post(int id){
    for(int i=0;i<post_n;i++)if(posts[i].id==id)return&posts[i];
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════
 * THREAD VIEW — página separada, threads dentro de threads
 * ═══════════════════════════════════════════════════════════════ */

/* Nó da árvore de replies */
typedef struct ThreadNode {
    Post *post;
    int   depth;    /* nível de aninhamento 0=raiz */
    int   idx;      /* índice flat para scroll */
} ThreadNode;

#define MAX_TNODES 512
static ThreadNode tnodes[MAX_TNODES];
static int        tnode_n = 0;

/* Constrói árvore de replies em DFS a partir de root_id */
static void build_tree(int root_id, int depth){
    if(tnode_n>=MAX_TNODES||depth>=MAX_DEPTH)return;
    Post*p=find_post(root_id);
    if(!p)return;
    tnodes[tnode_n].post=p;
    tnodes[tnode_n].depth=depth;
    tnodes[tnode_n].idx=tnode_n;
    tnode_n++;
    /* filhos: todos os posts que respondem a root_id */
    for(int i=0;i<post_n;i++){
        if(posts[i].reply_to==root_id&&posts[i].id!=root_id)
            build_tree(posts[i].id,depth+1);
    }
}

/* Desenha corpo com wrap, respeitando indent e cor de >>quote */
static int draw_body(const char*body,int y,int x,int bw,int max_y){
    int len=(int)strlen(body),off=0,row=y;
    while(off<len&&row<max_y){
        int is_q=(off==0||body[off-1]=='\n')&&(off+1<len)
                 &&body[off]=='>'&&body[off+1]=='>';
        if(is_q) attron(COLOR_PAIR(C_SYS)|A_BOLD);
        else     attron(COLOR_PAIR(C_NORMAL));
        int end=off,lw=0;
        while(end<len&&body[end]!='\n'&&lw<bw){end++;lw++;}
        mvprintw(row,x,"%.*s",lw,body+off);
        if(is_q) attroff(COLOR_PAIR(C_SYS)|A_BOLD);
        else     attroff(COLOR_PAIR(C_NORMAL));
        if(end<len&&body[end]=='\n')end++;
        off=end;row++;
    }
    return row;
}

/* Renderiza a thread view */
static void render_thread(int rows,int cols,int sel,int scroll,
                           const Board*b,const char*srch,int srching){
    clear();

    /* topbar */
    char tl[64];
    if(tnode_n>0)
        snprintf(tl,sizeof(tl),"/%s/ >>#%d",b->slug,tnodes[0].post->id);
    else
        snprintf(tl,sizeof(tl),"/%s/",b->slug);
    ui_topbar(cols,tl,"[jk] nav  [n] reply  [/] buscar  [ESC] voltar");

    if(srching){
        attron(COLOR_PAIR(C_WARN)|A_BOLD);
        mvhline(1,0,' ',cols); mvprintw(1,2,"/ %s_",srch);
        attroff(COLOR_PAIR(C_WARN)|A_BOLD);
    }

    int y=2,x0=0;

    for(int ti=scroll;ti<tnode_n&&y<rows-2;ti++){
        ThreadNode*tn=&tnodes[ti];
        Post*p=tn->post;
        int depth=tn->depth;
        int indent=depth*3;         /* 3 espaços por nível */
        int bw=cols-indent-2;
        if(bw<20)bw=20;
        int is_sel=(ti==sel);

        /* linha separadora */
        if(ti>scroll){
            attron(COLOR_PAIR(C_DIM));
            mvhline(y,indent,ACS_HLINE,cols-indent);
            attroff(COLOR_PAIR(C_DIM));
            y++;if(y>=rows-2)break;
        }

        /* árvore visual: ╰── */
        if(depth>0){
            attron(COLOR_PAIR(C_DIM));
            for(int d=0;d<depth-1;d++) mvprintw(y,x0+d*3,"│  ");
            mvprintw(y,x0+(depth-1)*3,"╰──");
            attroff(COLOR_PAIR(C_DIM));
        }

        /* header: id + autor + ts */
        if(is_sel){attron(COLOR_PAIR(C_SEL)|A_BOLD);mvhline(y,indent,' ',cols-indent);}

        attron(COLOR_PAIR(is_sel?C_SEL:C_BOARD)|A_BOLD);
        mvprintw(y,indent+1,">>#%-4d",p->id);
        attroff(A_BOLD);
        attron(COLOR_PAIR(is_sel?C_SEL:C_NICK)|A_BOLD);
        printw(" %-14.14s",p->author);
        attroff(A_BOLD);
        attron(COLOR_PAIR(is_sel?C_SEL:C_DIM));
        printw(" %s",p->ts);
        if(p->img_n>0){attron(COLOR_PAIR(is_sel?C_SEL:C_WARN)|A_BOLD);printw(" [img]");attroff(A_BOLD);}
        if(is_sel)attroff(COLOR_PAIR(C_SEL));
        else attroff(COLOR_PAIR(C_DIM));
        y++;

        /* assunto */
        if(p->subj[0]&&y<rows-2){
            attron(COLOR_PAIR(C_TITLE)|A_BOLD);
            mvprintw(y++,indent+2,"%.*s",bw,p->subj);
            attroff(COLOR_PAIR(C_TITLE)|A_BOLD);
        }

        /* corpo */
        y=draw_body(p->body,y,indent+2,bw,rows-2);
    }

    /* rodapé */
    ui_footbar(rows-1,cols,NULL);
    doupdate();
}

/* Loop da thread view */
static void thread_view_run(const Board*b,const Config*cfg,int root_id){
    /* constrói árvore */
    tnode_n=0;
    build_tree(root_id,0);
    if(tnode_n==0)return;

    int sel=0,scroll=0;
    int srching=0;
    char srch[64]={0};

    while(1){
        relay_tick();

        /* checa posts novos */
        {
            RelayNewPost np;int got=0;
            while(relay_post_dequeue(&np))if(!strcmp(np.board,b->slug))got=1;
            if(got){
                /* reconstrói árvore preservando sel */
                int old_id=tnodes[sel].post->id;
                /* recarrega posts */
                load_ndjson(b->slug);
                if(!post_n)load_demo(b);
                tnode_n=0;build_tree(root_id,0);
                /* tenta preservar sel */
                sel=0;
                for(int ti=0;ti<tnode_n;ti++)
                    if(tnodes[ti].post->id==old_id){sel=ti;break;}
            }
        }

        int rows,cols;
        getmaxyx(stdscr,rows,cols);
        int vis=rows-4;

        /* scroll */
        if(sel<scroll)scroll=sel;
        if(sel>=scroll+vis)scroll=sel-vis+1;
        if(scroll<0)scroll=0;

        render_thread(rows,cols,sel,scroll,b,srch,srching);

        int ch=ui_key_timeout(2);
        if(ch==ERR)continue;

        /* busca */
        if(srching){
            if(ch==27||ch=='\n'||ch==KEY_ENTER){srching=0;srch[0]=0;}
            else if(ch==KEY_BACKSPACE||ch==127){int l=(int)strlen(srch);if(l>0)srch[l-1]=0;}
            else if(ch>=32&&ch<127){int l=(int)strlen(srch);if(l<63){srch[l]=ch;srch[l+1]=0;}}
            continue;
        }

        switch(ch){
            case KEY_UP:case'k':
                if(sel>0)sel--;
                break;
            case KEY_DOWN:case'j':
                if(sel<tnode_n-1)sel++;
                break;
            case KEY_PPAGE:scroll-=vis/2;if(scroll<0)scroll=0;sel=scroll;break;
            case KEY_NPAGE:scroll+=vis/2;if(scroll>tnode_n-1)scroll=tnode_n-1;sel=scroll;break;
            case'n':{
                /* responder ao post selecionado */
                Post*rp=tnodes[sel].post;
                char nsubj[MAX_SUBJ]={0},nbody[MAX_BODY]={0};
                int nfield=0,ncur=0;
                int comp=1;
                while(comp){
                    int r2,c2;getmaxyx(stdscr,r2,c2);
                    render_thread(r2,c2,sel,scroll,b,srch,0);
                    /* compose overlay */
                    int h2=8,y0=r2-h2;
                    attron(COLOR_PAIR(C_DIM));mvhline(y0,0,ACS_HLINE,c2);attroff(COLOR_PAIR(C_DIM));
                    attron(COLOR_PAIR(C_TITLE)|A_BOLD);
                    mvprintw(y0,2,"[ reply a >>#%d ]  Tab: campo  Enter: enviar  ESC: cancelar",rp->id);
                    attroff(COLOR_PAIR(C_TITLE)|A_BOLD);
                    attron(nfield==0?(COLOR_PAIR(C_SEL)|A_BOLD):COLOR_PAIR(C_DIM));
                    mvprintw(y0+1,2,"assunto: %-*.*s",c2-12,c2-12,nsubj);
                    attrset(A_NORMAL);
                    attron(nfield==1?(COLOR_PAIR(C_SEL)|A_BOLD):COLOR_PAIR(C_DIM));
                    mvprintw(y0+2,2,"corpo:");
                    attrset(A_NORMAL);
                    attron(COLOR_PAIR(C_NORMAL));
                    int bw2=c2-4;
                    for(int l=0;l<4&&y0+3+l<r2;l++){int off=l*bw2;if(off>(int)strlen(nbody))break;mvprintw(y0+3+l,4,"%-*.*s",bw2,bw2,nbody+off);}
                    attroff(COLOR_PAIR(C_NORMAL));
                    doupdate();
                    int ch2=ui_key();
                    char*tgt=nfield==0?nsubj:nbody;
                    int mx=nfield==0?MAX_SUBJ-1:MAX_BODY-1;
                    switch(ch2){
                        case 27:comp=0;break;
                        case'\t':nfield=1-nfield;ncur=(int)strlen(nfield==0?nsubj:nbody);break;
                        case'\n':case KEY_ENTER:
                            if(nbody[0]){
                                char sb[MAX_BODY*2],ss[MAX_SUBJ*2];
                                int bi=0,si=0;
                                for(int x=0;nbody[x]&&bi<(int)sizeof(sb)-2;x++){if(nbody[x]=='\'')sb[bi++]='\\';sb[bi++]=nbody[x];}sb[bi]=0;
                                for(int x=0;nsubj[x]&&si<(int)sizeof(ss)-2;x++){if(nsubj[x]=='\'')ss[si++]='\\';ss[si++]=nsubj[x];}ss[si]=0;
                                char cmd[MAX_BODY+256];
                                snprintf(cmd,sizeof(cmd),
                                    "pyon-core post --board '%s' --body '%s' --subject '%s' --reply %d 2>/dev/null"
                                    " || txtboard-core post --board '%s' --body '%s' --subject '%s' --reply %d 2>/dev/null",
                                    b->slug,sb,nsubj[0]?ss:"",rp->id,
                                    b->slug,sb,nsubj[0]?ss:"",rp->id);
                                if(system(cmd)==0){
                                    add_post(post_n+1,cfg->my_name[0]?cfg->my_name:"você",
                                             nsubj[0]?nsubj:NULL,nbody,rp->id,NULL);
                                    /* reconstrói árvore */
                                    tnode_n=0;build_tree(root_id,0);
                                    sel=tnode_n-1;
                                }
                            }
                            comp=0;break;
                        case KEY_BACKSPACE:case 127:
                            if(ncur>0){memmove(tgt+ncur-1,tgt+ncur,mx-ncur);tgt[mx]=0;ncur--;}break;
                        default:
                            if(ch2>=32&&ch2<127&&ncur<mx){memmove(tgt+ncur+1,tgt+ncur,mx-ncur-1);tgt[ncur++]=(char)ch2;}break;
                    }
                }
                break;
            }
            case'/':srching=1;break;
            case 27:case'q':case'Q':return;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
 * BOARD VIEW — lista de posts
 * ═══════════════════════════════════════════════════════════════ */

static void draw_list(int rows,int cols,int sel_fi,int scroll){
    int y=2;
    for(int fi=scroll;fi<flt_n&&y<rows-2;fi++){
        Post*p=&posts[flt[fi]];
        int is_sel=(fi==sel_fi);

        if(fi>scroll){
            attron(COLOR_PAIR(C_DIM));mvhline(y,0,ACS_HLINE,cols);attroff(COLOR_PAIR(C_DIM));
            y++;if(y>=rows-2)break;
        }

        if(is_sel){attron(COLOR_PAIR(C_SEL)|A_BOLD);mvhline(y,0,' ',cols);}

        attron(COLOR_PAIR(is_sel?C_SEL:C_BOARD)|A_BOLD);
        mvprintw(y,1,">>#%-4d",p->id);attroff(A_BOLD);
        attron(COLOR_PAIR(is_sel?C_SEL:C_NICK)|A_BOLD);
        printw(" %-14.14s",p->author);attroff(A_BOLD);
        attron(COLOR_PAIR(is_sel?C_SEL:C_DIM)|A_BOLD);
        printw(" %s",p->ts);attroff(COLOR_PAIR(is_sel?C_SEL:C_DIM)|A_BOLD);
        if(p->img_n>0){attron(COLOR_PAIR(is_sel?C_SEL:C_WARN)|A_BOLD);printw(" [img]");attroff(COLOR_PAIR(C_WARN)|A_BOLD);}
        if(p->reply_to>0){attron(COLOR_PAIR(is_sel?C_SEL:C_SYS));printw(" >>%d",p->reply_to);attroff(COLOR_PAIR(is_sel?C_SEL:C_SYS));}
        /* conta replies */
        int nreplies=0;for(int k=0;k<post_n;k++)if(posts[k].reply_to==p->id)nreplies++;
        if(nreplies>0){attron(COLOR_PAIR(is_sel?C_SEL:C_DIM));printw(" (%d↩)",nreplies);attrset(A_NORMAL);}
        if(is_sel)attroff(COLOR_PAIR(C_SEL));
        y++;

        if(p->subj[0]&&y<rows-2){
            attron(COLOR_PAIR(C_TITLE)|A_BOLD);
            mvprintw(y++,3,"%.*s",cols-5,p->subj);
            attroff(COLOR_PAIR(C_TITLE)|A_BOLD);
        }
        if(y<rows-2){
            char prev[128];snprintf(prev,sizeof(prev),"%.*s",cols-5,p->body);
            char*nl=strchr(prev,'\n');if(nl)*nl=0;
            attron(COLOR_PAIR(C_NORMAL));mvprintw(y++,3,"%s",prev);attroff(COLOR_PAIR(C_NORMAL));
        }
    }
}

/* ── compose ────────────────────────────────────────────────── */
static void draw_compose(int rows,int cols,const char*subj,const char*body,int field){
    int h=8,y0=rows-h;
    attron(COLOR_PAIR(C_DIM));mvhline(y0,0,ACS_HLINE,cols);attroff(COLOR_PAIR(C_DIM));
    attron(COLOR_PAIR(C_TITLE)|A_BOLD);
    mvprintw(y0,2,"[ novo post ]  Tab: campo  Enter: enviar  ESC: cancelar");
    attroff(COLOR_PAIR(C_TITLE)|A_BOLD);
    attron(field==0?(COLOR_PAIR(C_SEL)|A_BOLD):COLOR_PAIR(C_DIM));
    mvprintw(y0+1,2,"assunto: %-*.*s",cols-12,cols-12,subj);
    attrset(A_NORMAL);
    attron(field==1?(COLOR_PAIR(C_SEL)|A_BOLD):COLOR_PAIR(C_DIM));
    mvprintw(y0+2,2,"corpo:");
    attrset(A_NORMAL);
    attron(COLOR_PAIR(C_NORMAL));
    int bw=cols-4;
    for(int l=0;l<4&&y0+3+l<rows;l++){int off=l*bw,blen=(int)strlen(body);if(off>blen)break;mvprintw(y0+3+l,4,"%-*.*s",bw,bw,body+off);}
    attroff(COLOR_PAIR(C_NORMAL));
}

/* ── loop principal board_view ───────────────────────────────── */
void board_view_run(const Board*b,const Config*cfg){
    load_ndjson(b->slug);
    if(!post_n)load_demo(b);
    build_filter("");

    int sel=0,scroll=0,comp=0,srching=0;
    char srch[64]={0};
    char nsubj[MAX_SUBJ]={0},nbody[MAX_BODY]={0};
    int  nfield=0,ncur=0;

    while(1){
        /* mantém o pipe do relay drenado mesmo fora da tela de chat */
        relay_tick();

        /* posts novos chegados pela rede */
        {
            RelayNewPost np;int got=0;
            while(relay_post_dequeue(&np))if(!strcmp(np.board,b->slug))got=1;
            if(got){
                int old_id=(sel<flt_n)?posts[flt[sel]].id:-1;
                load_ndjson(b->slug);if(!post_n)load_demo(b);
                build_filter(srch);sel=0;
                if(old_id>0)for(int fi=0;fi<flt_n;fi++)if(posts[flt[fi]].id==old_id){sel=fi;break;}
            }
        }

        int rows,cols;
        getmaxyx(stdscr,rows,cols);
        int lrows=comp?rows-9:rows;

        clear();

        char tl[64];snprintf(tl,sizeof(tl),"/%s/ — %s",b->slug,b->title);
        ui_topbar(cols,tl,"[jk] nav  [Enter] thread  [n] post  [/] buscar  [r] relay  [q] voltar");

        if(srching){
            attron(COLOR_PAIR(C_WARN)|A_BOLD);
            mvhline(1,0,' ',cols);mvprintw(1,2,"/ %s_   (%d posts)",srch,flt_n);
            attroff(COLOR_PAIR(C_WARN)|A_BOLD);
        } else {
            attron(COLOR_PAIR(C_DIM)|A_BOLD);
            mvprintw(1,1," %d posts  /",flt_n);
            attroff(COLOR_PAIR(C_DIM)|A_BOLD);
            attron(COLOR_PAIR(C_BOARD)|A_BOLD);printw("%s",b->slug);attroff(COLOR_PAIR(C_BOARD)|A_BOLD);
            attron(COLOR_PAIR(C_DIM)|A_BOLD);printw("/");attroff(COLOR_PAIR(C_DIM)|A_BOLD);
        }

        draw_list(lrows,cols,sel,scroll);
        if(comp)draw_compose(rows,cols,nsubj,nbody,nfield);

        ui_footbar(rows-1,cols,NULL);
        doupdate();

        int ch=ui_key_timeout(2);
        if(ch==ERR)continue;

        if(srching){
            if(ch==27||ch=='\n'||ch==KEY_ENTER){if(ch==27){srch[0]=0;build_filter("");}srching=0;sel=0;scroll=0;}
            else if(ch==KEY_BACKSPACE||ch==127){int l=(int)strlen(srch);if(l>0)srch[l-1]=0;build_filter(srch);sel=0;scroll=0;}
            else if(ch>=32&&ch<127){int l=(int)strlen(srch);if(l<63){srch[l]=ch;srch[l+1]=0;}build_filter(srch);sel=0;scroll=0;}
            continue;
        }

        if(comp){
            char*tgt=nfield==0?nsubj:nbody;
            int mx=nfield==0?MAX_SUBJ-1:MAX_BODY-1;
            switch(ch){
                case 27:comp=0;memset(nsubj,0,sizeof(nsubj));memset(nbody,0,sizeof(nbody));nfield=0;ncur=0;break;
                case'\t':nfield=1-nfield;ncur=(int)strlen(nfield==0?nsubj:nbody);break;
                case'\n':case KEY_ENTER:
                    if(nbody[0]){
                        char sb[MAX_BODY*2],ss[MAX_SUBJ*2];
                        int bi=0,si=0;
                        for(int x=0;nbody[x]&&bi<(int)sizeof(sb)-2;x++){if(nbody[x]=='\'')sb[bi++]='\\';sb[bi++]=nbody[x];}sb[bi]=0;
                        for(int x=0;nsubj[x]&&si<(int)sizeof(ss)-2;x++){if(nsubj[x]=='\'')ss[si++]='\\';ss[si++]=nsubj[x];}ss[si]=0;
                        char cmd[MAX_BODY+256];
                        snprintf(cmd,sizeof(cmd),
                            "pyon-core post --board '%s' --body '%s' --subject '%s' 2>/dev/null"
                            " || txtboard-core post --board '%s' --body '%s' --subject '%s' 2>/dev/null",
                            b->slug,sb,nsubj[0]?ss:"",b->slug,sb,nsubj[0]?ss:"");
                        if(system(cmd)==0){
                            add_post(post_n+1,cfg->my_name[0]?cfg->my_name:"você",nsubj[0]?nsubj:NULL,nbody,0,NULL);
                            build_filter(srch);sel=flt_n-1;scroll=0;
                        }
                    }
                    comp=0;memset(nsubj,0,sizeof(nsubj));memset(nbody,0,sizeof(nbody));nfield=0;ncur=0;break;
                case KEY_BACKSPACE:case 127:
                    if(ncur>0){memmove(tgt+ncur-1,tgt+ncur,mx-ncur);tgt[mx]=0;ncur--;}break;
                default:
                    if(ch>=32&&ch<127&&ncur<mx){memmove(tgt+ncur+1,tgt+ncur,mx-ncur-1);tgt[ncur++]=(char)ch;}break;
            }
            continue;
        }

        switch(ch){
            case KEY_UP:case'k':
                if(sel>0){sel--;if(sel<scroll)scroll=sel;}break;
            case KEY_DOWN:case'j':
                if(sel<flt_n-1){sel++;if(sel-scroll>=(lrows-4)/3)scroll++;}break;
            case KEY_PPAGE:scroll-=lrows/2;if(scroll<0)scroll=0;break;
            case KEY_NPAGE:scroll+=lrows/2;if(scroll>flt_n-1)scroll=flt_n-1;break;
            case'\n':case KEY_ENTER:
                /* abre thread como página separada */
                if(flt_n>0&&sel<flt_n){
                    int pid=posts[flt[sel]].id;
                    /* sobe para a raiz da thread */
                    while(1){
                        Post*pp=find_post(pid);
                        if(!pp||pp->reply_to==0)break;
                        pid=pp->reply_to;
                    }
                    thread_view_run(b,cfg,pid);
                    /* ao voltar, recarrega */
                    load_ndjson(b->slug);if(!post_n)load_demo(b);
                    build_filter(srch);
                }
                break;
            case'/':srching=1;break;
            case'n':comp=1;nfield=0;ncur=0;break;
            case'r':
                relay_run(cfg->my_name,cfg->my_pubkey,b->slug,cfg->relay_host,cfg->relay_port);
                break;
            case 27:case'q':case'Q':return;
        }
    }
}
