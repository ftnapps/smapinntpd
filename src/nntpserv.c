#include "nntpserv.h"

ulong cfg_port        = CFG_PORT;
ulong cfg_maxconn     = CFG_MAXCONN;

int	num_origins = -1;
uchar *cfg_origin[MAX_NUMBERS_ORIGIN];
uchar *cfg_guestsuffix;
uchar *cfg_echomailjam;
uchar *cfg_exitflag;

uchar *cfg_allowfile  = CFG_ALLOWFILE;
uchar *cfg_groupsfile = CFG_GROUPSFILE;
uchar *cfg_logfile    = CFG_LOGFILE;
uchar *cfg_usersfile  = CFG_USERSFILE;
uchar *cfg_xlatfile   = CFG_XLATFILE;

bool cfg_def_flowed = CFG_DEF_FLOWED;
bool cfg_def_showto = CFG_DEF_SHOWTO;

bool cfg_debug;
bool cfg_noecholog;
bool cfg_nostripre;
bool cfg_noreplyaddr;
bool cfg_notearline;
bool cfg_smartquote;
bool cfg_noencode;
bool cfg_notzutc;
bool cfg_nocancel;
bool cfg_strictnetmail;
bool cfg_readorigin;

int server_openconnections;
int server_quit;

ulong lastmsgidnum;

int get_server_openconnections(void)
{
   int res;

   os_getexclusive();
   res=server_openconnections;
   os_stopexclusive();

   return(res);
}

int get_server_quit(void)
{
   int res;

   os_getexclusive();
   res=server_quit;
   os_stopexclusive();

   return(res);
}

ulong get_msgid_num(void)
{
   ulong msgidnum;

   os_getexclusive();

   msgidnum=(time(NULL)/10)*10;

   if(msgidnum <= lastmsgidnum)
      msgidnum = lastmsgidnum+1;

   lastmsgidnum=msgidnum;

   os_stopexclusive();

   return(msgidnum);
}

uchar *parseinput(struct var *var)
{
   long s;

   /* Skip whitespace */
   while(var->input[var->inputpos]==' ')
      var->inputpos++;

   s=var->inputpos;

   if(var->input[var->inputpos] == 0)
      return(NULL);

   while(var->input[var->inputpos]!=' ' && var->input[var->inputpos]!=0)
      var->inputpos++;

   if(var->input[var->inputpos] == ' ')
      var->input[var->inputpos++]=0;

   return(&var->input[s]);
}

bool smapiopenarea(struct var *var,struct group *group)
{
   ushort type;
   uchar *realname;
   uchar *typename;
   
   if(group == var->opengroup)
      return(TRUE);

   if(var->openarea)
   {
      MsgCloseArea(var->openarea);
      var->openarea=NULL;
      var->opengroup=NULL;
   }

   if(group->jampath[0] == '!')
   {
      realname=&group->jampath[1];
      typename="JAM";
      type=MSGTYPE_JAM;
   }
   else if(group->jampath[0] == '$')
   {
      realname=&group->jampath[1];
      typename="Squish";
      type=MSGTYPE_SQUISH;
   }
   else if(group->jampath[0] == '#')
   {
      realname=&group->jampath[1];
      typename="*.msg";
      type=MSGTYPE_SDM;
   }
   else
   {
      realname=group->jampath;
      typename="JAM";
      type=MSGTYPE_JAM;
   }
   
   if(!group->local && !group->netmail)
      type |= MSGTYPE_ECHO;
      
   if(!(var->openarea=MsgOpenArea(realname,MSGAREA_NORMAL,type)))
   {
      os_logwrite("(%s) Failed to open %s messagebase \"%s\"",var->clientid,typename,realname);
      var->openarea=NULL;
      var->opengroup=NULL;
      return(FALSE);
   }

   var->opengroup=group;

   return(TRUE);
}

bool smapigetminmaxnum(struct var *var,struct group *group,ulong *min,ulong *max,ulong *num)
{
   if(!(smapiopenarea(var,group)))
      return(FALSE);

   *num=MsgGetNumMsg(var->openarea);
   
   if(*num == 0)
   {
      *min=0;
      *max=0;
      
      return(TRUE);
   }
   
   *min=MsgMsgnToUid(var->openarea,1);
   *max=MsgMsgnToUid(var->openarea,*num);
   
   if(group->netmail && cfg_strictnetmail)
   {
      uchar fromname[100],toname[100],chrs[20],codepage[20];
      uchar *ctrl,*str,*xlatres;
      struct xlat *xlat;
      ulong netmin,netmax,netnum,c,ctrllen;
      HMSG msg;
      XMSG xmsg;
            
      netmin=0;
      netmax=0;
      netnum=0;

      for(c=1;c<=*num && !var->disconnect && !get_server_quit();c++)
      {
         if((msg=MsgOpenMsg(var->openarea,MOPEN_READ,c)))
         {
            ctrllen=MsgGetCtrlLen(msg);
            ctrl=malloc(ctrllen+1);
            
            if(ctrl)
            {
               if(MsgReadMsg(msg,&xmsg,0,0,NULL,ctrllen,ctrl) != -1)
               {
                  mystrncpy(fromname,xmsg.from,100); 
                  mystrncpy(toname,xmsg.to,100); 

                  chrs[0]=0;
                  codepage[0]=0;
               
                  ctrl[ctrllen]=0;

                  if((str=MsgGetCtrlToken(ctrl,"CHRS")))
                  {
                     mystrncpy(chrs,getkludgedata(str),20);
                     free(str);
                     stripchrs(chrs);
                  }
   
                  if((str=MsgGetCtrlToken(ctrl,"CHARSET")))
                  {
                     mystrncpy(chrs,getkludgedata(str),20);
                     free(str);
                  }
      
                  if((str=MsgGetCtrlToken(ctrl,"CODEPAGE")))
                  {
                     mystrncpy(codepage,getkludgedata(str),20);
                     free(str);
                  }
            
                  xlat=findreadxlat(var,group,chrs,codepage,NULL);

                  if(xlat && xlat->xlattab)
                  {
                     if((xlatres=xlatstr(fromname,xlat->xlattab)))
                     {
                        mystrncpy(fromname,xlatres,100);
                        free(xlatres);
                     }
   
                     if((xlatres=xlatstr(toname,xlat->xlattab)))
                     {
                        mystrncpy(toname,xlatres,100);
                        free(xlatres);
                     }
                  }
            
                  if(matchname(var->realnames,fromname) || matchname(var->realnames,toname)) 
                  {
                     if(netmin == 0)
                        netmin=c;
                     
                     netmax=c;
                     netnum++;
                  }
               }
               
               free(ctrl);
            }
         }
         MsgCloseMsg(msg);
      }
   
      *min=MsgMsgnToUid(var->openarea,netmin);
      *max=MsgMsgnToUid(var->openarea,netmax);
      *num=netnum;
   }
      
   return(TRUE);
}

void command_list(struct var *var)
{
   struct group *g;
   ulong min,max,num;
   uchar *arg;
   bool listnewsgroups;
   
   listnewsgroups=FALSE;
   
   arg=parseinput(var);
   
   if(arg)
   {
      if(stricmp(arg,"overview.fmt") == 0)
      {
         socksendtext(var,"215 List of fields in XOVER result" CRLF);
         socksendtext(var,"Subject:" CRLF);
         socksendtext(var,"From:" CRLF);
         socksendtext(var,"Date:" CRLF);
         socksendtext(var,"Message-ID:" CRLF);
         socksendtext(var,"References:" CRLF);
         socksendtext(var,"Bytes:" CRLF),
         socksendtext(var,"Lines:" CRLF);
         socksendtext(var,"." CRLF);

         return;
      }
      else if(stricmp(arg,"newsgroups") == 0)
      {
         if(parseinput(var))
         {
            socksendtext(var,"501 Patterns not supported for LIST NEWSGROUPS" CRLF);
            return;
         }
         
         listnewsgroups=TRUE;
      }
      else if(stricmp(arg,"active") != 0)
      {
         socksendtext(var,"501 Unknown argument for LIST command" CRLF);
         return;
      }
   }

   socksendtext(var,"215 List of newsgroups follows" CRLF);

   for(g=var->firstgroup;g && !var->disconnect && !get_server_quit();g=g->next)
   {
      if(matchgroup(var->readgroups,g->group))
      {
         if(listnewsgroups)
         {
            sockprintf(var,"%s\t" CRLF,g->tagname);
         }
         else
         {
            if(!smapigetminmaxnum(var,g,&min,&max,&num))
            {
               min=0;
               max=0;
               num=0;
            }

            if(matchgroup(var->postgroups,g->group))
               sockprintf(var,"%s %lu %lu y" CRLF,g->tagname,min,max);

            else
               sockprintf(var,"%s %lu %lu n" CRLF,g->tagname,min,max);
         }
      }
   }

   socksendtext(var,"." CRLF);
}

void command_group(struct var *var)
{
   uchar *groupname;
   struct group *g;
   ulong min,max,num;

   if(!(groupname=parseinput(var)))
   {
      socksendtext(var,"501 No group specified" CRLF);
      return;
   }

   for(g=var->firstgroup;g;g=g->next)
      if(matchgroup(var->readgroups,g->group) && stricmp(g->tagname,groupname)==0) break;

   if(!g)
   {
      socksendtext(var,"411 No such newsgroup" CRLF);
      return;
   }

   if(!smapigetminmaxnum(var,g,&min,&max,&num))
   {
      socksendtext(var,"503 Local error: Could not get size of messagebase" CRLF);
      return;
   }

   var->currentgroup=g;
   var->currentarticle=min;

   sockprintf(var,"211 %lu %lu %lu %s Group selected" CRLF,num,min,max,g->tagname);
}

void command_next(struct var *var)
{
   ulong min,max,num;

   if(!var->currentgroup)
   {
      socksendtext(var,"412 No newsgroup selected" CRLF);
      return;
   }

   if(!var->currentarticle)
   {
      socksendtext(var,"420 No current article has been selected" CRLF);
      return;
   }

   if(!smapigetminmaxnum(var,var->currentgroup,&min,&max,&num))
   {
      socksendtext(var,"503 Local error: Could not get size of messagebase" CRLF);
      return;
   }

   if(var->currentarticle+1 > max)
   {
      socksendtext(var,"421 No next article in this group" CRLF);
      return;
   }

   var->currentarticle++;

   sockprintf(var,"223 %lu <%lu$%s@SmapiNNTPd> Article retrieved" CRLF,
      var->currentarticle,var->currentarticle,var->currentgroup->tagname);
}

void command_last(struct var *var)
{
   ulong min,max,num;

   if(!var->currentgroup)
   {
      socksendtext(var,"412 No newsgroup selected" CRLF);
      return;
   }

   if(!var->currentarticle)
   {
      socksendtext(var,"420 No current article has been selected" CRLF);
      return;
   }

   if(!smapigetminmaxnum(var,var->currentgroup,&min,&max,&num))
   {
      socksendtext(var,"503 Local error: Could not get size of messagebase" CRLF);
      return;
   }

   if(var->currentarticle-1 < min)
   {
      socksendtext(var,"422 No previous article in this group" CRLF);
      return;
   }

   var->currentarticle--;

   sockprintf(var,"223 %lu <%lu$%s@SmapiNNTPd> Article retrieved" CRLF,
      var->currentarticle,var->currentarticle,var->currentgroup->tagname);
}

