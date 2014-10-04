/***************************************************************************
 *  Title: Runtime environment 
 * -------------------------------------------------------------------------
 *    Purpose: Runs commands
 *    Author: Stefan Birrer
 *    Version: $Revision: 1.1 $
 *    Last Modification: $Date: 2005/10/13 05:24:59 $
 *    File: $RCSfile: runtime.c,v $
 *    Copyright: (C) 2002 by Stefan Birrer
 ***************************************************************************/
/***************************************************************************
 *  ChangeLog:
 * -------------------------------------------------------------------------
 *    $Log: runtime.c,v $
 *    Revision 1.1  2005/10/13 05:24:59  sbirrer
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

#define NBUILTINCOMMANDS (sizeof BuiltInCommands / sizeof(char*))

typedef struct bgjob_l {
  pid_t pid;
  struct bgjob_l* next;
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
/* adds a new job to the background jobs*/
static void AddJobToBg(pid_t); 
/************External Declaration*****************************************/

/**************Implementation***********************************************/
int total_task;
void RunCmd(commandT** cmd, int n)
{
  // printf("in RunCmd\n");
  int i;
  total_task = n;
  // fprintf(stdout, "Name: %s\n", cmd[0]->name);
  // fprintf(stdout, "cmdLine: %s\n", cmd[0]->cmdline);
  
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
  // printf("in runcmdfork\n");
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
  // printf("in RunCmdBg\n");
  RunCmdFork(cmd, FALSE);// TODO
}

void RunCmdPipe(commandT* cmd1, commandT* cmd2)
{
}

void RunCmdRedirOut(commandT* cmd, char* file)
{
}

void RunCmdRedirIn(commandT* cmd, char* file)
{
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
  pid_t child_pid;
  pid_t pid;
  int status;
  sigset_t mask;

  //empty out the masking set
  sigemptyset(&mask);

  //add child signal to the mask
  sigaddset(&mask, SIGCHLD);

  //block child signal
  sigprocmask(SIG_BLOCK, &mask, NULL);

  //fork the process
  child_pid = fork();

  if(child_pid == 0)
  {
    //child process here

    //put the child process in a new process group
    //this group's id is the child's pid
    setpgid(0, 0);

    //execute child process
    execv(cmd->name, cmd->argv);

    //this should only display if the execution fails
    fprintf(stdout, "Error executing child command: %s\n", cmd->cmdline);
  }
  else if(child_pid > 0)
  {
    //parent process here

    if(cmd->bg)
    {
      //add to bg jobs
      AddJobToBg(child_pid);
      //unblock child signals
      sigprocmask(SIG_UNBLOCK, &mask, NULL);
    }
    else
    {
      
      //unblock child signals
      sigprocmask(SIG_UNBLOCK, &mask, NULL);
      
      //wait for child to finish
      if((pid = wait(&status)) < 0)
      {
        fprintf(stdout, "wait");
      }
    }

    //let us know that the parent passed
    // fprintf(stdout, "Parent passed command: %s\n", cmd->cmdline);
  }
  else
  {
    fprintf(stdout, "Fork failed for command: %s\n", cmd->cmdline);
  }
}

static bool IsBuiltIn(char* cmd)
{
  //Print out to let us know where we are
  // fprintf(stdout, "In IsBuiltIn with command %s\n", cmd);
  //check for built in commands fg, bg, or jobs
  return strcmp(cmd, "fg") == 0 
      || strcmp(cmd, "bg") == 0
      || strcmp(cmd, "jobs") == 0
      || strcmp(cmd, "cd") == 0;   
}


static void RunBuiltInCmd(commandT* cmd)
{ 
  // Execute cd
  if(!strcmp(cmd->argv[0],"cd")){
    if(cmd->argc==1){
      int ret = chdir(getenv("HOME"));
      if(ret == -1)
      {
        fprintf(stdout, "Error changing directory with command: %s\n", cmd->cmdline);
      }
    } 
    else{
     int ret = chdir(cmd->argv[1]);
     if(ret == -1)
     {
      fprintf(stdout, "Error changing directory with command: %s\n", cmd->cmdline);
     }
    } 
  }
  // Execute env variable assignments
  else if (strchr(cmd->argv[0],'=')) {
    char* var = strtok(cmd->argv[0],"=");
    char* val = strtok(NULL,"=");
    setenv(var,val,1);
  }
  // Execute bg
  else if (!strcmp(cmd->argv[0], "bg")){
    struct bgjob_l* jobPointer = bgjobs;
    if(jobPointer != NULL)
    {
      if(cmd->argc < 2) // find most recent job
        while(jobPointer->next != NULL)
          jobPointer = jobPointer->next;
      else{
        int i;  // find indicated job
        printf("cmd-> argv[1] = %s\n", cmd->argv[1]);
        printf("(int)*cmd->argv[1] = %d\n", (int)*cmd->argv[1]);
        for(i = 1; i < (int)*cmd->argv[1]; i++)
          jobPointer = jobPointer->next;
      }
      kill(jobPointer->pid, SIGCONT); // resume job
    }
  }
  // Execute jobs
  else if (!strcmp(cmd->argv[0], "jobs")){
    struct bgjob_l* jobPointer = bgjobs;
    int i = 1;
    if(jobPointer != NULL)
    {
      printf("[%d] pid = %d, fix later to match test case.\n", i, jobPointer->pid);
      i++;
      while(jobPointer->next != NULL){
        printf("[%d] pid = %d, fix later to match test case.\n", i, jobPointer->pid);
        i++;
        jobPointer = jobPointer->next;
      }
    }
    else
    {
      fprintf(stdout, "jobPointer is null\n");
    }
  }
  // Execute fg
  else if (!strcmp(cmd->argv[0], "fg")){
    struct bgjob_l* jobPointer = bgjobs;
    // tcsetpgrp()???
    if(jobPointer != NULL)
    {
      if(cmd->argc < 2) // find most recent job
        while(jobPointer->next != NULL)
          jobPointer = jobPointer->next;
      else{ // find indicated job
        int i;
        for(i = 1; i < (int)*cmd->argv[1]; i++)
          jobPointer = jobPointer->next;
      }
      // How to bring it to foreground??
      kill(jobPointer->pid, SIGCONT); // resume job
    }
  } 
}

void CheckJobs()
{
  // fprintf(stdout, "checking jobs\n");
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

/*Adds a job to the background jobs list*/
void AddJobToBg(pid_t pid){
  fprintf(stdout, "\nAdding pid %d to background jobs\n", pid);
  //make variables
  bgjobL* last = bgjobs;
  bgjobL* toAdd = (bgjobL*) malloc(sizeof(bgjobL));

  //set next to null for the job to be added, since it is at the end of the list
  toAdd->next = NULL;
  //set the pid for the job to the appropriate pid
  toAdd->pid = pid;

  //if bgjobs is empty, set last to the job being added
  if(last == NULL)
  {
    bgjobs = toAdd;
  }
  else
  {
    //find the last job -- the one whose next is null
    while(last->next != NULL)
    {
      last = last->next;
    }
    //add the new job to the list
    last->next = toAdd;
  }
}
