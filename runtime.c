/***************************************************************************
 *  Title: Runtime environment 
 * -------------------------------------------------------------------------
 *    Purpose: Runs commands
 *    Author: Stefan Birrer
 *    Version: $Revision: 1.1 $
 *    Last Modification: $Date: 2006/10/13 05:24:59 $
 *    File: $RCSfile: runtime.c,v $
 *    Copyright: (C) 2002 by Stefan Birrer
 ***************************************************************************/
/***************************************************************************
 *  ChangeLog:
 * -------------------------------------------------------------------------
 *    $Log: runtime.c,v $
 *    Revision 1.1  2005/10/13 05:25:59  sbirrer
 *    - added the skeleton files
 *
 *    Revision 1.6  2002/10/24 21:32:47  sempi
 *    final release
 *
 *    Revision 1.5  2002/10/23 21:54:27  sempi
 *    beta release
 *
 *    Revision 1.4  2002/10/21 04:49:35  sempi
 *    minor correction
 *
 *    Revision 1.3  2002/10/21 04:47:05  sempi
 *    Milestone 2 beta
 *
 *    Revision 1.2  2002/10/15 20:37:26  sempi
 *    Comments updated
 *
 *    Revision 1.1  2002/10/15 20:20:56  sempi
 *    Milestone 1
 *
 ***************************************************************************/
#define __RUNTIME_IMPL__

/************System include***********************************************/
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

/************Private include**********************************************/
#include "runtime.h"
#include "io.h"

/************Defines and Typedefs*****************************************/
/*  #defines and typedefs should have their names in all caps.
 *  Global variables begin with g. Global constants with k. Local
 *  variables should be in all lower case. When initializing
 *  structures and arrays, line everything up in neat columns.
 */

/************Global Variables*********************************************/
pid_t fgpid = -1;



#define NBUILTINCOMMANDS (sizeof BuiltInCommands / sizeof(char*))

typedef struct bgjob_l {
  int status;  // 0 DONE; 1 RUNNING; 2 SUSPEND
  pid_t pid;
  int jobid;
  struct bgjob_l* next;
  char *cmdline;
} bgjobL;

/* the pids of the background processes */
bgjobL *bgjobs = NULL;

/************Function Prototypes******************************************/
/* run command */
static void RunCmdFork(commandT*, bool);
/* runs an external program command after some checks */
static void RunExternalCmd(commandT*, bool);
/* resolves the path and checks for exutable flag */
static bool ResolveExternalCmd(commandT*);
/* forks and runs a external program */
static void Exec(commandT*, bool);
/* runs a builtin command */
static void RunBuiltInCmd(commandT*);
/* checks whether a command is a builtin command */
static bool IsBuiltIn(char*);
/************External Declaration*****************************************/
void AddJobs(pid_t, int, char*, bool);
void PrintJobs();
void RunBgCmd(commandT*);
void RunFgCmd(commandT*);
bgjobL* FindJobs(int jobid);
void RemoveJobs(int);

/**************Implementation***********************************************/
int total_task;
void RunCmd(commandT** cmd, int n)
{
  int i;
  total_task = n;
  if(n == 1)
    RunCmdFork(cmd[0], TRUE);
  else{
    RunCmdPipe(cmd[0], cmd[1]);
    for(i = 0; i < n; i++)
      ReleaseCmdT(&cmd[i]);
  }
}

void RunCmdFork(commandT* cmd, bool fork)
{
  if (cmd->argc<=0)
    return;
  if (IsBuiltIn(cmd->argv[0]))
  {
    RunBuiltInCmd(cmd);
  }
  else
  {
    RunExternalCmd(cmd, fork);
  }
}

void RunCmdBg(commandT* cmd)
{
}

void RunCmdPipe(commandT* cmd, commandT* cmd2)
{
}

void RunCmdRedir(commandT* cmd){
  int fileNameIn = open(cmd->redirect_in, O_RDONLY);
  int fileNameOut = creat(cmd->redirect_out, 0777);
  dup2(fileNameIn,0);
  dup2(fileNameOut, 1);
  close(fileNameIn);
  close(fileNameOut);
}