struct attributename
{
   ulong attr;
   uchar *name;
};

struct attributename attributenames[] =
{ { MSGPRIVATE,     "Private"      },
  { MSGCRASH,       "Crash"        },
  { MSGREAD,        "Read"         },
  { MSGSENT,        "Sent"         },
  { MSGFILE,        "File"         },
  { MSGFWD,         "Fwd"          },
  { MSGORPHAN,      "Orphan"       },
  { MSGKILL,        "Kill"         },
  { MSGLOCAL,       "Local"        },
  { MSGHOLD,        "Hold"         },
  { MSGXX2,         "Xx2"          },
  { MSGFRQ,         "Frq"          },
  { MSGRRQ,         "Rrq"          },
  { MSGCPT,         "Cpt"          },
  { MSGARQ,         "Arq"          },
  { MSGURQ,         "Urq"          },
  { MSGSCANNED,     "Scanned"      },
  { MSGUID,         "Uid"          },
  { MSGIMM,         "Imm"          },
  { MSGLOCKED,      "Locked"       },
  { 0,               NULL           } };

#define WRAP_WIDTH 72
#define LINE_WIDTH 79
#define MAX_WIDTH 997

void copyline(uchar *dest,uchar *src,long len)
{
   int d,c;

   d=0;

   for(c=0;c<len;c++)
     if(src[c] != 10) dest[d++]=src[c];

   dest[d]=0;
}

void sendtextblock(struct var *var,uchar *text,struct xlat *xlat)
{
   long c,d,textpos,lastspace;
   uchar buf[1000],buf2[1000],*xlatres;
   bool wrapped;

   textpos=0;

   while(text[textpos]!=0 && !var->disconnect && !get_server_quit())
   {
      lastspace=0;
      c=0;
      wrapped=FALSE;

      /* Find last space before WRAP_WIDTH */

      while(c<=WRAP_WIDTH && text[textpos+c]!=0 && text[textpos+c]!=13)
      {
         if(text[textpos+c]==32) lastspace=c;
         c++;
      }

      /* It might not be necessary to wrap after all if we find EOL before LINE_WIDTH */

      if(text[textpos+c]!=13 && text[textpos+c]!=0)
      {
         d=c+1;

         while(text[textpos+d]!=0 && text[textpos+d]!=13 && d<LINE_WIDTH)
            d++;

         if(text[textpos+d] == 13 || text[textpos+d] == 0)
            c=d;
      }

      if(text[textpos+c] == 13 || text[textpos+c] == 0)
      {
         /* EOL found */

         copyline(buf,&text[textpos],c);
         if(text[textpos+c]==13) c++;
         textpos+=c;
      }
      else if(lastspace)
      {
         /* Wrap */

         copyline(buf,&text[textpos],lastspace);
         textpos+=lastspace+1;
         wrapped=TRUE;
      }
      else
      {
         /* Just one looong word */

         while(text[textpos+c] != 0 && text[textpos+c] != 13 && text[textpos+c] != 32 && c<MAX_WIDTH)
            c++;

         
         copyline(buf,&text[textpos],c);
         
         if(text[textpos+c] == 32)
            wrapped=TRUE;
         
         if(text[textpos+c] == 32 || text[textpos+c] == 13) 
            c++;
         
         textpos+=c;
      }

      /* Code for format=flowed */

      if(var->opt_flowed && strcmp(buf,"-- ")!=0)
      {
         if(wrapped) strcat(buf," "); /* For format=flowed */
         else        strip(buf);
         
         if(buf[0] == ' ' || strncmp(buf,"From ",5)==0)
         {
            strcpy(buf2,buf);
            strcpy(buf," ");
            strcat(buf,buf2);
         }
      }

      /* End format=flowed */

      if(stricmp(buf,".")==0) /* "." means end of message in NNTP */
         strcpy(buf,"..");

      strcat(buf,CRLF);

      if(xlat && xlat->xlattab)
      {
         if((xlatres=xlatstr(buf,xlat->xlattab)))
         {
            socksendtext(var,xlatres);
            free(xlatres);
         }
      }
      else
      {
         socksendtext(var,buf);
      }   
   }
}

