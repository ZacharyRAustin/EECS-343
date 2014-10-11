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
  struct bgjob_l* prev;
  char* status;
  char* cmdline;
  int id;
} bgjobL;

/* the pids of the background processes */
bgjobL *bgjobs = NULL;

/*foreground pid*/
pid_t fgpid = -1;

char* last_cmd;

int stopped = 0;

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
static void AddJobToBg(pid_t, int); 
/*Removes jobs with status = "Done" from the background jobs list*/
static void removeCompletedJobs();
/*Frees the given job*/
static void ReleaseJob(bgjobL*);
/*Wait for the foreground process to finish*/
static void wait_fg();
/*Removes the job with the given pid from the bgjobs list*/
static void RemoveJob(pid_t);

/************External Declaration*****************************************/

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

    //set last_cmd so we know the last command entered
    last_cmd = cmd->cmdline;
    //if it is a background job
    if(cmd->bg)
    {
      //add to bg jobs
      AddJobToBg(child_pid, 0);
      //unblock child signals
      sigprocmask(SIG_UNBLOCK, &mask, NULL);
    }
    else
    {
      //set foreground pid to the child pid
      fgpid = child_pid;
      //unblock child signals
      sigprocmask(SIG_UNBLOCK, &mask, NULL);
      //reset stopped so we can loop
      stopped = 0;
      //wait for the foreground process to finish
      wait_fg();
    }
  }
  else
  {
    //let us know that the fork failed
    fprintf(stdout, "Fork failed for command: %s\n", cmd->cmdline);
  }
}

static bool IsBuiltIn(char* cmd)
{
  //check for fg, bg, jobs, and cd as builtin commands
  return strcmp(cmd, "fg") == 0 
      || strcmp(cmd, "bg") == 0
      || strcmp(cmd, "jobs") == 0
      || strcmp(cmd, "cd") == 0;   
}


static void RunBuiltInCmd(commandT* cmd)
{ 
  // Execute cd
  if(!strcmp(cmd->argv[0],"cd"))
  {
    //if just cd
    if(cmd->argc==1)
    {
      //go HOME
      int ret = chdir(getenv("HOME"));
      //need this if for compiler warnings about ret even thought we don't do anything
      if(ret == -1)
      {
      }
    } 
    else
    {
      //try to go where it tells us
      int ret = chdir(cmd->argv[1]);
      //need this if for compiler warnings about ret even thought we don't do anything
      if(ret == -1)
      {
      }
    } 
  }
  // Execute bg
  else if (strcmp(cmd->argv[0], "bg") == 0)
  {
    //get job list
    struct bgjob_l* jobPointer = bgjobs;
    //if we have a list
    if(jobPointer != NULL)
    {
      // if no specific job given
      if(cmd->argc < 2)
      { 
        //find most recent
        while(jobPointer->next != NULL)
        {
          jobPointer = jobPointer->next;
        }
      }
      else
      {
        //get job id
        int job_num = atoi(cmd->argv[1]);

        //try to find the id
        while(jobPointer != NULL)
        {
          if(jobPointer->id == job_num)
          {
            break;
          }
          jobPointer = jobPointer->next;
        }
      }
      //if we found it
      if(jobPointer != NULL)
      {
        //send it a SIGCONT signal
        kill(-jobPointer->pid, SIGCONT);
        //set status to running
        jobPointer->status = "Running\0";
      }
    }
  }
  // Execute jobs
  else if (strcmp(cmd->argv[0], "jobs") == 0)
  {
    //get job list
    struct bgjob_l* jobPointer = bgjobs;
    //if there is a job list, go through it
    if(jobPointer != NULL)
    {
      //while we have a job
      while(jobPointer != NULL)
      {
        //print out status
        printf("[%d] %-24s%s%s\n", jobPointer->id, jobPointer->status, jobPointer->cmdline, strcmp(jobPointer->status, "Running") == 0 ?  " &" : "");
        fflush(stdout);
        //move to next job
        jobPointer = jobPointer->next;
      }
      //if any jobs are complete, remove them
      removeCompletedJobs();
    }
  }
  // Execute fg
  else if (strcmp(cmd->argv[0], "fg") == 0){
    struct bgjob_l* job = bgjobs;
    //if we have background jobs
    if(job != NULL)
    {
      // if just fg
      if(cmd->argc < 2) 
      {
        //find most recent job
        while(job->next != NULL)
        {
          job = job->next;
        }
      }
      else
      { // get job number to get
        int job_num = atoi(cmd->argv[1]);
        //find given job if it exists
        while(job != NULL)
        {
          if(job->id == job_num)
          {
            break;
          }
          job = job->next;
        }
      }
      //if the job is running, stop it
      if(job != NULL && strcmp(job->status, "Running") == 0)
      {
        kill(-job->pid, SIGTSTP);
      }
      //update foreground pid to equal the selected job's pid
      fgpid = job->pid;
      //continue the job
      kill(-job->pid, SIGCONT);
      //reset stopped value so we can loop
      stopped = 0;
      //remove the job from the background jobs list
      RemoveJob(fgpid);
      //wait for the new foreground job to finish
      wait_fg();
    }
  }
}