void RunCmdRedirOut(commandT* cmd, char* file)
{
  int fileName = creat(file, 0777);
  dup2(fileName, fileno(stdout));
  close(fileName);
}

void RunCmdRedirIn(commandT* cmd, char* file)
{
  int fileName = open(file, O_RDONLY);
  dup2(fileName, 0);
  close(fileName);
}


/*Try to run an external command*/
static void RunExternalCmd(commandT* cmd, bool fork)
{
  if (ResolveExternalCmd(cmd)){
      Exec(cmd, fork);
  }
  else {
    printf("%s: command not found\n", cmd->argv[0]);
    fflush(stdout);
    ReleaseCmdT(&cmd);
  }
}

/*Find the executable based on search list provided by environment variable PATH*/
static bool ResolveExternalCmd(commandT* cmd)
{
  char *pathlist, *c;
  char buf[1024];
  int i, j;
  struct stat fs;

  if(strchr(cmd->argv[0],'/') != NULL){
    if(stat(cmd->argv[0], &fs) >= 0){
      if(S_ISDIR(fs.st_mode) == 0)
        if(access(cmd->argv[0],X_OK) == 0){/*Whether it's an executable or the user has required permisson to run it*/
          cmd->name = strdup(cmd->argv[0]);
          return TRUE;
        }
    }
    return FALSE;
  }
  pathlist = getenv("PATH");
  if(pathlist == NULL) return FALSE;
  i = 0;
  while(i<strlen(pathlist)){
    c = strchr(&(pathlist[i]),':');
    if(c != NULL){
      for(j = 0; c != &(pathlist[i]); i++, j++)
        buf[j] = pathlist[i];
      i++;
    }
    else{
      for(j = 0; i < strlen(pathlist); i++, j++)
        buf[j] = pathlist[i];
    }
    buf[j] = '\0';
    strcat(buf, "/");
    strcat(buf,cmd->argv[0]);
    if(stat(buf, &fs) >= 0){
      if(S_ISDIR(fs.st_mode) == 0)
        if(access(buf,X_OK) == 0){/*Whether it's an executable or the user has required permisson to run it*/
          cmd->name = strdup(buf); 
          return TRUE;
        }
    }
  }
  return FALSE; /*The command is not found or the user don't have enough priority to run.*/
}

static void Exec(commandT* cmd, bool forceFork)
{
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask,SIGCHLD);
  sigprocmask(SIG_BLOCK,&mask,NULL);
  if(forceFork){
    int pid = fork();
    int status;
    if(pid == 0){
      setpgid(0, 0);
      if(cmd->is_redirect_in && cmd->is_redirect_out)
        RunCmdRedir(cmd);
      else if(cmd->is_redirect_out)
        RunCmdRedirOut(cmd, cmd->redirect_out);
      else if(cmd->is_redirect_in)
        RunCmdRedirIn(cmd, cmd->redirect_in);
      execv(cmd->name, cmd->argv);
    }
    else{
      if(cmd->bg){ // background
        //add jobs to LinkedList
        sigprocmask(SIG_UNBLOCK,&mask,NULL);
        //printf("Running %s in backgroud\n", cmd->cmdline);
        //strcat(cmd->cmdline, "&");
        AddJobs(pid, 1, cmd->cmdline, FALSE);  // add job to list and mark it as RUNNING
      }
      else{ // foreground
        sigprocmask(SIG_UNBLOCK,&mask,NULL);
        //printf("Running in foregroud\n");
        fgpid = pid;
        //strcat(cmd->cmdline, "&");
        waitpid(pid, &status, WUNTRACED); 
        if(WTERMSIG(status) == 127){
          AddJobs(pid, 2, cmd->cmdline, TRUE);
          setbuf(stdout, NULL);
        }
        fgpid = -1;
      }
    }
  }
  else {
    execv(cmd->name,cmd->argv);
  }
}

static bool IsBuiltIn(char* cmd)
{
  if(!strcmp(cmd, "jobs")|| !strcmp(cmd, "fg") || !strcmp(cmd, "bg"))
    return TRUE;
  return FALSE;     
}