void command_abhs(struct var *var,uchar *cmd)
{
   uchar *article;
   ulong articlenum,smapinum,ctrllen,textlen;
   struct group *group;
   ulong min,max,num,c,d,e;
   uchar fromaddr[100],toaddr[100],replyaddr[100],fromname[100],toname[100],subject[100];
   uchar chrs[20],codepage[20],encoding[20],format[20],timezone[20];
   uchar dispname[210],dispfromname[100],disptoname[100],dispaddr[100],buf[200];
   uchar *at,*pc;
   uchar *text,*ctrl,*str,*xlatres;
   struct xlat *xlat;
   HMSG msg;
   XMSG xmsg;

   article=parseinput(var);

   if(!article)
   {
      if(!var->currentgroup)
      {
         socksendtext(var,"412 No newsgroup selected" CRLF);
         return;
      }

      if(!var->currentarticle)
      {
         socksendtext(var,"420 No current article has been selected" CRLF);
         return;
      }

      articlenum=var->currentarticle;
      group=var->currentgroup;
   }
   else if(article[0] == '<' && article[strlen(article)-1] == '>')
   {
      strcpy(article,&article[1]);
      article[strlen(article)-1]=0;

      at=strchr(article,'@');
      pc=strchr(article,'$');

      if(!at || !pc)
      {
         socksendtext(var,"430 No such article found" CRLF);
         return;
      }

      *at=0;
      *pc=0;

      at++;
      pc++;

      if(strcmp(at,"SmapiNNTPd") != 0)
      {
         socksendtext(var,"430 No such article found" CRLF);
         return;
      }

      for(group=var->firstgroup;group;group=group->next)
         if(matchgroup(var->readgroups,group->group) && stricmp(pc,group->tagname) == 0) break;

      if(!group)
      {
         socksendtext(var,"430 No such article found" CRLF);
         return;
      }

      articlenum=atol(article);

      if(!smapiopenarea(var,group))
      {
         socksendtext(var,"503 Local error: Could not open messagebase" CRLF);
         return;
      }

      if(!smapigetminmaxnum(var,var->currentgroup,&min,&max,&num))
      {
         socksendtext(var,"503 Local error: Could not get size of messagebase" CRLF);
         return;
      }

      if(articlenum < min || articlenum > max)
      {
         socksendtext(var,"430 No such article found" CRLF);
         return;
      }
   }
   else if(atol(article) > 0)
   {
      if(!var->currentgroup)
      {
         socksendtext(var,"412 No newsgroup selected" CRLF);
         return;
      }
      
      articlenum=atol(article);
      group=var->currentgroup;

      if(!smapigetminmaxnum(var,var->currentgroup,&min,&max,&num))
      {
         socksendtext(var,"503 Local error: Could not get size of messagebase" CRLF);
         return;
      }

      if(articlenum < min || articlenum > max)
      {
         socksendtext(var,"423 No such article number in this group" CRLF);
         return;
      }

      var->currentarticle = articlenum;
   }
   else
   {
      socksendtext(var,"501 Invalid article number specified" CRLF);
      return;
   }

   if(stricmp(cmd,"STAT") == 0)
   {
      sockprintf(var,"223 %lu <%lu$%s@SmapiNNTPd> Article retrieved" CRLF,
         articlenum,articlenum,group->tagname);

      return;
   }

   if(!smapiopenarea(var,group))
   {
      socksendtext(var,"503 Local error: Could not open messagebase" CRLF);
      return;
   }

   if(!(smapinum=MsgUidToMsgn(var->openarea,articlenum,UID_EXACT)))
   {
      socksendtext(var,"503 Local error: Could not find message" CRLF);       
      return;
   }
   
   if(!(msg=MsgOpenMsg(var->openarea,MOPEN_READ,smapinum)))
   {
      os_logwrite("(%s) Could not open message %lu in \"%s\"",var->clientid,smapinum,var->opengroup->jampath);
      socksendtext(var,"503 Local error: Could not open message" CRLF);
      return;
   }
         
   textlen=MsgGetTextLen(msg);
   ctrllen=MsgGetCtrlLen(msg);

   text=malloc(textlen+1);
   ctrl=malloc(ctrllen+1);
   
   if(!text || !ctrl)
   {
      os_logwrite("(%s) Out of memory",var->clientid);
      socksendtext(var,"503 Local error: Out of memory" CRLF);
      
      if(text) free(text);
      if(ctrl) free(ctrl);
      MsgCloseMsg(msg);
      
      return;
   }
   
   if(MsgReadMsg(msg,&xmsg,0,textlen,text,ctrllen,ctrl) == -1)
   {
      os_logwrite("(%s) Could not read message %lu in \"%s\"",var->clientid,smapinum,var->opengroup->jampath);
      socksendtext(var,"503 Local error: Could not read message" CRLF);
      
      free(text);
      free(ctrl);
      MsgCloseMsg(msg);
      
      return;
   }

   text[textlen]=0;
   ctrl[ctrllen]=0;
      
   fromname[0]=0;
   fromaddr[0]=0;
   toname[0]=0;
   toaddr[0]=0;
   subject[0]=0;
   replyaddr[0]=0;
   timezone[0]=0;
   chrs[0]=0;
   codepage[0]=0;

   mystrncpy(fromname,xmsg.from,100); 
   mystrncpy(toname,xmsg.to,100); 
   mystrncpy(subject,xmsg.subj,100); 

   if(group->netmail)
   {   
      if(xmsg.orig.point) 
         sprintf(fromaddr,"%u:%u/%u.%u",
            xmsg.orig.zone ? xmsg.orig.zone : atoi(group->aka),
            xmsg.orig.net,
            xmsg.orig.node,
            xmsg.orig.point);
      
      else 
         sprintf(fromaddr,"%u:%u/%u",
            xmsg.orig.zone ? xmsg.orig.zone : atoi(group->aka),
            xmsg.orig.net,
            xmsg.orig.node);

      if(xmsg.dest.point) 
         sprintf(toaddr,"%u:%u/%u.%u",
            xmsg.dest.zone ? xmsg.dest.zone : atoi(group->aka),
            xmsg.dest.net,
            xmsg.dest.node,
            xmsg.dest.point);
            
      else
         sprintf(toaddr,"%u:%u/%u",
            xmsg.dest.zone ? xmsg.dest.zone : atoi(group->aka),
            xmsg.dest.net,
            xmsg.dest.node);
   }
   else if(!group->local)
   {
      extractorigin(text,fromaddr);
   }
   
   if((str=MsgGetCtrlToken(ctrl,"CHRS")))
   {
      mystrncpy(chrs,getkludgedata(str),20);
      free(str);
      stripchrs(chrs);
   }
   
   if((str=MsgGetCtrlToken(ctrl,"CHARSET")))
   {
      mystrncpy(chrs,getkludgedata(str),20);
      free(str);
   }
   
   if((str=MsgGetCtrlToken(ctrl,"CODEPAGE")))
   {
      mystrncpy(codepage,getkludgedata(str),20);
      free(str);
   }
   
   if((str=MsgGetCtrlToken(ctrl,"TZUTC")))
   {
      mystrncpy(timezone,getkludgedata(str),20);
      free(str);
   }

   if((str=MsgGetCtrlToken(ctrl,"TZUTCINFO")))
   {
      mystrncpy(timezone,getkludgedata(str),20);
      free(str);
   }
      
   if((str=MsgGetCtrlToken(ctrl,"REPLYADDR")))
   {
      mystrncpy(replyaddr,getkludgedata(str),100);
      free(str);
   }
   
   if(replyaddr[0])
      stripreplyaddr(replyaddr);
      
   xlat=findreadxlat(var,group,chrs,codepage,NULL);

   if(xlat) strcpy(chrs,xlat->tochrs);
   else     strcpy(chrs,"unknown-8bit");

   if(xlat && xlat->xlattab)
   {
      if((xlatres=xlatstr(fromname,xlat->xlattab)))
      {
         mystrncpy(fromname,xlatres,100);
         free(xlatres);
      }

      if((xlatres=xlatstr(toname,xlat->xlattab)))
      {
         mystrncpy(toname,xlatres,100);
         free(xlatres);
      }

      if((xlatres=xlatstr(subject,xlat->xlattab)))
      {
         mystrncpy(subject,xlatres,100);
         free(xlatres);
      }
   }   

   if(group->netmail)
   {
      if(!matchname(var->realnames,fromname) && !matchname(var->realnames,toname)) 
      {
         socksendtext(var,"503 Access denied" CRLF);

         free(text);
         free(ctrl);
         MsgCloseMsg(msg);

         return;
      }
   }   

   if(!(xlat && xlat->keepsoftcr))
   {
      d=0;
            
      for(c=0;text[c];c++)
         if(text[c] != 0x8d) text[d++]=text[c];
            
      text[d]=0;
   }
      
   if(stricmp(cmd,"ARTICLE")==0)
      sockprintf(var,"220 %ld <%ld$%s@SmapiNNTPd> Article retrieved - Head and body follow" CRLF,articlenum,articlenum,group->tagname);

   if(stricmp(cmd,"HEAD")==0)
      sockprintf(var,"221 %ld <%ld$%s@SmapiNNTPd> Article retrieved - Head follows" CRLF,articlenum,articlenum,group->tagname);

   if(stricmp(cmd,"BODY")==0)
      sockprintf(var,"222 %ld <%ld$%s@SmapiNNTPd> Article retrieved - Body follows" CRLF,articlenum,articlenum,group->tagname);

   if(stricmp(cmd,"ARTICLE") == 0 || stricmp(cmd,"HEAD") == 0)
   {
      if(fromaddr[0] == 0) 
         strcpy(fromaddr,"unknown@unknown");

      if(fromname[0] == 0) strcpy(dispfromname,"unknown");
      else                 strcpy(dispfromname,fromname);

      if(toname[0] == 0) strcpy(disptoname,"(none)");
      else               strcpy(disptoname,toname);
      
      if(var->opt_showto) sprintf(dispname,"%s -> %s",dispfromname,disptoname);
      else                strcpy(dispname,dispfromname);

      if(replyaddr[0]) strcpy(dispaddr,replyaddr);
      else             strcpy(dispaddr,fromaddr);
      
      sockprintf(var,"Path: SmapiNNTPd!not-for-mail" CRLF);

      mimesendheaderline(var,"From",dispname,chrs,dispaddr,cfg_noencode);
      mimesendheaderline(var,"X-Comment-To",toname,chrs,NULL,cfg_noencode);
      sockprintf(var,"Newsgroups: %s" CRLF,group->tagname);
      mimesendheaderline(var,"Subject",subject,chrs,NULL,cfg_noencode);

      makedate(&xmsg.date_written,buf,timezone);
      sockprintf(var,"Date: %s" CRLF,buf); 
      
      sockprintf(var,"Message-ID: <%ld$%s@SmapiNNTPd>" CRLF,articlenum,group->tagname);

      if(xmsg.replyto)

         sockprintf(var,"References: <%ld$%s@SmapiNNTPd>" CRLF,xmsg.replyto,group->tagname);
      
      sprintf(fromaddr,"%u:%u/%u.%u",xmsg.orig.zone,xmsg.orig.net,xmsg.orig.node,xmsg.orig.point);
      sprintf(toaddr,"%u:%u/%u.%u",xmsg.dest.zone,xmsg.dest.net,xmsg.dest.node,xmsg.dest.point);

      /* Dump header */
      
      sockprintf(var,"X-SMAPI-From: %s <%s>" CRLF,fromname,fromaddr);
      sockprintf(var,"X-SMAPI-To: %s <%s>" CRLF,toname,toaddr);

      sc_time((union stamp_combo *)&xmsg.date_written,buf);
      sockprintf(var,"X-SMAPI-DateWritten: %s" CRLF,buf);

      sc_time((union stamp_combo *)&xmsg.date_arrived,buf);
      sockprintf(var,"X-SMAPI-DateArrived: %s" CRLF,buf);

      if(xmsg.attr)
      {
         int c;

         strcpy(buf,"X-SMAPI-Attributes:");

         for(c=0;attributenames[c].name;c++)
            if(xmsg.attr	 & attributenames[c].attr)
            {
               strcat(buf," ");
               strcat(buf,attributenames[c].name);
            }

         strcat(buf,CRLF);
         socksendtext(var,buf);
      }

      /* Dump kludges */
      
      c=0;

      while(ctrl[c])
      {
         while(ctrl[c] == 1) 
            c++;

         d=c;

         while(ctrl[d] != 0 && ctrl[d] != 1) 
            d++;

         if(ctrl[d] == 1)
            ctrl[d++] = 0;

         sockprintf(var,"X-SMAPI-Control: @%.100s" CRLF,&ctrl[c]);

         c=d;
      }

      /* Dump SEEN-BY and any other kludges, remove them from rest of the text */

      c=0;
      e=0;
      
      while(text[c])
      {
         if(strncmp(&text[c],"SEEN-BY:",8) == 0)
         {
            d=c;

            while(text[d] != 0 && text[d] != 13) 
               d++;
            
            if(text[d] == 13)
               text[d++] = 0;

            sockprintf(var,"X-SMAPI-Control: %.100s" CRLF,&text[c]);
               
            c=d;
         }
         else if(text[c] == 1)
         {
            c++;

            d=c;

            while(text[d] != 0 && text[d] != 13) 
               d++;
            
            if(text[d] == 13)
               text[d++] = 0;

            sockprintf(var,"X-SMAPI-Control: @%.100s" CRLF,&text[c]);
               
            c=d;
         }
         else
         {
            while(text[c] != 0 && text[c] != 13) 
               text[e++]=text[c++];
               
            if(text[c] == 13) 
               text[e++]=text[c++];
          }
      }
      
      text[e]=0;
      
      /* MIME headers */

      socksendtext(var,"MIME-Version: 1.0" CRLF);

      if(count8bit(text))
      {
         strcpy(encoding,"8bit");
      }
      else
      {
         strcpy(encoding,"7bit");
         strcpy(chrs,"us-ascii");
      }

      if(var->opt_flowed)
         strcpy(format,"flowed");

      else
         strcpy(format,"fixed");

      sockprintf(var,"Content-Type: text/plain; charset=%s; format=%s" CRLF,chrs,format);
      sockprintf(var,"Content-Transfer-Encoding: %s" CRLF,encoding);
   }

   if(stricmp(cmd,"ARTICLE") == 0)
      socksendtext(var,CRLF);

   if(stricmp(cmd,"ARTICLE") == 0 || stricmp(cmd,"BODY") == 0)
      sendtextblock(var,text,xlat);
   
   socksendtext(var,"." CRLF);

   free(text);
   free(ctrl);
   MsgCloseMsg(msg);
}