void CheckJobs()
{
  bgjobL* jobs = bgjobs;
  pid_t endid;
  int status;
  while(jobs != NULL)
  {
    //get the endid for the current job
    endid = waitpid(jobs->pid, &status, WNOHANG|WUNTRACED);

    //if it is equal to the job pid, then it has exited
    if(endid == jobs->pid)
    {
      //check if it exited normally
      if(WIFEXITED(status))
      {
        //if it has, print out id, status, and cmd line
        printf("[%d] %-24s%s\n", jobs->id, "Done", jobs->cmdline);
        fflush(stdout);
        //set status to done
        jobs->status = (char*) "Done\0";
      }
      //check if there was an uncaught signal
      else if(WIFSIGNALED(status))
      {
        //set status to error
        jobs->status = (char*) "Error\0";
      }
      //check if the process was stopped
      else if(WIFSTOPPED(status))
      {
        //set status to stopped
        jobs->status = (char*) "Stopped\0";
      }
    }
    //move on to next job
    jobs = jobs->next; 
  }
  //remove all completed jobs since they have been displayed
  removeCompletedJobs();
  
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
void AddJobToBg(pid_t pid, int stopped){
  //make variables
  bgjobL* last = bgjobs;
  bgjobL* toAdd = (bgjobL*) malloc(sizeof(bgjobL));

  //set next to null for the job to be added, since it is at the end of the list
  toAdd->next = NULL;
  //set the pid for the job to the appropriate pid
  toAdd->pid = pid;
  //set previous to null, will be replaced later if needed
  toAdd->prev = NULL;
  //set status to running
  toAdd->status = (char*) "Running\0";
  //unless we are getting it from ctrl+z
  if(stopped)
  {
    toAdd->status = (char*) "Stopped\0";
  }
  //set the cmdline of the new job
  toAdd->cmdline = last_cmd;

  //if bgjobs is empty, set last to the job being added
  if(last == NULL)
  {
    toAdd->id = 1;
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
    //set the new job to point correctly to the previous job
    toAdd->prev = last;
    //set the new job's id correctly
    toAdd->id = last->id + 1;
  }
  //if it was stopped by ctrl+z
  if(stopped)
  {
    //print out id, status, and cmdline 
    printf("[%d] %-24s%s\n", toAdd->id, toAdd->status, toAdd->cmdline);
    fflush(stdout);
  }
}

void removeCompletedJobs(){
  bgjobL* current = bgjobs;
  bgjobL* prev = NULL;
  bgjobL* temp = NULL;
  //while we are not at the end of the list
  while(current != NULL)
  {
    //if status is done
    if(strcmp(current->status, "Done") == 0)
    {
      //if we are at the beginning of the list, 
      //move the beginning to the next job
      if(prev == NULL)
      {
        bgjobs = current->next;
      }
      else
      {
        //set the previous job's next pointer to the next job
        prev->next = current->next;
        //set the next job's prev pointer to the prev job
        current->next->prev = prev;
      }
      //set the temp pointer to the next job so we have it after we free the current job
      temp = current->next;
      //release the current job
      ReleaseJob(current);
      //update current to the next job
      current = temp;
    }
    else
    {
      //move on to the next job
      current = current->next;
    }
  }
}

void ReleaseJob(bgjobL* toRelease){
  free(toRelease);
}

void StopJob(){
  if(fgpid > 0)
  {
    kill(-fgpid, SIGTSTP);
    stopped = 1;
    AddJobToBg(fgpid, 1);
    fgpid = -1;
    CheckJobs();
  }
}

void KillJob(){
  //if we have a valid foreground job
  if(fgpid > 0)
  {
    //send it a SIGINT
    kill(fgpid, SIGINT);
  }
}

void wait_fg(){
  if(fgpid > 0)
  {
    int status;
    //wait for the foreground job to finish
    while(waitpid(fgpid, &status, WNOHANG|WUNTRACED) == 0 && !stopped)
    {
      //sleep when not finished yet
      sleep(0.5);
    }
    //set no foreground job when finished
    fgpid = -1;
  }
}

//Remove the given job from the background jobs list
void RemoveJob(pid){
  //get the list
  bgjobL* job = bgjobs;
  //while it isn't null
  while(job != NULL)
  {
    //if this is our job
    if(job->pid == pid)
    {
      //if this is the only job in the background jobs list
      if(job->prev == NULL && job->next == NULL)
      {
        //set the list pointer to null
        bgjobs = NULL;
      }
      else
      {
        //if is isn't the first element
        if(job->prev != NULL)
        {
          //set the previous job's next pointer to the current job's next pointer
          job->prev->next = job->next;
        }
        //if it isn't the last element
        if(job->next != NULL)
        {
          //set the next job's prev pointer to the current job's prev pointer
          job->next->prev = job->prev;
        }
      }
      //release the job
      ReleaseJob(job);
      //stop the loop
      break;
    }
    //move to the next job
    job = job->next;
  }
}