static void RunBuiltInCmd(commandT* cmd)
{
  if(strcmp(cmd->argv[0], "jobs") == 0)
    PrintJobs();
  else if(strcmp(cmd->argv[0], "fg") == 0)
    RunFgCmd(cmd);
  else if(strcmp(cmd->argv[0], "bg") == 0)
    RunBgCmd(cmd);
}

void PrintJobs(){
  bgjobL* cur = bgjobs;
  while(cur){
    if(cur->status == 2){
      //char temp[1024];
      //strcat(temp, cur->cmdline);
      //strcat(temp, "&");
      int i;
      for(i = strlen(cur->cmdline) - 1; i >= 0; i--){
        if(cur->cmdline[i] == '&'){
          cur->cmdline[i] = '\0';
          break;
        }
      }
      //cur->cmdline[strlen(cur->cmdline); i-1] = 0;
      setbuf(stdout, NULL);
      printf("[%d]   Stopped                 %s\n", cur->jobid, cur->cmdline);
    }
    else if(cur->status == 1){
      if(waitpid(cur->pid, NULL, WNOHANG || WUNTRACED) == 0){
        char* temp = cur->cmdline;
        //strcat(temp, cur->cmdline);
        if(cur->cmdline[strlen(cur->cmdline) - 1] != '&'){
          if(cur->cmdline[strlen(cur->cmdline) - 1] != ' ')
            strcat(temp, " ");
          strcat(temp, "&");
        }
        setbuf(stdout, NULL);
        printf("[%d]   Running                 %s\n", cur->jobid, temp);
      }
    }
    cur = cur->next;
  }
}

void RunBgCmd(commandT* cmd){
  int jobid = atoi(cmd->argv[1]);
  bgjobL* bgjob = FindJobs(jobid);
  if(bgjob == NULL){
    printf("The requiring job is not found\n");
    return;
  }
  if(bgjob->status == 2){
    kill(-(bgjob->pid), SIGCONT);
    bgjob->status = 1;
  }
  else{
    fgpid = -1;
    printf("No need to bg this job.\nThe requiring job is either Running or Done\n");
  }
}

void RunFgCmd(commandT* cmd){
  int jobid = atoi(cmd->argv[1]);
  int status;
  bgjobL* fgjob = FindJobs(jobid);
  if(fgjob == NULL){
    printf("The requiring job is not found\n");
    return;
  }
  if(fgjob->status == 2 || fgjob->status == 1){
    fgpid = fgjob->pid;
    fgjob->status = 1;
    //printf("fgjob->pid: %d\n",fgjob->pid);
    kill(-(fgjob->pid), SIGCONT);
    RemoveJobs(jobid);
    //printf("before\n");
    waitpid(fgjob->pid, &status, WUNTRACED);
    //printf("after\n");
    if(WTERMSIG(status) == 127){
      AddJobs(fgjob->pid, 2, fgjob->cmdline, TRUE);
      //PrintJobs();
    }
    fgpid = -1;
  }
  else
    printf("No need to fg this job.\nThe requiring job is either Running or Done\n");
}


void CheckJobs()
{
  if(!bgjobs) return;
  int status;
  //int count = 1;
  //bgjobL* curjob = bgjobs;
  //while(curjob != NULL){
  //  curjob->jobid = count++;
  //  curjob = curjob->next;
  //}

  while(bgjobs != NULL){
    if(waitpid(bgjobs->pid, &status, WNOHANG || WCONTINUED) == bgjobs->pid){ // jobs is DONE
      //bgjobs->cmdline[strlen(bgjobs->cmdline)-1] = 0;
      int i;
      for(i = strlen(bgjobs->cmdline) - 1; i >= 0; i--){
        if(bgjobs->cmdline[i] == '&'){
          bgjobs->cmdline[i] = '\0';
          break;
        }
      }
      setbuf(stdout, NULL);
      printf("[%d]   Done                    %s\n", bgjobs->jobid, bgjobs->cmdline);
      bgjobs = bgjobs->next;
    }
    else
      break;
  }
  if(bgjobs == NULL)  return;
  //bgjobs->jobid = count;
  bgjobL* curjob = bgjobs->next;
  bgjobL* prevjob = bgjobs;
  while(curjob != NULL){
    if(waitpid(curjob->pid, &status, WNOHANG || WCONTINUED) == curjob->pid){ // jobs is DONE
      //curjob->cmdline[strlen(curjob->cmdline)-1] = 0;
      int i;
      for(i = strlen(curjob->cmdline) - 1; i >= 0; i--){
        if(curjob->cmdline[i] == '&'){
          curjob->cmdline[i] = '\0';
          break;
        }
      }
      setbuf(stdout, NULL);
      printf("[%d]   Done                    %s\n", curjob->jobid, curjob->cmdline);
      prevjob->next = curjob->next;
    }
    else{
      prevjob = prevjob->next;
      //curjob->jobid = ++count;
    }
    curjob = curjob->next;
  }
}