void command_xover(struct var *var)
{
   uchar *article,*dash;
   ulong min,max,num,smapinum,ctrllen,textlen;
   ulong first,last,c;
   uchar msgid[150],reply[150],chrs[20],codepage[20],datebuf[50],timezone[20];
   uchar fromname[100],toname[100],fromaddr[100],subject[100],replyaddr[100];
   uchar mimefrom[1000],mimesubj[1000],xoverres[2500];
   uchar dispname[210],dispfromname[100],disptoname[100],dispaddr[100];
   struct xlat *xlat;
   uchar *xlatres,*str,*ctrl,*text;
   bool access;
   HMSG msg;
   XMSG xmsg;
   
   if(!var->currentgroup)
   {
      socksendtext(var,"412 No newsgroup selected" CRLF);
      return;
   }

   smapigetminmaxnum(var,var->currentgroup,&min,&max,&num);

   article=parseinput(var);

   if(!article)
   {
      if(!var->currentarticle)
      {
         socksendtext(var,"420 No current article has been selected" CRLF);
         return;
      }

      first=var->currentarticle;
      last=var->currentarticle;
   }
   else
   {
      dash=strchr(article,'-');

      if(dash)
      {
         *dash=0;
         dash++;

         first=atol(article);

         if(dash[0] == 0)
            last=max;

         else
            last=atol(dash);
      }
      else
      {
         first=atol(article);
         last=atol(article);
      }
   }

   if(first < min) first=min;
   if(last > max) last=max;

   if(first > last || num == 0)
   {
      socksendtext(var,"420 No articles found in this range" CRLF);
      return;
   }

   if(!smapiopenarea(var,var->currentgroup))
   {
      socksendtext(var,"503 Local error: Could not open messagebase" CRLF);
      return;
   }

   socksendtext(var,"224 Overview information follows" CRLF);

   for(c=first;c<=last && !var->disconnect && !get_server_quit();c++)
   {
      if((smapinum=MsgUidToMsgn(var->openarea,c,UID_EXACT)))
      {
         if((msg=MsgOpenMsg(var->openarea,MOPEN_READ,smapinum)))
         {
            ctrllen=MsgGetCtrlLen(msg);
            textlen=MsgGetTextLen(msg);
         
            ctrl=malloc(ctrllen+1);
            text=malloc(textlen+1);
            
            if(ctrl && text)
            {
               if(MsgReadMsg(msg,&xmsg,0,textlen,text,ctrllen,ctrl) != -1)
               {
                  ctrl[ctrllen]=0;
                  text[textlen]=0;
                     
                  fromname[0]=0;
                  fromaddr[0]=0;
                  toname[0]=0;
                  subject[0]=0;
                  replyaddr[0]=0;
                  timezone[0]=0;
                  chrs[0]=0;
                  codepage[0]=0;
               
                  mystrncpy(fromname,xmsg.from,100); 
                  mystrncpy(toname,xmsg.to,100); 
                  mystrncpy(subject,xmsg.subj,100); 
               
                  if(var->currentgroup->netmail)
                  {   
                     if(xmsg.orig.point) 
                        sprintf(fromaddr,"%u:%u/%u.%u",
                           xmsg.orig.zone ? xmsg.orig.zone : atoi(var->currentgroup->aka),
                           xmsg.orig.net,
                           xmsg.orig.node,
                           xmsg.orig.point);
                           
                     else
                        sprintf(fromaddr,"%u:%u/%u",
                           xmsg.orig.zone ? xmsg.orig.zone : atoi(var->currentgroup->aka),
                           xmsg.orig.net,
                           xmsg.orig.node);
                  }
                  else if(!var->currentgroup->local)
                  {
                     extractorigin(text,fromaddr);
                  }
                  
                  if((str=MsgGetCtrlToken(ctrl,"CHRS")))
                  {
                     mystrncpy(chrs,getkludgedata(str),20);
                     free(str);
                     stripchrs(chrs);
                  }
                  
                  if((str=MsgGetCtrlToken(ctrl,"CHARSET")))
                  {
                     mystrncpy(chrs,getkludgedata(str),20);
                     free(str);
                  }
                  
                  if((str=MsgGetCtrlToken(ctrl,"CODEPAGE")))
                  {
                     mystrncpy(codepage,getkludgedata(str),20);
                     free(str);
                  }
                  
                  if((str=MsgGetCtrlToken(ctrl,"TZUTC")))
                  {
                     mystrncpy(timezone,getkludgedata(str),20);
                     free(str);
                  }
               
                  if((str=MsgGetCtrlToken(ctrl,"TZUTCINFO")))
                  {
                     mystrncpy(timezone,getkludgedata(str),20);
                     free(str);
                  }
                     
                  if((str=MsgGetCtrlToken(ctrl,"REPLYADDR")))
                  {
                     mystrncpy(replyaddr,getkludgedata(str),100);
                     free(str);
                  }
                  
                  if(replyaddr[0])
                     stripreplyaddr(replyaddr);
                     
                  xlat=findreadxlat(var,var->currentgroup,chrs,codepage,NULL);
               
                  if(xlat) strcpy(chrs,xlat->tochrs);
                  else     strcpy(chrs,"unknown-8bit");
               
                  if(xlat && xlat->xlattab)
                  {
                     if((xlatres=xlatstr(fromname,xlat->xlattab)))
                     {
                        mystrncpy(fromname,xlatres,100);
                        free(xlatres);
                     }
               
                     if((xlatres=xlatstr(toname,xlat->xlattab)))
                     {
                        mystrncpy(toname,xlatres,100);
                        free(xlatres);
                     }
               
                     if((xlatres=xlatstr(subject,xlat->xlattab)))
                     {
                        mystrncpy(subject,xlatres,100);
                        free(xlatres);
                     }
                  }         
                  
                  access=TRUE;
            
                  if(var->currentgroup->netmail)
                  {
                     if(!matchname(var->realnames,fromname) && !matchname(var->realnames,toname)) 
                        access=FALSE;
                  }
            
                  if(access)
                  {
                     if(fromaddr[0] == 0) 
                        strcpy(fromaddr,"unknown@unknown");

                     if(fromname[0] == 0) strcpy(dispfromname,"unknown");
                     else                 strcpy(dispfromname,fromname);
               
                     if(toname[0] == 0) strcpy(disptoname,"(none)");
                     else               strcpy(disptoname,toname);
                     
                     if(var->opt_showto) sprintf(dispname,"%s -> %s",dispfromname,disptoname);
                     else                strcpy(dispname,dispfromname);
               
                     if(replyaddr[0]) strcpy(dispaddr,replyaddr);
                     else             strcpy(dispaddr,fromaddr);
                  
                     makedate(&xmsg.date_written,datebuf,timezone);
                  
                     sprintf(msgid,"<%ld$%s@SmapiNNTPd>",c,var->currentgroup->tagname);
      
                     reply[0]=0;
      
                     if(xmsg.replyto)
                        sprintf(reply,"<%ld$%s@SmapiNNTPd>",xmsg.replyto,var->currentgroup->tagname);
      
                     mimemakeheaderline(mimefrom,1000,"From",dispname,chrs,dispaddr,cfg_noencode);
                     mimemakeheaderline(mimesubj,1000,"Subject",subject,chrs,NULL,cfg_noencode);
      
                     strcpy(mimefrom,&mimefrom[6]);
                     strcpy(mimesubj,&mimesubj[9]);
      
                     stripctrl(mimesubj);
                     stripctrl(mimefrom);
      
                     sprintf(xoverres,"%ld\t%s\t%s\t%s\t%s\t%s\t\t" CRLF,
                           c,mimesubj,mimefrom,datebuf,msgid,reply);
      
                     socksendtext(var,xoverres);
                  }
               }
            }      
            
            if(ctrl) free(ctrl);               
            if(text) free(text);               
            
            MsgCloseMsg(msg);
         }      
      }
   }
   
   socksendtext(var,"." CRLF);
}

#define POST_MAXSIZE 20000

bool getcontenttypepart(uchar *line,ulong *pos,uchar *dest,ulong destlen)
{
   bool quote;
   ulong c,d;

   quote=FALSE;
   c=*pos;
   d=0;

   /* Skip initial whitespace */

   while(isspace(line[c]))
      c++;

   /* Check if there is anything to copy */

   if(line[c] == 0)
   {
      *pos=c;
      return(FALSE);
   }

   /* Copy until ; or 0. Ignore unquoted whitespace */

   for(;;)
   {
      if(line[c] == '"')
      {
         if(quote) quote=FALSE;
         else      quote=TRUE;
      }
      else if(line[c] == 0 || (line[c] == ';' && !quote))
      {
         if(line[c] != 0) c++;
         *pos=c;
         dest[d]=0;
         return(TRUE);
      }
      else if(quote || !isspace(line[c]))
      {
         if(d < destlen-1)
            dest[d++]=line[c];
      }

      c++;
   }
}

void unmimecpy(uchar *dest,uchar *src,ulong destlen,uchar *chrs,uchar *chrs2,ulong chrslen)
{
   unmime(src,chrs,chrs2,chrslen);
   mystrncpy(dest,src,destlen);
}

void unbackslashquote(uchar *text)
{
   int c,d;

   d=0;

   for(c=0;text[c];c++)
   {
      if(text[c] == '\\' && text[c+1] != 0)
         c++;

      text[d++]=text[c];
   }

   text[d]=0;
}

void getparentinfo(struct var *var,uchar *article,uchar *currentgroup,uchar *msgid,uchar *fromname,uchar *fromaddr,ulong *msgnum,struct xlat *postxlat,bool readorigin)
{
   uchar *at,*pc;
   struct group *group;
   uchar *xlatres,*text,*ctrl,*str;
   ulong articlenum,smapinum,ctrllen,textlen;
   uchar jam_fromname[100],jam_fromaddr[100],jam_toname[100],jam_msgid[100],jam_chrs[20],jam_codepage[20];
   struct xlat *xlat;
   HMSG msg;
   XMSG xmsg;
    
   msgid[0]=0;
   fromname[0]=0;
   fromaddr[0]=0;
   *msgnum=0;
   
   /* Parse messageid */
   
   if(article[0] != '<' || article[strlen(article)-1] != '>')
      return;

   strcpy(article,&article[1]);
   article[strlen(article)-1]=0;

   at=strchr(article,'@');
   pc=strchr(article,'$');

   if(!at || !pc)
      return;

   *at=0;
   *pc=0;

   at++;
   pc++;

   if(strcmp(at,"SmapiNNTPd") != 0)
      return;

   /* Find group */
   
   for(group=var->firstgroup;group;group=group->next)
      if(matchgroup(var->readgroups,group->group) && stricmp(pc,group->tagname) == 0) break;

   if(!group)
      return;

   articlenum=atol(article);

   /* Read parent message */
   
   if(!smapiopenarea(var,group))
      return;

   if(!(smapinum=MsgUidToMsgn(var->openarea,articlenum,UID_EXACT)))
      return;
      
   if(!(msg=MsgOpenMsg(var->openarea,MOPEN_READ,smapinum)))
   {
      os_logwrite("(%s) Could not open message %lu in \"%s\"",var->clientid,smapinum,var->opengroup->jampath);
      socksendtext(var,"503 Local error: Could not open message" CRLF);
      return;
   }
         
   textlen=MsgGetTextLen(msg);
   ctrllen=MsgGetCtrlLen(msg);

   text=malloc(textlen+1);
   ctrl=malloc(ctrllen+1);
   
   if(!text || !ctrl)
   {
      if(text) free(text);
      if(ctrl) free(ctrl);
      MsgCloseMsg(msg);
      return;
   }
   
   if(MsgReadMsg(msg,&xmsg,0,textlen,text,ctrllen,ctrl) == -1)
   {
      os_logwrite("(%s) Could not read message %lu in \"%s\"",var->clientid,smapinum,var->opengroup->jampath);
      socksendtext(var,"503 Local error: Could not read message" CRLF);
      
      free(text);
      free(ctrl);
      MsgCloseMsg(msg);
      
      return;
   }

   text[textlen]=0;
   ctrl[ctrllen]=0;
      
   if(stricmp(pc,currentgroup) == 0) 
      *msgnum=articlenum;   /* Only set parent message number if in the same group */

   jam_msgid[0]=0;
   jam_chrs[0]=0;
   jam_codepage[0]=0;   

   mystrncpy(jam_fromname,xmsg.from,100); 
   mystrncpy(jam_toname,xmsg.to,100); 

   if(group->netmail)
   {   
      if(xmsg.orig.point) 
         sprintf(jam_fromaddr,"%u:%u/%u.%u",
            xmsg.orig.zone ? xmsg.orig.zone : atoi(group->aka),
            xmsg.orig.net,
            xmsg.orig.node,
            xmsg.orig.point);
      
      else 
         sprintf(jam_fromaddr,"%u:%u/%u",
            xmsg.orig.zone ? xmsg.orig.zone : atoi(group->aka),
            xmsg.orig.net,
            xmsg.orig.node);
   }
   else if(!group->local)
   {
      extractorigin(text,fromaddr);
   }
   else
   {
      strcpy(jam_fromaddr,"unknown@unknown");
   }            
   
   if((str=MsgGetCtrlToken(ctrl,"CHRS")))
   {
      mystrncpy(jam_chrs,getkludgedata(str),20);
      free(str);
      stripchrs(jam_chrs);
   }
   
   if((str=MsgGetCtrlToken(ctrl,"CHARSET")))
   {
      mystrncpy(jam_chrs,getkludgedata(str),20);
      free(str);
   }
   
   if((str=MsgGetCtrlToken(ctrl,"CODEPAGE")))
   {
      mystrncpy(jam_codepage,getkludgedata(str),20);
      free(str);
   }
   
   if((str=MsgGetCtrlToken(ctrl,"MSGID")))
   {
      mystrncpy(jam_msgid,getkludgedata(str),100);
      free(str);
   }
   
   MsgCloseMsg(msg);
   
   xlat=findreadxlat(var,group,jam_chrs,jam_codepage,postxlat->fromchrs);

   if(xlat && xlat->xlattab)
   {
      if((xlatres=xlatstr(jam_fromname,xlat->xlattab)))
      {
         mystrncpy(jam_fromname,xlatres,100);
         free(xlatres);
      }
   
      if((xlatres=xlatstr(jam_toname,xlat->xlattab)))
      {
         mystrncpy(jam_toname,xlatres,100);
         free(xlatres);
      }
   }
         
   if(group->netmail)
   {
      if(!matchname(var->realnames,jam_fromname) && !matchname(var->realnames,jam_toname)) 
         return;
   }
   
   if(xlat && postxlat->xlattab)
   {
      if((xlatres=xlatstr(jam_fromname,postxlat->xlattab)))
      {
         mystrncpy(jam_fromname,xlatres,100);
         free(xlatres);
      }
   }
      
   strcpy(fromname,jam_fromname);
   strcpy(fromaddr,jam_fromaddr);
   strcpy(msgid,jam_msgid);
}

void cancelmessage(struct var *var,uchar *article,struct xlat *postxlat)
{
   uchar *at,*pc,*ctrl,*str;
   struct group *group;
   ulong num,smapinum,ctrllen;
   struct xlat *xlat;
   uchar *xlatres;
   uchar fromname[100],chrs[20],codepage[20];
   int counts;
   HMSG msg;
   XMSG xmsg;
   
   if(article[0] != '<' || article[strlen(article)-1] != '>')
   {
      socksendtext(var,"441 POST failed (Article to cancel not found)" CRLF);
      return;
   }

   strcpy(article,&article[1]);
   article[strlen(article)-1]=0;

   at=strchr(article,'@');
   pc=strchr(article,'$');

   if(!at || !pc)
   {
      socksendtext(var,"441 POST failed (Article to cancel not found)" CRLF);
      return;
   }

   *at=0;
   *pc=0;

   at++;
   pc++;

   if(strcmp(at,"SmapiNNTPd") != 0)
   {
      socksendtext(var,"441 POST failed (Article to cancel not found)" CRLF);
      return;
   }
   
   for(group=var->firstgroup;group;group=group->next)
      if(matchgroup(var->readgroups,group->group) && stricmp(pc,group->tagname) == 0) break;

   if(strcmp(at,"SmapiNNTPd") != 0)
   {
      socksendtext(var,"441 POST failed (Article to cancel not found)" CRLF);
      return;
   }

   num=atol(article);

   if(!smapiopenarea(var,group))
   {
      socksendtext(var,"441 POST failed (Article to cancel not found)" CRLF);
      return;
   }

   if(!(smapinum=MsgUidToMsgn(var->openarea,num,UID_EXACT)))
   {
      socksendtext(var,"441 POST failed (Article to cancel not found)" CRLF);
      return;
   }
  
   if(!(msg=MsgOpenMsg(var->openarea,MOPEN_READ,smapinum)))
   {
      os_logwrite("(%s) Could not open message %lu in \"%s\"",var->clientid,smapinum,var->opengroup->jampath);
      socksendtext(var,"441 POST failed (Could not open message to cancel)" CRLF);
      return;
   }
         
   ctrllen=MsgGetCtrlLen(msg);
   ctrl=malloc(ctrllen+1);
   
   if(!ctrl)
   {
      MsgCloseMsg(msg);
      return;
   }
   
   if(MsgReadMsg(msg,&xmsg,0,0,NULL,ctrllen,ctrl) == -1)
   {
      os_logwrite("(%s) Could not read message %lu in \"%s\"",var->clientid,smapinum,var->opengroup->jampath);
      socksendtext(var,"441 POST failed (Could not read message to cancel)" CRLF);
      free(ctrl);
      MsgCloseMsg(msg);
      return;
   }

   ctrl[ctrllen]=0;
      
   chrs[0]=0;
   codepage[0]=0;   

   mystrncpy(fromname,xmsg.from,100); 
   
   if((str=MsgGetCtrlToken(ctrl,"CHRS")))
   {
      mystrncpy(chrs,getkludgedata(str),20);
      free(str);
      stripchrs(chrs);
   }
   
   if((str=MsgGetCtrlToken(ctrl,"CHARSET")))
   {
      mystrncpy(chrs,getkludgedata(str),20);
      free(str);
   }
   
   if((str=MsgGetCtrlToken(ctrl,"CODEPAGE")))
   {
      mystrncpy(codepage,getkludgedata(str),20);
      free(str);
   }
   
   MsgCloseMsg(msg);
   free(ctrl);
   
   xlat=findreadxlat(var,group,chrs,codepage,postxlat->fromchrs);

   if(xlat && xlat->xlattab)
   {
      if((xlatres=xlatstr(fromname,xlat->xlattab)))
      {
         mystrncpy(fromname,xlatres,100);
         free(xlatres);
      }
   }

   if(!matchname(var->realnames,fromname))
   {
      socksendtext(var,"441 POST failed (Cannot cancel, message not from you)" CRLF);
      return;
   }

   if(!(xmsg.attr & MSGLOCAL))
   {
      socksendtext(var,"441 POST failed (Cannot cancel, message not local)" CRLF);
      return;
   }
      
   if(xmsg.attr & MSGSENT)
   {
      socksendtext(var,"441 POST failed (Cannot cancel, message already sent)" CRLF);
      return;
   }
   
   if(xmsg.attr & MSGLOCKED)
   {
      socksendtext(var,"441 POST failed (Cannot cancel, message is locked)" CRLF);
      return;
   }
   
   while(MsgLock(var->openarea) == -1)
   {
      os_logwrite("(%s) Messagebase \"%s\" is locked, waiting...",var->clientid,var->currentgroup->jampath);
      counts++;
      
      if(counts == 10)
      {
         os_logwrite("(%s) Failed to lock \"%s\"",var->clientid,var->currentgroup->jampath);
         socksendtext(var,"441 Local error: Failed lock message" CRLF);
         return;
      }
      
      os_sleep(1);
   }
                  
   if(!(msg=MsgOpenMsg(var->openarea,MOPEN_CREATE,0)))
   {
      socksendtext(var,"503 Local error: Could not create message" CRLF);
      return;
   }
      
   if(MsgKillMsg(var->openarea,smapinum) == -1)
   {
      os_logwrite("(%s) Could not delete message %lu in \"%s\"",var->clientid,num,var->opengroup->jampath);
      socksendtext(var,"441 Local error: Failed to delete message" CRLF);
   }
   else
   {
      os_logwrite("(%s) %s deleted message #%lu in %s",var->clientid,var->loginname,num,var->opengroup->tagname);
      socksendtext(var,"240 Article cancelled" CRLF);
   }
   
   MsgUnlock(var->openarea);
}

/* line should have some extra room. line may grow by one or two characters */
void tidyquote(char *line)
{
   int lastquote,numquotes,c;
   char *input,*initials;
   
   if(!(input=strdup(line)))
      return;

   strip(input);
   
   numquotes=0;
   lastquote=0;      
         
   for(c=0;input[c]!=0;c++)
   {
      if(input[c] == '>')
      {
         lastquote=c;
         numquotes++;
      }
      else if(input[c] == '<')
      {
         break;
      }
      else if(input[c] != ' ' && input[c+1] == ' ')
      {
         break;
      }
   }
               
   if(numquotes)
   {
      initials="";
                  
      /* Find end of initials */
      
      c=lastquote;
      
      while(c > 0 && input[c] == '>')
         c--;
         
      if(input[c] != '>')
      {
         /* Initials found */
         
         input[c+1]=0;
         
         while(c > 0 && input[c] != ' ' && input[c] != '>')
            c--;
            
         if(input[c] == ' ' || input[c] == '>') initials=&input[c+1];         
         else                                   initials=&input[c];
      }

      /* Recreate line */
      
      if(input[lastquote+1] == 0)
      {
         strcpy(line,"\x0d");
      }
      else            
      {
         strcpy(line," ");
         strcat(line,initials);
      
         for(c=0;c<numquotes;c++)
            strcat(line,">");
         
         strcat(line," ");
      
         if(input[lastquote+1] == ' ') strcat(line,&input[lastquote+2]);
         else                          strcat(line,&input[lastquote+1]);
      
         strcat(line,"\x0d");
      }
   }         

   free(input);
}

uchar *smartquote(uchar *oldtext,ulong maxlen,uchar *fromname)
{
   uchar *newtext,line[300],mark[100];
   int c,d,linebegins;

   if(!(newtext=malloc(maxlen)))
      return(NULL);

   d=0;

   for(c=0;fromname[c];c++)
      if(c==0 || (c!=0 && fromname[c-1]==32)) mark[d++]=fromname[c];

   mark[d]=0;

   c=0;
   d=0;

   while(oldtext[c])
   {
      linebegins=c;

      while(oldtext[c] != 13 && oldtext[c] != 0)
         c++;

      if(oldtext[c] == 13)
         c++;

      if(oldtext[linebegins] == '>' && c-linebegins < 200)
      {
         strcpy(line,mark);
         strcat(line,">");
         
         if(oldtext[linebegins+1] == '>')
            strcat(line," ");
            
         strncat(line,&oldtext[linebegins+1],c-linebegins-1);
         tidyquote(line);

         if(strlen(line)+d+1 > maxlen)
         {
            /* We ran out of room */
            free(newtext);
            return(NULL);
         }

         strcpy(&newtext[d],line);
         d+=strlen(line);
      }
      else
      {
         if(c-linebegins+d+1 > maxlen)
         {
            /* We ran out of room */
            free(newtext);
            return(NULL);
         }

         strncpy(&newtext[d],&oldtext[linebegins],c-linebegins);
         d+=c-linebegins;
      }
   }

   newtext[d]=0;

   return(newtext);
}

void setreply(struct var *var,ulong parentmsg,ulong newmsg)
{
   ulong smapinum;
   HMSG msg;
   XMSG xmsg;
   int i;
       
   if(!(smapinum=MsgUidToMsgn(var->openarea,parentmsg,UID_EXACT)))
      return;
      
   if(!(msg=MsgOpenMsg(var->openarea,MOPEN_READ,smapinum)))
      return;
   
   if(MsgReadMsg(msg,&xmsg,0,0,NULL,0,NULL) == -1)
   {
      MsgCloseMsg(msg);
      return;
   }

   for(i=0;i<MAX_REPLY;i++)
      if(xmsg.replies[i] == 0) break;
      
   if(i != MAX_REPLY)
      xmsg.replies[i] = newmsg;
   
   MsgCloseMsg(msg);
   
   if(!(msg=MsgOpenMsg(var->openarea,MOPEN_RW,smapinum)))
      return;

   MsgWriteMsg(msg,0,&xmsg,NULL,0,0,0,NULL);
   MsgCloseMsg(msg);
}

bool parseaddr(uchar *str,NETADDR *dest)
{
   unsigned int zone,net,node,point;
   char ch;

   /* Remove domain if present */
   
   if(strchr(str,'@'))     
      *strchr(str,'@')=0;

   if(sscanf(str,"%u:%u/%u.%u%c",&zone,&net,&node,&point,&ch) != 4)
   {
      point=0;

      if(sscanf(str,"%u:%u/%u%c",&zone,&net,&node,&ch) != 3)
         return(FALSE);
   }

   if(dest)
   {
      dest->zone=zone;
      dest->net=net;
      dest->node=node;
      dest->point=point;
   }
   
   return(TRUE);   
}