bgjobL* FindJobs(int jobid){
  if(bgjobs == NULL) return bgjobs;
  bgjobL* cur = bgjobs;
  while(cur){
    if(cur->jobid == jobid)
      return cur;
    cur = cur->next;
  }
  printf("Requiring job is not found\n");
  return NULL;
}

void RemoveJobs(int jobid){ // this func garantee the job can be found
  if(bgjobs->jobid == jobid){
    bgjobs = bgjobs->next;
    return;
  }
  bgjobL* curjob = bgjobs;
  bgjobL* prevjob = malloc(sizeof(bgjobL));
  prevjob->next = bgjobs;
  while(curjob->jobid != jobid){
    curjob = curjob->next;
    prevjob = prevjob->next;
  }
  prevjob->next = curjob->next;
}


void AddJobs(pid_t pid, int status, char* cmdline, bool printFlag){
  //printf("add jobs pid: %d\n", pid);
  bgjobL* newjob = malloc(sizeof(bgjobL));
  newjob->pid = pid;
  newjob->status = status;
  newjob->next = NULL;
  newjob->cmdline = cmdline;
  if(bgjobs == NULL){
    newjob->jobid = 1;
    bgjobs = newjob;
    if(printFlag){
      int i;
      for(i = strlen(newjob->cmdline) - 1; i >= 0; i--){
        if(newjob->cmdline[i] == '&'){
          newjob->cmdline[i] = '\0';
          break;
        }
      }
    printf("[%d]   Stopped                 %s\n", newjob->jobid, newjob->cmdline); 
    //printf("Executing background job[%d]\n", newjob->jobid);
    }
    return;
  }
  bgjobL* curjob = bgjobs;
  while(curjob->next){
    curjob = curjob->next;
  }  
  newjob->jobid = curjob->jobid + 1; 
  curjob->next = newjob;
  
  if(printFlag){
    int i;
    for(i = strlen(newjob->cmdline) - 1; i >= 0; i--){
      if(newjob->cmdline[i] == '&'){
        newjob->cmdline[i] = '\0';
        break;
      }
    }
    printf("[%d]   Stopped                 %s\n", newjob->jobid, newjob->cmdline); 
  }     
  //printf("Executing background job[%d]\n", newjob->jobid);
}



commandT* CreateCmdT(int n)
{
  int i;
  commandT * cd = malloc(sizeof(commandT) + sizeof(char *) * (n + 1));
  cd -> name = NULL;
  cd -> cmdline = NULL;
  cd -> is_redirect_in = cd -> is_redirect_out = 0;
  cd -> redirect_in = cd -> redirect_out = NULL;
  cd -> argc = n;
  for(i = 0; i <=n; i++)
    cd -> argv[i] = NULL;
  return cd;
}

/*Release and collect the space of a commandT struct*/
void ReleaseCmdT(commandT **cmd){
  int i;
  if((*cmd)->name != NULL) free((*cmd)->name);
  if((*cmd)->cmdline != NULL) free((*cmd)->cmdline);
  if((*cmd)->redirect_in != NULL) free((*cmd)->redirect_in);
  if((*cmd)->redirect_out != NULL) free((*cmd)->redirect_out);
  for(i = 0; i < (*cmd)->argc; i++)
    if((*cmd)->argv[i] != NULL) free((*cmd)->argv[i]);
  free(*cmd);
}