void command_post(struct var *var)
{
   uchar *text,*newtext,*xlatres,line[1000],buf[100],*ch;
   ulong allocsize,textlen,textpos,getctpos,c,d,parentmsg,pos,msgnum;
   bool finished,toobig;
   uchar from[100],fromaddr[100],toname[100],subject[100],organization[100],newsgroup[100];
   uchar contenttype[100],contenttransferencoding[100],reference[100],newsreader[100];
   uchar replyid[100],replyto[100],chrs[20],chrs2[20],codepage[20];
   uchar control[100],dispname[100],toaddr[100],quotename[100];
   uchar kludges[1000]; /* space for 10 lines should be enough */
   struct group *g;
   struct xlat *xlat;
   time_t t1,t2;
   struct tm *tp;
   int timeofs,timesign,tr,counts;
   bool flowed;
   FILE *fp;
   HMSG hmsg;
   XMSG xmsg;
   
   allocsize=POST_MAXSIZE+500; /* Some extra room for tearline and origin */

   if(!(text=malloc(allocsize)))
   {
      socksendtext(var,"503 Out of memory" CRLF);
      return;
   }

   socksendtext(var,"340 Send article to be posted. End with <CR-LF>.<CR-LF>" CRLF);

   finished=FALSE;
   toobig=FALSE;

   textpos=0;

   while(!finished && !var->disconnect && !get_server_quit() && sockreadline(var,line,1000))
   {
      if(line[0] && cfg_debug)
         printf("(%s) < %s",var->clientid,line);

      if(stricmp(line,"." CRLF) == 0)
      {
         finished=TRUE;
      }
      else
      {
         if(textpos + strlen(line) > POST_MAXSIZE-1)
         {
            toobig=TRUE;
         }
         else
         {
            strcpy(&text[textpos],line);
            textpos+=strlen(line);
         }
      }
   }

   text[textpos]=0;
   
   if(get_server_quit())
   {
      free(text);
      return;
   }

   if(toobig)
   {
      sockprintf(var,"441 Posting failed (message too long, maximum size %ld bytes" CRLF,POST_MAXSIZE);
      os_logwrite("(%s) POST failed (message too long, maximum size %ld bytes)",var->clientid,POST_MAXSIZE);
      free(text);
      return;
   }

   from[0]=0;
   fromaddr[0]=0;
   newsgroup[0]=0;
   subject[0]=0;
   replyto[0]=0;
   contenttype[0]=0;
   chrs[0]=0;
   chrs2[0]=0;
   contenttransferencoding[0]=0;
   reference[0]=0;
   organization[0]=0;
   newsreader[0]=0;
   control[0]=0;
   flowed=FALSE;

   textpos=0;
   textlen=strlen(text);

   finished=FALSE;

   while(text[textpos] != 0 && !finished)
   {
      c=0;

      for(;;)
      {
         if(text[textpos] == 0)
         {
            break;
         }
         else if(c>0 && text[textpos-1] == 13 && text[textpos] == 10)
         {
            if(c>1 && (text[textpos+1] == ' ' || text[textpos+1] == '\t'))
            {
               /* Multi-line header */

               while(text[textpos+1] == ' ' || text[textpos+1] == '\t')
                  textpos++;

               line[c-1]=' ';
            }
            else
            {
               line[c-1]=0;
               textpos++;
               break;
            }
         }
         else
         {
            if(c<999)
               line[c++]=text[textpos];
         }

         textpos++;
      }

      line[c]=0;
      strip(line);

      if(strnicmp(line,"From: ",6)==0)
      {
         if(line[strlen(line)-1] == '>' && strchr(line,'<'))
         {
            /* From: Mark Horton <mark@cbosgd.ATT.COM> */

            line[strlen(line)-1]=0;
            unmimecpy(fromaddr,strrchr(line,'<')+1,100,chrs,chrs2,20);
            strip(fromaddr);

            *strchr(line,'<')=0;
            unmimecpy(from,&line[6],100,chrs,chrs2,20);
            strip(from);
         }
         else if(line[strlen(line)-1] == ')' && strchr(line,'('))
         {
            /* From: mark@cbosgd.ATT.COM (Mark Horton) */

            line[strlen(line)-1]=0;
            unbackslashquote(strrchr(line,'(')+1); /* Comments should be un-backslash-quoted */
            unmimecpy(from,strrchr(line,'(')+1,100,chrs,chrs2,20);
            strip(from);

            *strrchr(line,'(')=0;
            unmimecpy(fromaddr,&line[6],100,chrs,chrs2,20);
            strip(fromaddr);
         }
         else
         {
            unmimecpy(from,&line[6],100,chrs,chrs2,20);
            unmimecpy(fromaddr,&line[6],100,chrs,chrs2,20);
         }

         if(strlen(from) > 0)
         {
            /* Remove quotes if any */
            
            if(from[0] == '\"' && from[strlen(from)-1]=='\"')
            {
               from[strlen(from)-1]=0;
               strcpy(from,&from[1]);
               unbackslashquote(from); /* Text in "" should be un-backslash-quoted */
            }
         }
      }
      else if(strnicmp(line,"Newsgroups: ",12)==0)
      {
         mystrncpy(newsgroup,&line[12],100);
      }
      else if(strnicmp(line,"Subject: ",9)==0)
      {
         unmimecpy(subject,&line[9],100,chrs,chrs2,20);
      }
      else if(strnicmp(line,"Reply-To: ",10)==0)
      {
         unmimecpy(replyto,&line[10],100,chrs,chrs2,20);
      }
      else if(strnicmp(line,"Content-Type: ",14)==0)
      {
         getctpos=14;

         if(!getcontenttypepart(line,&getctpos,contenttype,100))
            contenttype[0]=0;

         while(getcontenttypepart(line,&getctpos,buf,100))
         {
            if(strnicmp(buf,"charset=",8)==0)
               setcharset(&buf[8],chrs,chrs2,20);

            else if(stricmp(buf,"format=flowed")==0)
               flowed=TRUE;
         }
      }
      else if(strnicmp(line,"Content-Transfer-Encoding: ",27)==0)
      {
         getctpos=27;

         if(!getcontenttypepart(line,&getctpos,contenttransferencoding,100))
            contenttransferencoding[0]=0;
      }
      else if(strnicmp(line,"References: ",12)==0)
      {
         if(strrchr(line,'<'))
            mystrncpy(reference,strrchr(line,'<'),100);
      }
      else if(strnicmp(line,"Organization: ",14)==0)
      {
         unmimecpy(organization,&line[14],100,chrs,chrs2,20);
      }
      else if(strnicmp(line,"X-Newsreader: ",14)==0)
      {
         unmimecpy(newsreader,&line[14],100,chrs,chrs2,20);
      }
      else if(strnicmp(line,"User-Agent: ",12)==0)
      {
         unmimecpy(newsreader,&line[12],100,chrs,chrs2,20);
      }
      else if(strnicmp(line,"Control: ",9)==0)
      {
         mystrncpy(control,&line[9],100);
      }
      else if(line[0] == 0)
      {
         finished=TRUE; /* End of headers */
      }
   }
   
   /* Strip Re: */

   if(!cfg_nostripre && (strncmp(subject,"Re: ",4)==0 || strcmp(subject,"Re:")==0))
      strcpy(subject,&subject[4]);
   
   /* Truncate strings */

   from[36]=0;
   subject[72]=0;
   organization[70]=0;
   newsreader[75]=0;

   /* Check syntax */

   if(newsgroup[0] == 0)
   {
      sockprintf(var,"441 Posting failed (No valid Newsgroups line found)" CRLF);
      os_logwrite("(%s) POST failed (No valid Newsgroups line found)",var->clientid);
      free(text);
      return;
   }

   if(from[0] == 0 || fromaddr[0] == 0)
   {
      sockprintf(var,"441 Posting failed (No valid From line found)" CRLF);
      os_logwrite("(%s) POST failed (No valid From line found)",var->clientid);
      free(text);
      return;
   }

   if(strchr(newsgroup,','))
   {
      sockprintf(var,"441 Posting failed (Crossposts are not allowed)" CRLF);
      os_logwrite("(%s) POST failed (Crossposts are not allowed)",var->clientid);
      free(text);
      return;
   }

   if(contenttype[0] && stricmp(contenttype,"text/plain")!=0)
   {
      sockprintf(var,"441 Posting failed (Content-Type \"%s\" not allowed, please use text/plain)" CRLF,contenttype);
      os_logwrite("(%s) POST failed (Content-Type \"%s\" not allowed)",var->clientid,contenttype);
      free(text);
      return;
   }

   /* Decode message */
   
   if(stricmp(contenttransferencoding,"quoted-printable")==0)
   {
      decodeqpbody(&text[textpos],&text[textpos]);
   }
   else if(stricmp(contenttransferencoding,"base64")==0)
   {
      decodeb64(&text[textpos],&text[textpos]);
   }
   else if(contenttransferencoding[0] && stricmp(contenttransferencoding,"8bit")!=0 && stricmp(contenttransferencoding,"7bit")!=0)
   {
      sockprintf(var,"441 Posting failed (unknown Content-Transfer-Encoding \"%s\")" CRLF,contenttransferencoding);
      os_logwrite("(%s) POST failed (Content-Transfer-Encoding \"%s\" not allowed)",var->clientid,contenttransferencoding);
      free(text);
      return;
   }
   
   /* Reformat text */

   d=0;

   while(text[textpos])
   {
      c=0;

      for(;;)
      {
         if(text[textpos] == 0)
         {
            break;
         }
         else if(text[textpos] == 13 || c==999)
         {
            textpos++;
            break;
         }
         else
         {
            if(text[textpos]!=10) line[c++]=text[textpos];
            textpos++;
         }
      }

      line[c]=0;

      if(flowed && line[0]!=0 && line[0]!='>' && strncmp(line,"-- ",3)!=0)
      {
         if(line[0] == ' ')
            strcpy(line,&line[1]);

         if(line[strlen(line)-1] == ' ')
         {
            strip(line);
            strcpy(&text[d],line);
            d+=strlen(line);
            text[d++]=' ';
         }
         else
         {
            strip(line);
            strcpy(&text[d],line);
            d+=strlen(line);
            text[d++]=13;
         }
      }
      else
      {
         if(strncmp(line,"-- ",3)!=0)
            strip(line);

         strcpy(&text[d],line);
         d+=strlen(line);
         text[d++]=13;
      }
   }

   /* Reformat CR:s at the end of the text */

   while(d > 0 && text[d-1] == 13) d--;

   if(d > 0 && d <= POST_MAXSIZE-3)
      text[d++]=13;
   
   text[d]=0;

   /* Check access rights */
   
   for(g=var->firstgroup;g;g=g->next)
      if(stricmp(newsgroup,g->tagname)==0) break;

   if(!g)
   {
      sockprintf(var,"441 Posting failed (Unknown newsgroup %s)" CRLF,newsgroup);
      os_logwrite("(%s) POST failed (Unknown newsgroup %s)",var->clientid,newsgroup);
      free(text);
      return;
   }

   if(!(matchgroup(var->postgroups,g->group)))
   {
      sockprintf(var,"441 Posting failed (Posting access denied to %s)" CRLF,newsgroup);
      os_logwrite("(%s) POST failed (Posting access denied to %s)",var->clientid,newsgroup);
      free(text);
      return;
   }

   /* Check charset */

   if(chrs2[0])
   {
      sockprintf(var,"441 Posting failed (Message contains multiple charsets, \"%s\" and \"%s\")" CRLF,chrs,chrs2);
      os_logwrite("(%s) POST failed (Message contains multiple charsets, \"%s\" and \"%s\")",var->clientid,chrs,chrs2);
      free(text);
      return;
   }

   if(g->defaultchrs[0] == '!' || var->defaultreadchrs[0] == '!')
   {
      if(g->defaultchrs[0] == '!')
         xlat=findpostxlat(var,chrs,&g->defaultchrs[1]);
   
      else
         xlat=findpostxlat(var,chrs,&var->defaultreadchrs[1]);
   
      if(!xlat)
      {
         sockprintf(var,"441 Posting failed (Unsupported charset \"%s\" for area %s)" CRLF,chrs,g->tagname);
         os_logwrite("(%s) POST failed (Unsupported charset \"%s\" for area %s)",var->clientid,chrs,g->tagname);
         free(text);
         return;
      }     
   }
   else
   {      
      xlat=findpostxlat(var,chrs,NULL);

      if(!xlat)
      {
         sockprintf(var,"441 Posting failed (Unsupported charset \"%s\")" CRLF,chrs);
         os_logwrite("(%s) POST failed (Unsupported charset \"%s\")",var->clientid,chrs);
         free(text);
         return;
      }     
   }

   if(strnicmp(control,"cancel ",7)==0)
   {
      if(!cfg_nocancel)
      {
         cancelmessage(var,&control[7],xlat); 
         free(text);
         return;
      }
      else
      {
         sockprintf(var,"441 Posting failed (Cancel messages are not permitted)" CRLF);
         free(text);
         return;
      }
   }
   
   replyid[0]=0;
   toname[0]=0;
   toaddr[0]=0;
   parentmsg=0;
   
   quotename[0]=0;
      
   if(reference[0])
   {
      if(g->netmail)
         getparentinfo(var,reference,g->tagname,replyid,toname,toaddr,&parentmsg,xlat,TRUE);
      
      else
         getparentinfo(var,reference,g->tagname,replyid,toname,toaddr,&parentmsg,xlat,FALSE);
         
      strcpy(quotename,toname);
      toname[36]=0;
   }
   else
   {
      strcpy(toname,"All");
   }

   /* Parse To: */
   
   if(strnicmp(text,"To:",3)==0)
   {
      if(text[3] == ' ') c=4;
      else               c=3;
      
      d=0;
      
      while(text[c] != 13 && text[c] != 0)
      {
         if(d < 99) line[d++]=text[c++];
         else       c++;
      }
      
      line[d]=0;

      if(text[c] == 13) c++;
      if(text[c] == 13) c++;
         
      strcpy(text,&text[c]);    
      
      if((ch=strchr(line,',')))
      {
         /* Format: To: name,address */
      
         *ch=0;
         ch++;
         
         while(*ch == ' ')
            ch++;
         
         mystrncpy(toname,line,36);
         mystrncpy(toaddr,ch,100);
      
         strip(toname);
         strip(toaddr);
      }
      else
      {
         /* Format: To: name */
         
         mystrncpy(toname,line,36);
         strip(toname);
      }

      if(xlat->xlattab)
      {
         if((xlatres=xlatstr(toname,xlat->xlattab)))
         {
            mystrncpy(toname,xlatres,36);
            free(xlatres);
         }
      }
   }
   else if(g->netmail && !reference[0])
   {
      sockprintf(var,"441 Posting failed (No \"To:\" line found)" CRLF);
      free(text);
      return;
   }
   
   /* Validate address */
   
   if(g->netmail)
   {
      if(!toaddr[0])
      {
         sockprintf(var,"441 Posting failed (No destination address specified on \"To:\" line)" CRLF);
         free(text);
         return;
      }
      
      if(!parseaddr(toaddr,NULL))
      {
         sockprintf(var,"441 Posting failed (Invalid address %s)" CRLF,toaddr);
         free(text);
         return;
      }      
   }
   
   /* Reformat quotes */
      
   if(reference[0] && cfg_smartquote)
   {
      if((newtext=smartquote(text,allocsize,quotename)))
      {
         free(text);
         text=newtext;
      }
   }

   /* Add tearline and origin */
   
   if(!g->netmail && !g->local)
   {
      if(newsreader[0]==0 || cfg_notearline)  strcpy(line,CR "---" CR);
      else                                    sprintf(line,CR "--- %s" CR,newsreader);

      if(strlen(text) + strlen(line) < allocsize-1)
         strcat(text,line);

      if(num_origins >= 0)
      { 
         int r = rand() % (num_origins+1);
         sprintf(line," * Origin: %s (%s)" CR,cfg_origin[r],g->aka);
      }
      else if (strlen(organization) >0)
      {
         sprintf(line," * Origin: %s (%s)" CR,organization,g->aka);           
      }
      else sprintf(line," * Origin: %s %s (%s)" CR,SERVER_NAME,SERVER_VERSION,g->aka); 

      if(strlen(text) + strlen(line) < allocsize-1)
         strcat(text,line);
   }
   
   /* Do xlat */
   
   if(xlat->xlattab)
   {
      if((xlatres=xlatstr(from,xlat->xlattab)))
      {
         mystrncpy(from,xlatres,36);
         free(xlatres);
      }

      if((xlatres=xlatstr(subject,xlat->xlattab)))
      {
         mystrncpy(subject,xlatres,72);
         free(xlatres);
      }

      if((xlatres=xlatstr(text,xlat->xlattab)))
      {
         free(text);
         text=xlatres;
      }
   }
   
   /* Create header */
   
   memset(&xmsg,0,sizeof(xmsg));
   
   t1=time(NULL);
   tp=gmtime(&t1);
   tp->tm_isdst=-1;
   t2=mktime(tp);

   timeofs=(t1-t2)/60;
   timesign=timeofs < 0 ? -1 : 1;
   
   tp=localtime(&t1);
   
   TmDate_to_DosDate(tp,(union stamp_combo *)&xmsg.date_written);
   TmDate_to_DosDate(tp,(union stamp_combo *)&xmsg.date_arrived);

   pos=0;

   if(getcomma(var->realnames,&pos,dispname,100))
      if(!ispattern(dispname)) mystrncpy(from,dispname,36);
   
   if(!var->login && cfg_guestsuffix)
   {
      tr=36-strlen(cfg_guestsuffix)-1;
      if(tr < 0) tr=0;
      
      from[tr]=0;
      strcat(from,cfg_guestsuffix);
   }      

   xmsg.attr = MSGLOCAL;
   
   if(g->netmail)
      xmsg.attr |= MSGPRIVATE;

   parseaddr(g->aka,&xmsg.orig);
         
   if(g->netmail)
      parseaddr(toaddr,&xmsg.dest);
         
   mystrncpy(xmsg.from,from,sizeof(xmsg.from));
   mystrncpy(xmsg.to,toname,sizeof(xmsg.to));
   mystrncpy(xmsg.subj,subject,sizeof(xmsg.subj));

   xmsg.replyto=parentmsg;
      
   /* Create kludges */
   
   kludges[0]=0;
   
   if(!g->local)
      sprintf(&kludges[strlen(kludges)],"\01MSGID: %s %08lx",g->aka,get_msgid_num());
   
   if(replyid[0])
      sprintf(&kludges[strlen(kludges)],"\01REPLY: %s",replyid);
   
   if(!cfg_noreplyaddr)
   {
      if(replyto[0]) sprintf(&kludges[strlen(kludges)],"\01REPLYADDR %s",replyto);
      else           sprintf(&kludges[strlen(kludges)],"\01REPLYADDR %s",fromaddr);
   }

   sprintf(&kludges[strlen(kludges)],"\01PID: " SERVER_NAME " " SERVER_PIDVERSION);

   if(xlat->tochrs[0] && !g->nochrs)
   {
      setchrscodepage(chrs,codepage,xlat->tochrs);
      
      if(chrs[0])
         sprintf(&kludges[strlen(kludges)],"\01CHRS: %s 2",chrs);
      
      if(codepage[0])
         sprintf(&kludges[strlen(kludges)],"\01CODEPAGE: %s",codepage);
   }

   if(!cfg_notzutc)
   {
      sprintf(&kludges[strlen(kludges)],"\01TZUTC: %s%02d%02d",
         (t1 < t2 ? "-" : ""),
         (timesign * timeofs) / 60,
         (timesign * timeofs) % 60);
   }

   /* Write message */
         
   if(!smapiopenarea(var,g))
   {
      socksendtext(var,"503 Local error: Could not open messagebase" CRLF);
      free(text);
      return;
   }

   counts=0;
   
   while(MsgLock(var->openarea) == -1)
   {
      os_logwrite("(%s) Messagebase \"%s\" is locked, waiting...",var->clientid,g->jampath);
      counts++;
      
      if(counts == 10)
      {
         os_logwrite("(%s) Failed to lock \"%s\"",var->clientid,g->jampath);
         socksendtext(var,"503 Local error: Failed to lock messagebase" CRLF);
         free(text);
         return;
      }
      
      os_sleep(1);
   }
                  
   if(!(hmsg=MsgOpenMsg(var->openarea,MOPEN_CREATE,0)))
   {
      socksendtext(var,"503 Local error: Could not create message" CRLF);
      free(text);
      return;
   }

   if(MsgWriteMsg(hmsg,0,&xmsg,text,strlen(text),strlen(text),strlen(kludges),kludges) != 0)
   {
      os_logwrite("(%s) Failed to write message to messagebase \"%s\"",var->clientid,g->jampath);
      socksendtext(var,"503 Local error: Could not write message" CRLF);
      MsgCloseMsg(hmsg);
      free(text);
      return;
   }
   
   msgnum=MsgMsgnToUid(var->openarea,MsgGetNumMsg(var->openarea));

   if(parentmsg)
      setreply(var,parentmsg,msgnum);
      
   MsgCloseMsg(hmsg);
   MsgUnlock(var->openarea);
      
   socksendtext(var,"240 Article posted" CRLF);

   os_logwrite("(%s) Posted message to %s (#%ld)",var->clientid,newsgroup,msgnum);
   
   if(cfg_echomailjam && g->jampath[0]!='#' && g->jampath[0]!='$')
   {
      if(!(fp=fopen(cfg_echomailjam,"a")))
      {
         os_logwrite("(%s) Failed to open %s",var->clientid,cfg_echomailjam);
      }
      else
      {
         fprintf(fp,"%s %ld\n",g->jampath,xmsg.umsgid);
         fclose(fp);
      }
   }
   
   if(cfg_exitflag)
   {
         if(utime(cfg_exitflag,NULL) == -1)
         {
            fp = fopen(cfg_exitflag,"a");
            fclose(fp);
            os_logwrite("(%s) Flag created %s",var->clientid,cfg_exitflag);
         } else {
            os_logwrite("(%s) Flag touched %s",var->clientid,cfg_exitflag);
         }
   }
   
   free(text);
}
      
void command_authinfo(struct var *var)
{
   uchar *tmp,*opt,*next,*equal;
   bool flowed,showto;

   if(!(tmp=parseinput(var)))
   {
      socksendtext(var,"501 Only AUTHINFO USER or AUTHINFO pass are understood" CRLF);
      return;
   }

   if(stricmp(tmp,"user")!=0 && stricmp(tmp,"pass")!=0)
   {
      socksendtext(var,"501 Only AUTHINFO USER or AUTHINFO pass are understood" CRLF);
      return;
   }

   if(stricmp(tmp,"user")==0)
   {
      if(!(tmp=parseinput(var)))
      {
         socksendtext(var,"482 No user specified for AUTHINFO USER" CRLF);
         return;
      }

      mystrncpy(var->loginname,tmp,100);

      socksendtext(var,"381 Received login name, now send password" CRLF);
      return;
   }

   /* AUTHINFO PASS */

   if(var->loginname[0] == 0)
   {
      socksendtext(var,"482 Use AUTHINFO USER before AUTHINFO pass" CRLF);
      return;
   }

   if(!(tmp=parseinput(var)))
   {
      socksendtext(var,"482 No password specified for AUTHINFO PASS" CRLF);
      return;
   }

   mystrncpy(var->password,tmp,100);

   /* Parse loginname */

   opt=NULL;

   flowed=var->opt_flowed;
   showto=var->opt_showto;

   if(strchr(var->loginname,'/'))
   {
      opt=strchr(var->loginname,'/');
      *opt=0;
      opt++;
   }

   while(opt)
   {
      next=strchr(opt,',');

      if(next)
      {
         *next=0;
         next++;
      }

      equal=strchr(opt,'=');

      if(!equal)
      {
         sockprintf(var,"482 Invalid option format %s, use option=on/off" CRLF,opt);
         return;
      }

      *equal=0;
      equal++;

      if(stricmp(opt,"flowed")==0)
      {
         if(!(setboolonoff(equal,&flowed)))
         {
            sockprintf(var,"482 Unknown setting %s for option %s, use on or off" CRLF,equal,opt);
            return;
         }
      }
      else if(stricmp(opt,"showto")==0)
      {
         if(!(setboolonoff(equal,&showto)))
         {
            sockprintf(var,"482 Unknown setting %s for option %s, use on or off" CRLF,equal,opt);
            return;
         }
      }
      else
      {
         sockprintf(var,"482 Unknown option %s, known options: flowed, showto" CRLF,opt);
         return;
      }

      opt=next;
   }

   if(var->loginname[0])
   {
      if(!(login(var,var->loginname,var->password)))
      {
         socksendtext(var,"481 Authentication rejected" CRLF);
         return;
      }

      socksendtext(var,"281 Authentication accepted" CRLF);
   }
   else
   {
      socksendtext(var,"281 Authentication accepted (options set, no login)"  CRLF);
   }

   var->opt_flowed=flowed;
   var->opt_showto=showto;

   return;
}

void server(SOCKET s)
{
   uchar line[1000],lookup[200],*cmd;
   struct var var;

   struct hostent *hostent;
   struct sockaddr_in fromsa;
   int fromsa_len = sizeof(struct sockaddr_in);

   os_getexclusive();
   server_openconnections++;
   os_stopexclusive();

   var.disconnect=0;

   var.currentgroup=NULL;
   var.currentarticle=0;

   var.openarea=NULL;
   var.opengroup=NULL;

   var.firstgroup=NULL;
   var.firstreadxlat=NULL;
   var.firstpostxlat=NULL;
   var.firstreadalias=NULL;
   var.firstpostalias=NULL;
   var.firstxlattab=NULL;

   var.readgroups[0]=0;
   var.postgroups[0]=0;

   var.loginname[0]=0;
   var.password[0]=0;
   var.realnames[0]=0;
   
   var.opt_flowed=cfg_def_flowed;
   var.opt_showto=cfg_def_showto;

   if(getpeername(s,(struct sockaddr *)&fromsa,&fromsa_len) == SOCKET_ERROR)
   {
      os_showerror("getpeername() failed");

      shutdown(s,2);
      close(s);

      os_getexclusive();
      server_openconnections--;
      os_stopexclusive();

      return;
   }

   if(!(var.sio=allocsockio(s)))
   {
      os_showerror("allocsockio() failed");

      shutdown(s,2);
      close(s);

      os_getexclusive();
      server_openconnections--;
      os_stopexclusive();

      return;
   }

   sprintf(var.clientid,"%s:%u",inet_ntoa(fromsa.sin_addr),ntohs(fromsa.sin_port));

   mystrncpy(lookup,inet_ntoa(fromsa.sin_addr),200);

   if((hostent=gethostbyaddr((char *)&fromsa.sin_addr,sizeof(fromsa.sin_addr),AF_INET)))
      mystrncpy(lookup,hostent->h_name,200);

   os_logwrite("(%s) Connection established to %s",var.clientid,lookup);

   if(!checkallow(&var,inet_ntoa(fromsa.sin_addr)))
   {
      socksendtext(&var,"502 Access denied." CRLF);
      os_logwrite("(%s) Access denied (not in allow list)",var.clientid);

      shutdown(s,2);
      close(s);
      freesockio(var.sio);

      os_getexclusive();
      server_openconnections--;
      os_stopexclusive();

      return;
   }

   if((ulong)get_server_openconnections() > cfg_maxconn)
   {
      os_logwrite("(%s) Access denied (server full)",var.clientid);
      socksendtext(&var,"502 Maximum number of connections reached, please try again later" CRLF);

      shutdown(s,2);
      close(s);
      freesockio(var.sio);

      os_getexclusive();
      server_openconnections--;
      os_stopexclusive();

      return;
   }

   if(!(readgroups(&var)))
   {
      socksendtext(&var,"503 Failed to read group configuration file" CRLF);

      shutdown(s,2);
      close(s);
      freesockio(var.sio);

      os_getexclusive();
      server_openconnections--;
      os_stopexclusive();

      return;
   }

   if(!(readxlat(&var)))
   {
      socksendtext(&var,"503 Failed to read xlat configuration file" CRLF);

      shutdown(s,2);
      close(s);
      freesockio(var.sio);
      freegroups(&var);

      os_getexclusive();
      server_openconnections--;
      os_stopexclusive();

      return;
   }


   socksendtext(&var,"200 Welcome to " SERVER_NAME " " SERVER_VERSION " (posting may or may not be allowed, try your luck)" CRLF);

   while(!var.disconnect && !get_server_quit() && sockreadline(&var,line,1000))
   {
      strip(line);

      var.input=line;
      var.inputpos=0;

      if(line[0] && cfg_debug)
         printf("(%s) < %s\n",var.clientid,line);

      if((cmd=parseinput(&var)))
      {
         if(stricmp(cmd,"ARTICLE")==0)
         {
            command_abhs(&var,cmd);
         }
         else if(stricmp(cmd,"AUTHINFO")==0)
         {
            command_authinfo(&var);
         }
         else if(stricmp(cmd,"BODY")==0)
         {
            command_abhs(&var,cmd);
         }
         else if(stricmp(cmd,"HEAD")==0)
         {
            command_abhs(&var,cmd);
         }
         else if(stricmp(cmd,"STAT")==0)
         {
            command_abhs(&var,cmd);
         }
         else if(stricmp(cmd,"GROUP")==0)
         {
            command_group(&var);
         }
         else if(stricmp(cmd,"HELP")==0)
         {
            socksendtext(&var,"100 Help text follows" CRLF);
            socksendtext(&var,"Recognized commands:" CRLF);
            socksendtext(&var,CRLF);
            socksendtext(&var,"ARTICLE" CRLF);
            socksendtext(&var,"AUTHINFO" CRLF);
            socksendtext(&var,"BODY" CRLF);
            socksendtext(&var,"GROUP" CRLF);
            socksendtext(&var,"HEAD" CRLF);
            socksendtext(&var,"HELP" CRLF);
            socksendtext(&var,"IHAVE (not implemented, messages are always rejected)" CRLF);
            socksendtext(&var,"LAST" CRLF);
            socksendtext(&var,"LIST" CRLF);
            socksendtext(&var,"NEWGROUPS (not implemented, always returns an empty list)" CRLF);
            socksendtext(&var,"NEWNEWS (not implemented, always returns an empty list)" CRLF);
            socksendtext(&var,"NEXT" CRLF);
            socksendtext(&var,"QUIT" CRLF);
            socksendtext(&var,"SLAVE (has no effect)" CRLF);
            socksendtext(&var,"STAT" CRLF);
            socksendtext(&var,"XOVER (partially implemented, byte count and line count are always empty)" CRLF);
            socksendtext(&var,CRLF);
            socksendtext(&var,"SmapiNNTPd supports most of RFC-977 and also has support for AUTHINFO and" CRLF);
            socksendtext(&var,"limited XOVER support (RFC-2980)" CRLF);
            socksendtext(&var,"." CRLF);
         }
         else if(stricmp(cmd,"IHAVE")==0)
         {
            socksendtext(&var,"435 Article not wanted - do not send it" CRLF);
         }
         else if(stricmp(cmd,"LAST")==0)
         {
            command_last(&var);
         }
         else if(stricmp(cmd,"LIST")==0)
         {
            command_list(&var);
         }
         else if(stricmp(cmd,"NEWGROUPS")==0)
         {
            socksendtext(&var,"231 Warning: NEWGROUPS not implemented, returning empty list" CRLF);
            socksendtext(&var,"." CRLF);
         }
         else if(stricmp(cmd,"NEWNEWS")==0)
         {
            socksendtext(&var,"230 Warning: NEWNEWS not implemented, returning empty list" CRLF);
            socksendtext(&var,"." CRLF);
         }
         else if(stricmp(cmd,"NEXT")==0)
         {
            command_next(&var);
         }
         else if(stricmp(cmd,"POST")==0)
         {
            command_post(&var);
         }
         else if(stricmp(cmd,"SLAVE")==0)
         {
            socksendtext(&var,"202 Slave status noted (but ignored)" CRLF);
         }
         else if(stricmp(cmd,"QUIT")==0)
         {
            socksendtext(&var,"205 Goodbye" CRLF);
            var.disconnect=1;
         }
         else if(stricmp(cmd,"XOVER")==0)
         {
            command_xover(&var);
         }
         else
         {
            socksendtext(&var,"500 Unknown command" CRLF);
         }
      }
   }

   os_logwrite("(%s) Connection closed",var.clientid);

   if(var.openarea)
      MsgCloseArea(var.openarea);

   freegroups(&var);
   freexlat(&var);

   freesockio(var.sio);

   os_getexclusive();
   server_openconnections--;
   os_stopexclusive();

   shutdown(s,2);
   close(s);
}

/* limitations in SMAPI 2.4-RC2: */

 /* SLOW! It can take several seconds to open a JAM area with 80 000 messages. */
 /* setting links when replying to a message seems to only work partially in JAM areas */
 /* SMAPI adds garbage characters after FMPT and TOPT lines in netmails, a string termination issue? */
 
/* shared library p� de plattformar d�r hpt anv�nderv det */
/* l�sa copying */
